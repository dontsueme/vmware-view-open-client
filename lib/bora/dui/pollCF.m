/* **********************************************************
 * Copyright 2006 VMware, Inc.  All rights reserved. -- VMware Confidential
 * **********************************************************/

/*
 * pollCF.mm -- 
 *
 *    An implementation of the VMware Poll_* API based on CoreFoundation:
 *    CFRunLoop takes care of pumping events as needed.
 *
 *    PollCF is designed under the assumption that calls made to 
 *    register callbacks will happen on the main thread of the application.
 *    Because of this, the CFRunLoop running on the main thread will issue the
 *    callbacks on the main thread. If you are registering a callback that has 
 *    a significant amount of work to do you should start a worker thread within 
 *    that callback. If you are registering a callback from some other thread
 *    expect possible strange behaviour.
 *
 *    CFRunLoop has the notion of run loop modes eg: the default mode when the
 *    application is waiting for user input is a different run mode than when
 *    the application is minimized in the Dock. Currently this implementation 
 *    adds things to kCFRunLoopCommonModes; a pseudo mode refering to a few
 *    of the modes an application can be in. This is important since background
 *    activity, such as vmdb activity, needs to continue even if the application
 *    is minimized in the Dock for example.
 */

#import <Foundation/NSAutoreleasePool.h>
#include <CoreFoundation/CoreFoundation.h>
#include <exception>

extern "C" {
#include "vmware.h"
#include "pollImpl.h"
#include "util.h"
#include "hostinfo.h"
#include "syncRecMutex.h"
#include "dbllnklst.h"
}

/*
 * Logging rules
 * -------------
 *        Always: Anything unexpected.
 *    Level >= 1: Entry points.
 *    Level >= 2: Internal functions.
 */
#define LOGLEVEL_MODULE poll
#include "loglevel_user.h"

#ifdef VMX86_DEBUG
/*
 * Enum used to qualify the lists that an entry can be on.
 *
 * The lifecycle of an entry:
 *
 *  /->FREE    The entry is not used by anybody. It is linked on the free list.
 *  |   |
 *  |   v
 *  |  NONE    The entry is in transition to a new state. It is not linked on
 *  |   |      any list.
 *  |   v
 *  |  ACTIVE  The entry is used by PollCF and CoreFoundation. It is linked on
 *  |   |      the active list.
 *  |   v
 *  |  NONE    The entry is in transition to a new state. It is not linked on
 *  |   |      any list.
 *  |   v
 *  |  LIMBO   The entry is no longer used by PollCF, but it is still used by
 *  |   |      CoreFoundation. It is linked on the limbo list in debug builds,
 *  \---/      it is not linked on any list in non-debug builds.
 */
typedef enum ListType
{
   NONE,
   FREE,
   ACTIVE,
   LIMBO,
} ListType;
#endif


/*
 * A PollCF callback registered with PollCFCallback(), and unregistered with
 * PollCFCallbackRemove().
 */
typedef struct PollCFCb {
   int flags;
   PollerFunction func;
   void *clientData;
   PollClassSet classSet;
} PollCFCb;


/*
 * A PollCFEntry is the client data (private to PollCF) of a
 * CF[Socket|RunLoopTimer|RunLoopObserver]. One entry instance is associated
 * with exactly one CoreFoundation instance.
 *
 * To work well with CoreFoundation we follow the retain/release memory
 * management semantics of CoreFoundation. Use PollCFCreateEntry() to get an
 * entry with a retain count of 1 and PollCFReleaseEntry() when you no longer
 * need the entry (though the memory for the entry may not be freed or returned
 * to the pool immmediately).
 */
typedef struct PollCFEntry {
   DblLnkLst_Links l;
   unsigned int refCount;

   /*
    * For CFSocket, up to 2 (read and write) PollCF callbacks can be
    * registered. By convention we use index 0 to hold the read callback, and
    * index 1 to hold the write callback.
    *
    * For CFRunLoop[Timer|Observer], only 1 PollCF callback can be registered
    * and we always use index 0 for it.
    *
    * This knowledge is encapsulated in PollCFGetCallbackIndex().
    */
   PollEventType type;
   PollCFCb cbs[2];

   union {
      CFSocketRef runLoopSocket;
      CFRunLoopTimerRef runLoopTimer;
      CFRunLoopObserverRef runLoopObserver;
   };
   
#ifdef VMX86_DEBUG
   ListType onList;
   unsigned int incorrectCbCount;
#endif
} PollCFEntry;


/*
 * The global Poll state.
 */
typedef struct Poll
{  
   CFRunLoopRef runLoop;
   
   DblLnkLst_Links free;   // List of FREE PollCFEntry's.
   DblLnkLst_Links active; // List of ACTIVE PollCFEntry's.
#ifdef VMX86_DEBUG
   DblLnkLst_Links limbo;  // List of LIMBO PollCFEntry's.
#endif
} Poll;

static Poll *pollState;

static void PollCFCleanupActiveEntry(PollCFEntry *entry);
static PollCFEntry *PollCFLookupActiveEntry(Poll *poll, PollClassSet classSet,
                                            int flags, PollerFunction func,
                                            void *clientData,
                                            PollEventType type);

static void PollCFCallbackRemoveAll(void);

static PollCFEntry *PollCFCreateEntry(void);
static void PollCFReleaseEntry(PollCFEntry *entry, Bool invSock);

static void PollCFTimerCallback(CFRunLoopTimerRef timer, void *info); 
static void PollCFObserverCallback(CFRunLoopObserverRef observer,
                                   CFRunLoopActivity activity,
                                   void *info);
static void 
PollCFSocketCallback(CFSocketRef s,
                     CFSocketCallBackType callbackType,
                     CFDataRef address,
                     const void *data,
                     void *info);
static const void *PollCFRetainEntryCallback(const void *clientData);
static void PollCFReleaseEntryCallback(const void *clientData);

#ifdef VMX86_DEBUG
static void PollCFSetEntryListType(PollCFEntry *entry, ListType list);
static void PollCFCheckEntryOnList(PollCFEntry const *entry, ListType list);
#endif


/*
 *------------------------------------------------------------------------------
 *
 * PollCFLoopTimeout --
 *
 *      The poll loop.
 *      This is defined here to allow libraries like Foundry to link.
 *      When run with the PollCF implementation, however, this routine
 *      should never be called. The CoreFoundation framework will pump events.
 *
 * Result:
 *      None.
 *
 * Side effects:
 *      Not implemented.
 *
 *------------------------------------------------------------------------------
 */

static void
PollCFLoopTimeout(Bool loop,           // IN: Loop forever if TRUE, else do one pass.
                  Bool *exit,          // IN: NULL or set to TRUE to end loop.
                  PollClass pollClass, // IN: Class of events (POLL_CLASS_*).
                  int timeout)         // IN: Maximum time to sleep
{
   ASSERT_NOT_IMPLEMENTED(0);
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFInit --
 *
 *      Module initialization routine. 
 *
 * Results: 
 *      None.
 *
 * Side effects: 
 *      Initializes the module-wide state and sets pollState.
 *
 *------------------------------------------------------------------------------
 */

static void
PollCFInit(void)
{
   ASSERT(pollState == NULL);
   
   pollState = (Poll *)malloc(sizeof *pollState);
   
   pollState->runLoop = CFRunLoopGetCurrent();
   DblLnkLst_Init(&pollState->free);
   DblLnkLst_Init(&pollState->active);
#ifdef VMX86_DEBUG
   DblLnkLst_Init(&pollState->limbo);
#endif

   PollCFCallbackRemoveAll();
   LOG(1, ("%s: runLoop %p.\n", __func__, pollState->runLoop));
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFExit --
 *
 *      Module exit routine.
 *
 * Results: 
 *      None.
 *
 * Side effects: 
 *      Discards the module-wide state and clears pollState. Takes care
 *      of releasing all memory.
 *
 *------------------------------------------------------------------------------
 */

static void
PollCFExit(void)
{
   ASSERT(pollState);
   LOG(1, ("%s.\n", __func__));
   PollCFCallbackRemoveAll();
   free(pollState);
   pollState = NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFUpdateSocketCallbacks --
 *
 *      Set the socket callback settings depending on which of read and/or
 *      write callbacks have been registered with it.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
PollCFUpdateSocketCallbacks(PollCFEntry const *entry) // IN
{
   static CFOptionFlags const callbacksToDisable =
        kCFSocketReadCallBack
      | kCFSocketWriteCallBack;
   CFOptionFlags callbacksToEnable = 0;
   // The default flags (at socket creation time) are not what we want.
   static CFOptionFlags const flagsToDisable =
        kCFSocketCloseOnInvalidate
      | kCFSocketAutomaticallyReenableReadCallBack
      | kCFSocketAutomaticallyReenableWriteCallBack;
   CFOptionFlags flagsToEnable = 0;

   ASSERT(entry);
   ASSERT(entry->type == POLL_DEVICE);

   if (entry->cbs[0].func) {
      callbacksToEnable |= kCFSocketReadCallBack;
      flagsToEnable |= kCFSocketAutomaticallyReenableReadCallBack;
   }
   if (entry->cbs[1].func) {
      callbacksToEnable |= kCFSocketWriteCallBack;
      flagsToEnable |= kCFSocketAutomaticallyReenableWriteCallBack;
   }

   CFSocketDisableCallBacks(entry->runLoopSocket, callbacksToDisable);
   CFSocketEnableCallBacks(entry->runLoopSocket, callbacksToEnable);
   CFSocketSetSocketFlags(entry->runLoopSocket,
        (CFSocketGetSocketFlags(entry->runLoopSocket) & ~flagsToDisable)
      | flagsToEnable);
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFCleanupActiveEntry --
 *
 *      If an entry still has a registered PollCF callback, update its socket
 *      callbacks. Otherwise remove the entry from the active list (i.e. after
 *      the call returns, the entry is no longer used by PollCF).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
PollCFCleanupActiveEntry(PollCFEntry *entry) // IN
{
   if (entry->cbs[0].func || entry->cbs[1].func) {
      ASSERT(entry->type == POLL_DEVICE);
      PollCFUpdateSocketCallbacks(entry);
      return;
   }

   DEBUG_ONLY(PollCFCheckEntryOnList(entry, ACTIVE);)
   DblLnkLst_Unlink1(&entry->l); // Unlink from active list.
   DEBUG_ONLY(PollCFSetEntryListType(entry, NONE);)
   PollCFReleaseEntry(entry, TRUE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFCallbackRemoveAll --
 *
 *      Unregister all PollCF callbacks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
PollCFCallbackRemoveAll(void)
{
   Poll *poll = pollState;

   ASSERT(poll != NULL);

   while (DblLnkLst_IsLinked(&poll->active)) {
      PollCFEntry *entry =
         DblLnkLst_Container(poll->active.next, PollCFEntry, l);

      entry->cbs[0].func = NULL;
      entry->cbs[1].func = NULL;
      PollCFCleanupActiveEntry(entry);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFGetCallbackIndex --
 *
 *      Compute a PollCFCb index.
 *
 * Results:
 *      The callback index.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static INLINE unsigned int
PollCFGetCallbackIndex(PollEventType type, // IN
                       int flags)          // IN
{
   return (type == POLL_DEVICE && (flags & POLL_FLAG_WRITE)) ? 1 : 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFLookupActiveEntry --
 *
 *      Lookup an active entry.
 *
 * Results:
 *      On success: the entry.
 *      On failure: NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static PollCFEntry *
PollCFLookupActiveEntry(Poll *poll,            // IN
                        PollClassSet classSet, // IN
                        int flags,             // IN
                        PollerFunction func,   // IN
                        void *clientData,      // IN
                        PollEventType type)    // IN
{
   unsigned int cbIndex;
   DblLnkLst_Links *cur;

   ASSERT(poll);

   cbIndex = PollCFGetCallbackIndex(type, flags);
   for (cur = poll->active.next;
        cur != &poll->active;
        cur = cur->next) {
      PollCFEntry *entry = DblLnkLst_Container(cur, PollCFEntry, l);
      PollCFCb *cb = &entry->cbs[cbIndex];

      DEBUG_ONLY(PollCFCheckEntryOnList(entry, ACTIVE);)
      if (   func == cb->func
          && clientData == cb->clientData
          && classSet == cb->classSet
          && type == entry->type
          && flags == cb->flags) {
         return entry;
      }
   }

   return NULL;
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFCallback --
 *
 *      Register callbacks on CoreFoundation's main run loop. This should be
 *      called from the main thread. This is the more complex and flexible 
 *      interface for adding callbacks. You may also want to have a look at
 *      the simpler Poll_CB_Device and Poll_CB_RTime.
 *
 *      For the POLL_REALTIME, POLL_DEVICE, or POLL_MAIN queues entries can be
 *      inserted for good to fire on a periodic basis (by setting the
 *      POLL_FLAG_PERIODIC flag).
 *
 *      Otherwise, the callback fires only once and the callback is 
 *      automatically removed.
 *
 *      For POLL_REALTIME callbacks (ie: timer callbacks), "info" is the time in
 *      microseconds between execution of the callback. For
 *      POLL_DEVICE callbacks, info is the file descriptor.
 *
 * Results:
 *      VMWARE_STATUS_SUCCESS if the callback was successfully added to the run 
 *      loop.
 *
 * Side effects:
 *      Callbacks will be added to the main CFRunLoop and will be fired as 
 *      needed.
 *
 *------------------------------------------------------------------------------
 */

static VMwareStatus
PollCFCallback(PollClassSet classSet,   // IN: Or'ed classes of events eg: mks, 
                                        //     vmdb 
               int flags,               // IN: Depends on event type, customizes
                                        //     callback behaviour and 
                                        //     registration.
                                        //     Look at POLL_FLAGS_* in poll.h.
               PollerFunction func,     // IN: The function to call when the
                                        //     event is fired.
               void *clientData,        // IN: The data that will be passed to 
                                        //     the callback function
               PollEventType type,      // IN: The type of event
               PollDevHandle info,      // IN: The delay of a timer or a valid fd
               struct DeviceLock *lock) // IN: Must be NULL, unused
{
   Poll *poll = pollState;
   unsigned int newCbIndex;
   PollCFCb *newCb;

   ASSERT_BUG(5315, poll != NULL);
   ASSERT(func);
   ASSERT(lock == NULL);

   /*
    * POLL_FLAG_READ and POLL_FLAG_WRITE are mutually exclusive because
    * the signature of PollerFunc currently does not support it.
    */
   ASSERT((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) !=
          (POLL_FLAG_READ | POLL_FLAG_WRITE));

   // Make sure caller passed POLL_CS instead of POLL_CLASS. 
   ASSERT(classSet & POLL_CS_BIT);
   
   // Every callback must be in POLL_CLASS_MAIN (and possibly others).
   ASSERT((classSet & 1 << POLL_CLASS_MAIN) != 0);
   // Type is within enum range.
   ASSERT(type >= 0 && type < POLL_NUM_QUEUES);
   
   // We are calling this from the main thread.
   ASSERT(CFRunLoopGetCurrent() == poll->runLoop);
   
   if (type == POLL_DEVICE) {
      // When neither flag is passed, default to READ.
      if ((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) == 0) {
         flags |= POLL_FLAG_READ;
      }
   }

   /*
    * If this ASSERT triggers, the caller is probably doing something wrong.
    * See bug 197715 for an example of this.
    */
   ASSERT(PollCFLookupActiveEntry(poll, classSet, flags, func, clientData,
                                  type) == NULL);

   PollCFEntry *newEntry = PollCFCreateEntry();
   DEBUG_ONLY(PollCFCheckEntryOnList(newEntry, NONE);)
   LOG(1, ("%s: classSet 0x%x "
           "type %d(flags 0x%x%s%s%s, info %d) "
           "func %p(clientData %p): entry %p.\n",
           __func__, classSet, type, flags,
           (flags & POLL_FLAG_PERIODIC) ? " periodic" : "",
           (flags & POLL_FLAG_READ) ? " read" : "",
           (flags & POLL_FLAG_WRITE) ? " write" : "",
           info, func, clientData, newEntry));

   newCbIndex = PollCFGetCallbackIndex(type, flags);
   newCb = &newEntry->cbs[newCbIndex];
   newCb->func = func;
   newCb->clientData = clientData;
   newCb->classSet = classSet;
   newCb->flags = flags;
   newEntry->type = type;

   switch (type) {
   case POLL_REALTIME:
      {
         CFRunLoopTimerContext timerContext;
         CFTimeInterval period = (CFTimeInterval)info / 1000000;

         ASSERT_BUG(2430, info >= 0);
         
         timerContext.version = 0;
         timerContext.info = newEntry;
         timerContext.retain = PollCFRetainEntryCallback;
         timerContext.release = PollCFReleaseEntryCallback;
         timerContext.copyDescription = NULL; 
         
         newEntry->runLoopTimer = 
            CFRunLoopTimerCreate(kCFAllocatorDefault, 
                                 CFAbsoluteTimeGetCurrent() + period,
                                 /*
                                  * Values <= 0 mean one-shot.
                                  * Very small positive values (like 1 ns.)
                                  * starve the CFRunLoop.
                                  * So use a minimum value of 1 ms.
                                  */
                                 period >= .001 ? period : .001,
                                 0, 
                                 0, 
                                 PollCFTimerCallback,
                                 &timerContext);  
         
         CFRunLoopAddTimer(poll->runLoop, 
                           newEntry->runLoopTimer, 
                           kCFRunLoopCommonModes);
      }
      break;
         
   case POLL_MAIN_LOOP:
      {
         CFRunLoopObserverContext observerContext;
         
         ASSERT(info == 0);
         
         observerContext.version = 0;
         observerContext.info = newEntry;
         observerContext.retain = PollCFRetainEntryCallback;
         observerContext.release = PollCFReleaseEntryCallback;
         observerContext.copyDescription = NULL;
         
         newEntry->runLoopObserver = 
            CFRunLoopObserverCreate(kCFAllocatorDefault,
                                    kCFRunLoopBeforeSources,
                                    TRUE,
                                    0,
                                    PollCFObserverCallback,
                                    &observerContext);

         CFRunLoopAddObserver(poll->runLoop,
                              newEntry->runLoopObserver,
                              kCFRunLoopCommonModes);
      }
      break;
         
   case POLL_DEVICE:
      {
         CFSocketContext socketContext;
         CFSocketContext actualSocketContext;
         PollCFEntry *actualEntry;
         CFRunLoopSourceRef source;

         socketContext.version = 0;
         socketContext.info = newEntry;
         socketContext.retain = PollCFRetainEntryCallback;
         socketContext.release = PollCFReleaseEntryCallback;
         socketContext.copyDescription = NULL;

         /*
          * The CFSocket documentation specifies that the 2nd argument of
          * CFSocketEnableCallBacks() must be a subset of the 3rd argument of
          * CFSocketCreateWithNative().
          *
          * Since we want to selectively enable read or write callbacks after
          * the CFSocket has been created (the first call to
          * PollCFUpdateSocketCallbacks below), we must pass
          * kCFSocketReadCallBack | kCFSocketWriteCallBack.
          */
         newEntry->runLoopSocket =
            CFSocketCreateWithNative(kCFAllocatorDefault,
                                     info, // The fd.
                                       kCFSocketReadCallBack
                                     | kCFSocketWriteCallBack,
                                     PollCFSocketCallback,
                                     &socketContext);
         ASSERT(newEntry->runLoopSocket);
         /*
          * CoreFoundation caches CFSocket instances. If it finds a
          * match with the same 2nd argument in its cache, it will return a new
          * reference to it instead of creating a new instance, ignoring the
          * 3rd, 4th, and 5th arguments.
          *
          * We avoid the problem by always passing exactly the same 3rd, 4th,
          * and 5th arguments except for 'socketContext.info', which we use to
          * detect the condition.
          */
         CFSocketGetContext(newEntry->runLoopSocket, &actualSocketContext);
         actualEntry = (PollCFEntry *)actualSocketContext.info;
         if (actualEntry != newEntry) {
            /*
             * A pre-existing CFSocket was returned. It is already associated
             * with 'actualEntry'.
             */

            // Add the second PollCF callback to 'actualEntry'.
            ASSERT(!actualEntry->cbs[newCbIndex].func);
            actualEntry->cbs[newCbIndex] = *newCb;

            PollCFUpdateSocketCallbacks(actualEntry);

            /*
             * Discard 'newEntry', but without invalidating the CFSocket it
             * refers to, because 'actualEntry' is in use and refers to it as
             * well.
             */
            newCb->func = NULL;
            PollCFReleaseEntry(newEntry, FALSE);

            return VMWARE_STATUS_SUCCESS;
         }

         // A brand-new CFSocket was created for 'newEntry'.
         PollCFUpdateSocketCallbacks(newEntry);

         source = CFSocketCreateRunLoopSource(kCFAllocatorDefault,
                                              newEntry->runLoopSocket,
                                              0);
         ASSERT(source);

         CFRunLoopAddSource(poll->runLoop,
                            source,
                            kCFRunLoopCommonModes);
         CFRelease(source);
      }
      break;
         
      case POLL_VIRTUALREALTIME:
      case POLL_VTIME:
      default:
         NOT_IMPLEMENTED();
   }
   
   DblLnkLst_LinkFirst(&poll->active, &newEntry->l);
   DEBUG_ONLY(PollCFSetEntryListType(newEntry, ACTIVE);)
   return VMWARE_STATUS_SUCCESS;
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFCallbackRemove --
 *
 *      Uses the parameters to find the matching entry in the set of registered
 *      callbacks. This is the more complex and flexible  interface for removing
 *      callbacks. You may also want to have a look at the simpler 
 *      Poll_CB_DeviceRemove and Poll_CB_RTimeRemove.
 *
 * Results:
 *      TRUE if the entry was found and removed, FALSE otherwise.
 *
 * Side effects:
 *      If an entry matching the given parameters is found it's callback is 
 *      unregistered and the memory associated with the run loop entry is 
 *      recycled.
 *
 *------------------------------------------------------------------------------
 */

static Bool
PollCFCallbackRemove(PollClassSet classSet, // IN: Or'ed classes of events eg:  
                                            //     mks, vmdb 
                     int flags,             // IN: Depends on event type,
                                            //     customizes callback behaviour 
                                            //     and registration. Look at 
                                            //     POLL_FLAGS_* in poll.h
                     PollerFunction func,   // IN: The function to call when the
                                            //     event is fired.
                     void *clientData,      // IN: The data that will be passed  
                                            //     to the callback function
                     PollEventType type)    // IN: The type of event
{
   Poll *poll = pollState;
   PollCFEntry *entry;
   
   ASSERT(poll);
   ASSERT(type >= 0 && type < POLL_NUM_QUEUES);
   // We are calling this from the main thread.
   ASSERT(CFRunLoopGetCurrent() == pollState->runLoop);

   /*
    * POLL_FLAG_READ and POLL_FLAG_WRITE are mutually exclusive because
    * the signature of PollerFunc currently does not support it.
    */
   ASSERT((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) !=
          (POLL_FLAG_READ | POLL_FLAG_WRITE));

   if (type == POLL_DEVICE) {
      // When neither flag is passed, default to READ.
      if ((flags & (POLL_FLAG_READ | POLL_FLAG_WRITE)) == 0) {
         flags |= POLL_FLAG_READ;
      }
   }

   entry =
      PollCFLookupActiveEntry(poll, classSet, flags, func, clientData, type);
   LOG(1, ("%s: classSet 0x%x "
           "type %d(flags 0x%x%s%s%s) "
           "func %p(clientData %p): entry %p.\n",
           __func__, classSet, type, flags,
           (flags & POLL_FLAG_PERIODIC) ? " periodic" : "",
           (flags & POLL_FLAG_READ) ? " read" : "",
           (flags & POLL_FLAG_WRITE) ? " write" : "",
           func, clientData, entry));
   if (entry) {
      entry->cbs[PollCFGetCallbackIndex(type, flags)].func = NULL;
      PollCFCleanupActiveEntry(entry);
      return TRUE;
   }

   return FALSE;
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFCreateEntry --
 *
 *      Keeping with the CoreFoundation idioms of memory management this 
 *      "creates" an entry. The actual entry may be provided from a pool of
 *      PollCFEntries. The returned entry will have a ref count of 1
 *
 * Results:
 *      A PollCFEntry pointer. These are recycled from a pool of PollCFEntry
 *      structs so it is the caller's responsibility to set the fields properly.
 *      It is also the caller's reposibility to add the entry to the active 
 *      list. The entry will have a ref count of 1.
 *
 * Side effects:
 *      Memory for a new PollCFEntry may have been allocated if the pool was 
 *      empty.
 *
 *------------------------------------------------------------------------------
 */

static PollCFEntry *
PollCFCreateEntry(void)
{
   PollCFEntry *newEntry;
   Poll *poll = pollState;

   if (DblLnkLst_IsLinked(&poll->free)) {
      // Grab a free entry from the freeList.
      newEntry = DblLnkLst_Container(poll->free.next, PollCFEntry, l);
      ASSERT(newEntry->refCount == 0);
      ASSERT(newEntry->cbs[0].func == NULL);
      ASSERT(newEntry->cbs[1].func == NULL);
      DEBUG_ONLY(PollCFCheckEntryOnList(newEntry, FREE);)
      DblLnkLst_Unlink1(&newEntry->l);
   } else {
      newEntry = (PollCFEntry *)malloc(sizeof *newEntry);
      newEntry->refCount = 0;
      newEntry->cbs[0].func = NULL;
      newEntry->cbs[1].func = NULL;
      DblLnkLst_Init(&newEntry->l);
   }

   DEBUG_ONLY(PollCFSetEntryListType(newEntry, NONE);)
   DEBUG_ONLY(newEntry->incorrectCbCount = 0;)
   return (PollCFEntry*)PollCFRetainEntryCallback(newEntry);
   // Retained for the caller of this function.
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFReleaseEntry --
 *
 *      Keeping with the CoreFoundation idioms of memory management this 
 *      "releases" an entry. The actual memory might not be released. This gives
 *      the implementation freedom to pool entries and re-use them in 
 *      PollCFCreateEntry. The entry should already have been unlinked from any
 *      list it may be on. Freeing and unlinked entry may have unnexpected 
 *      results. If the entry's refCount is greater than 1 the entry will not
 *      be returned to the pool immmediately since someone else still has a
 *      "valid" pointer to it.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes the the coresponding entry from the CFRunLoop. In effect this
 *      unregisters the callback. May not free the entry back to the pool
 *      immmediately since CoreFoundation may still be pointng to it.
 *
 *------------------------------------------------------------------------------
 */

static void
PollCFReleaseEntry(PollCFEntry *entry, // IN: The entry to free.
                   Bool invSock)       // IN: Invalidate the CFSocket.
{
   LOG(2, ("%s: entry %p.\n", __func__, entry));
   ASSERT(entry);
   ASSERT(entry->cbs[0].func == NULL);
   ASSERT(entry->cbs[1].func == NULL);
   DEBUG_ONLY(PollCFCheckEntryOnList(entry, NONE);)

   switch (entry->type) {
   case POLL_DEVICE:
      ASSERT(entry->runLoopSocket != NULL);
      if (invSock) {
         CFSocketInvalidate(entry->runLoopSocket);
      }
      CFRelease(entry->runLoopSocket);
      break;

   case POLL_REALTIME:
      ASSERT(entry->runLoopTimer != NULL);
      CFRunLoopTimerInvalidate(entry->runLoopTimer);
      CFRelease(entry->runLoopTimer);
      break;

   case POLL_MAIN_LOOP:
      ASSERT(entry->runLoopObserver != NULL);
      CFRunLoopObserverInvalidate(entry->runLoopObserver);
      CFRelease(entry->runLoopObserver);
      break;

   default:
      NOT_IMPLEMENTED();
      break;
   }
   
#ifdef VMX86_DEBUG
   DblLnkLst_LinkFirst(&pollState->limbo, &entry->l);
   PollCFSetEntryListType(entry, LIMBO);
#endif
   
   /*
    * Entries are ref counted, it may not be the right time to put it back on 
    * the free list (let the release callback to this instance decide).
    */
   PollCFReleaseEntryCallback(entry);
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFHandleCallback --
 *
 *      Bridges the CFRunLoop callback world with the actual callbacks to be 
 *      perfomed. This allows us to define our own function signatures for our
 *      callbacks and not depend on platform specific implmentations such as 
 *      the various CoreFoundation callback signatures.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on the registered callback.
 *
 *------------------------------------------------------------------------------
 */

static void
PollCFHandleCallback(PollCFEntry *entry, // IN: The entry for the event
                                         //     that was fired.
                     PollCFCb *cb)       // IN: The callback that was fired.
{
   try {
      PollerFunction delegateFunc;
      void *clientData;

      LOG(1, ("%s: entry %p.\n", __func__, entry));
      // We are calling this from the main thread.
      ASSERT(CFRunLoopGetCurrent() == pollState->runLoop);
      ASSERT(entry);
      DEBUG_ONLY(PollCFCheckEntryOnList(entry, ACTIVE);)

      /*
       * Cache the bits we need to fire the callback in case the
       * callback is discarded for being non-periodic.
       */
      ASSERT(cb);
      delegateFunc = cb->func;
      ASSERT(delegateFunc);
      clientData = cb->clientData;

      /*
       * At the CFRunLoop level, we register all our source types as periodic.
       * This reduces the number of code paths to test: as far as CFRunLoop is
       * concerned, calling PollCFCleanupActiveEntry() here is no different
       * than letting '*delegateFunc' call PollCFCallbackRemove().
       */
      if (!(POLL_FLAG_PERIODIC & cb->flags)) {
         cb->func = NULL;
         PollCFCleanupActiveEntry(entry);
      }

      // Fire the callback.
      (*delegateFunc)(clientData);
   } catch (const std::exception &e) {
      Warning("%s: Error: Unhandled exception %s.\n", __func__, e.what());
      ASSERT(FALSE);
   } catch (...) {
      Warning("%s: Error: unhandled exception\n.", __func__);
      ASSERT(FALSE);
   }
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFTimerCallback --
 *
 *      Wrapper function to make CoreFoundation happy. Delegates work to
 *      PollCFHandleCallback.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on the delegated callback.
 *
 *------------------------------------------------------------------------------
 */

static void 
PollCFTimerCallback(CFRunLoopTimerRef ref, // IN: The timer that fired.
                    void *info)            // IN: The PollCFentry.
{
   PollCFEntry *entry = (PollCFEntry *)info;

   PollCFHandleCallback(entry, &entry->cbs[0]);
}


/*
 *------------------------------------------------------------------------------
 *
 * PollCFObserverCallback --
 *
 *      Wrapper function to make CoreFoundation happy. Delegates work to
 *      PollCFHandleCallback.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on the delegated callback.
 *
 *------------------------------------------------------------------------------
 */

static void 
PollCFObserverCallback(CFRunLoopObserverRef ref,   // IN: The observer
                                                   //     that fired.
                       CFRunLoopActivity activity, // IN: The loop's
                                                   //     activity stage.
                       void *info)                 // IN: The PollCFentry.
{
   PollCFEntry *entry = (PollCFEntry *)info;

   PollCFHandleCallback(entry, &entry->cbs[0]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFSocketCallback --
 *
 *      Wrapper function to make CoreFoundation happy. Delegates work to
 *      PollCFHandleCallback. CFRunLoop Sources are more flexible than
 *      Observers and Timers and have several different callbacks you can
 *      register. We are only implementing the callback for CFSocket.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on the delegated callback.
 *
 *-----------------------------------------------------------------------------
 */

static void 
PollCFSocketCallback(CFSocketRef ref,                   // IN: The socket that 
                                                        //     had activity
                     CFSocketCallBackType callbackType, // IN: Why the event 
                                                        //     was fired.
                     CFDataRef address,                 // IN: Don't care.
                     const void *data,                  // IN: Don't care.
                     void *info)                        // IN: The PollCFentry.
{
   PollCFEntry *entry = (PollCFEntry *)info;
   PollCFCb *cb = (callbackType & kCFSocketWriteCallBack) ? &entry->cbs[1]
                                                          : &entry->cbs[0];

   if (!cb->func) {
      /*
       * CFSocket bug triggered by the "Device*" unit tests at the end of this
       * file: if at the time you create a CFSocket with kCFSocketReadCallBack
       * and kCFSocketWriteCallBack capabilities, the fd is ready to read and
       * ready to write, then even if you disable the read (respectively write)
       * callback before adding the CFSocket to the run loop, it may fire (it
       * appears to be a race condition).
       *
       * So far we have noticed that such an incorrect callback fire only
       * happens once in the lifetime of the CFSocket. So,
       * 1) We detect the condition and ignore it by pretending that the
       *    callback never fired.
       * 2) In debug builds, we check that callbacks can incorrectly fire at
       *    most once in the lifetime of the CFSocket, because if it happened
       *    more often it could be indicative of a performance issue.
       */
      DEBUG_ONLY(entry->incorrectCbCount++;)
      return;
   }

   /*
    * We depend on the autorelease pool being emptied on a regular
    * basis.  However, if our CFSocket is very busy, we may not get to
    * the end of the main application runloop (which implicitly
    * empties the top-level autorelease pool).
    *
    * Since the vast majority of the work the UI does is in response
    * to socket activity, we'll create an autorelease pool before each
    * socket event and empty it when the socket is done.
    */
   NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
   PollCFHandleCallback(entry, cb);
   [pool release];
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFRetainEntryCallback --
 *
 *      Callback used to retain a PollCFEntry. We pass a function pointer to
 *      this callback so that CoreFoundation can let us know that it has a 
 *      pointer to the entry. This function can also be called directly from a
 *      function in PollCF in order to abstract away the details of ref 
 *      counting the entries.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Increments the refCount for entry by 1.
 *
 *-----------------------------------------------------------------------------
 */

static const void *
PollCFRetainEntryCallback(const void *clientData) // IN: The entry to retain
{
   PollCFEntry *entry = (PollCFEntry *)clientData;

   ASSERT(entry);
   entry->refCount += 1;
   LOG(2, ("%s: entry %p new refCount %d.\n", __func__, entry,
           entry->refCount));

   return entry;
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFReleaseEntryCallback --
 *
 *      Callback used to release a PollCFEntry. We pass a function pointer to
 *      this callback so that CoreFoundation can let us know that it no longer 
 *      needs a pointer to the entry. This function can also be called directly 
 *      from a function in PollCF in order to abstract away the details of ref 
 *      counting the entries.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If the refCount after the decrement is 0 the entry will be returned
 *      to the freeList.
 *
 *-----------------------------------------------------------------------------
 */

static void 
PollCFReleaseEntryCallback(const void *clientData) // IN: The entry to release
{
   PollCFEntry *releaseEntry = (PollCFEntry *)clientData;

   ASSERT(releaseEntry);
   ASSERT(releaseEntry->refCount > 0);
   releaseEntry->refCount -= 1;
   LOG(2, ("%s: entry %p new refCount %d.\n", __func__, releaseEntry,
           releaseEntry->refCount));
   if (releaseEntry->refCount == 0) {
#ifdef VMX86_DEBUG
      PollCFCheckEntryOnList(releaseEntry, LIMBO);
      DblLnkLst_Unlink1(&releaseEntry->l);
#endif
      ASSERT(releaseEntry->incorrectCbCount <= 1);

      DblLnkLst_LinkFirst(&pollState->free, &releaseEntry->l);
      DEBUG_ONLY(PollCFSetEntryListType(releaseEntry, FREE);)
   }
}


#ifdef VMX86_DEBUG
/*
 *-----------------------------------------------------------------------------
 *
 * PollCFSetEntryListType --
 *
 *      Sets entry's onList value to list.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      After setting the value the consistency of the entry is checked by 
 *      calling PollCFCheckEntryOnList.
 *
 *-----------------------------------------------------------------------------
 */

static void
PollCFSetEntryListType(PollCFEntry *entry, // IN: The entry to modify.
                       ListType list)      // IN: ListType to set.
{
   entry->onList = list;
   PollCFCheckEntryOnList(entry, list);
}


/*
 *-----------------------------------------------------------------------------
 *
 * PollCFCheckEntryOnList --
 *
 *      Checks the entry for consistency given the list type the entry should 
 *      be on.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      If the entry is found to be inconsistent this function will ASSERT.
 *
 *-----------------------------------------------------------------------------
 */

static void
PollCFCheckEntryOnList(PollCFEntry const *entry, // IN: The entry to check.
                       ListType list)            // IN: ListType to check.
{
   ASSERT(entry->onList == list && 
          DblLnkLst_IsLinked(&entry->l) == (list != NONE));
}


#endif


/*
 *-----------------------------------------------------------------------------
 *
 * Poll_InitCF --
 *
 *      Public init function for this Poll implementation. Poll loop will be
 *      up and running after this is called.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Poll_InitCF(void)
{
   static PollImpl cfImpl =
   {
      PollCFInit,
      PollCFExit,
      PollCFLoopTimeout,
      PollCFCallback,
      PollCFCallbackRemove,
   };

   Poll_InitWithImpl(&cfImpl);
}
