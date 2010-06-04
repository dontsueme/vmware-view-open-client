/*********************************************************
 * Copyright (C) 1998-2010 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is released with an additional exemption that
 * compiling, linking, and/or using the OpenSSL libraries with this
 * program is allowed.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * fileLockWin32.c --
 *
 *      Host-specific file locking functions for win32 hosts
 */

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <tlhelp32.h>
#include <errno.h>
#include <string.h>

#include "vmware.h"
#include "msg.h"
#include "log.h"
#include "util.h"
#include "str.h"
#include "file.h"
#include "fileInt.h"
#include "fileLock.h"

#include "unicodeOperations.h"

#define LOGLEVEL_MODULE main
#include "loglevel_user.h"

static Bool    attemptDynaLinking = TRUE;

static BOOL    (WINAPI *Process32NextFn)
                     (HANDLE handle, PROCESSENTRY32 *entry) = NULL;
static BOOL    (WINAPI *Process32FirstFn)
                     (HANDLE handle, PROCESSENTRY32 *entry) = NULL;
static HANDLE  (WINAPI *CreateToolhelp32SnapshotFn)
                     (DWORD flags, DWORD processID) = NULL;

static BOOL    (WINAPI *GetProcessTimesFn) (HANDLE Process,
                                            FILETIME *CreationTime,
                                            FILETIME *ExitTime,
                                            FILETIME *KernelTime,
                                            FILETIME *UserTime) = NULL;


/*
 *----------------------------------------------------------------------
 *
 * DynaLink
 *
 *      Attempt to dynamically link the necessary functions.
 *
 * Results:
 *     None
 *
 * Side effects:
 *     None
 *
 *----------------------------------------------------------------------
 */

static void
DynaLink(void)
{
   // Racey but effective caching of the dynalinked function pointers
   if (attemptDynaLinking) {
      HMODULE dllHandle = GetModuleHandleA("kernel32");

      if (dllHandle) {
         // Look for the entry points to be used
         CreateToolhelp32SnapshotFn = (void *) GetProcAddress(dllHandle,
                                                 "CreateToolhelp32Snapshot");
         Process32FirstFn = (void *) GetProcAddress(dllHandle,
                                                    "Process32First");
         Process32NextFn = (void *) GetProcAddress(dllHandle,
                                                   "Process32Next");
         GetProcessTimesFn = (void *) GetProcAddress(dllHandle,
                                                    "GetProcessTimes");
      } else {
         Warning(LGPFX" %s kernel32 missing?\n", __FUNCTION__);
      }

      attemptDynaLinking = FALSE;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * ObtainCreationTime --
 *
 *      Attempt to obtain the process creation time for the specified
 *      process.
 *
 * Results:
 *      TRUE    Obtained the process creation time
 *      FALSE   Did not obtained the process creation time
 *
 * Side effects:
 *     None
 *
 *----------------------------------------------------------------------
 */

static Bool
ObtainCreationTime(HANDLE processHandle,                    // IN:
                   unsigned long long *processCreationTime) // OUT:
{
   LARGE_INTEGER big;
   FILETIME      garbage;
   FILETIME      creationTime;

   if (GetProcessTimesFn == NULL) {
      return FALSE;
   }

   if ((*GetProcessTimesFn)(processHandle, &creationTime, &garbage,
                            &garbage, &garbage) == 0) {
      return FALSE;
   }

   big.LowPart = creationTime.dwLowDateTime;
   big.HighPart = creationTime.dwHighDateTime;

   *processCreationTime = big.QuadPart;

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * FurtherProcessValidation --
 *      Attempt to perform process creation time validation of a process.
 *
 * Results:
 *      TRUE    Process is valid
 *      FALSE   Process is not valid
 *
 * Side effects:
 *     None
 *
 *----------------------------------------------------------------------
 */

static Bool
FurtherProcessValidation(DWORD processID,                     // IN:
                         Bool gotFileCreationTime,            // IN:
                         unsigned long long fileCreationTime) // IN:
{
   if (gotFileCreationTime) {
      HANDLE processHandle;

      processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE,
                                  processID);

      if (processHandle != NULL) {
         unsigned long long processCreationTime;
         Bool               gotProcessCreationTime;

         gotProcessCreationTime = ObtainCreationTime(processHandle,
                                                     &processCreationTime);

         CloseHandle(processHandle);

         if (gotProcessCreationTime &&
             (fileCreationTime != processCreationTime)) {
               return FALSE; // Process exists but isn't the file creator
         }
      }
   }

   return TRUE; // Process exists
}


/*
 *----------------------------------------------------------------------
 *
 * HardProcessValidation --
 *
 *      Validate the specified processID by scanning the list of all
 *      processes.
 *
 * Results:
 *      TRUE    yes
 *      FALSE   no
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Bool
HardProcessValidation(DWORD processID,                     // IN:
                      Bool haveTime,                       // IN:
                      unsigned long long fileCreationTime) // IN:
{
   PROCESSENTRY32 pe32;
   DWORD          status;
   int            isValid;
   HANDLE         snapHandle;

   // Fail if all of the search entry points are not present
   if (!CreateToolhelp32SnapshotFn || !Process32FirstFn ||
       !Process32NextFn) {
      return TRUE; // assume OK
   }

   // Attempt to take a snapshot
   snapHandle = (*CreateToolhelp32SnapshotFn)(TH32CS_SNAPPROCESS, 0);

   if (snapHandle == INVALID_HANDLE_VALUE) {
      Warning(LGPFX" %s CreateToolhelp32Snapshot failed.\n",
              __FUNCTION__);

      return TRUE; // assume OK
   }

   // There must be a least one process running - us!
   pe32.dwSize = sizeof pe32;
   if (!(*Process32FirstFn)(snapHandle, &pe32)) {
      Warning(LGPFX" %s at least one process assertion failure: %d\n",
              __FUNCTION__, GetLastError());

      return TRUE; // assume OK
   }

   // Look through the snapshot and determine if the processID is present
   do {
      if (processID == pe32.th32ProcessID) {
         isValid = FurtherProcessValidation(processID, haveTime,
                                            fileCreationTime);

         goto exit;
      }

      pe32.dwSize = sizeof pe32;
   } while ((*Process32NextFn)(snapHandle, &pe32));

   status = GetLastError();

   if (status != ERROR_NO_MORE_FILES) {
      Warning(LGPFX" %s Process32Next failure: %d\n",
              __FUNCTION__, status);
   }

   isValid = FALSE;

exit:
   CloseHandle(snapHandle);

   return isValid;
}

/*
 *----------------------------------------------------------------------
 *
 * FileLockValidOwner --
 *
 *      Validate the lock file owner.
 *
 * Results:
 *      TRUE    Yes
 *      FALSE   No
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
FileLockValidOwner(const char *executionID, // IN:
                   const char *payload)     // IN:
{
   Bool               haveTime;
   DWORD              processID;
   unsigned long long fileCreationTime;

   if (sscanf(executionID, "%d", &processID) != 1) {
      Warning(LGPFX" %s pid conversion error on %s\n", __FUNCTION__,
              executionID);

      return TRUE; // assume OK
   }

   if (payload == NULL) {
      haveTime = FALSE;
   } else {
      if (sscanf(payload, "%"FMT64"u", &fileCreationTime) == 1) {
         haveTime = TRUE;
      } else {
         Warning(LGPFX" %s file creation time conversion error on %s\n",
                 __FUNCTION__, payload);

         haveTime = FALSE;
      }
   }

   return HardProcessValidation(processID, haveTime, fileCreationTime);
}


/*
 *----------------------------------------------------------------------
 *
 *  FileLockOpenFile --
 *
 *      Open the specified file
 *
 * Results:
 *      0       success
 *      > 0     failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *----------------------------------------------------------------------
 */

int
FileLockOpenFile(ConstUnicode pathName,        // IN:
                 int flags,                    // IN:
                 FILELOCK_FILE_HANDLE *handle) // OUT:
{
   DWORD status;
   const utf16_t *path;
   DWORD shareMode;
   DWORD creationDisposition;

   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   uint32 retries = 5;
   DWORD desiredAccess = 0;

   /* Set up the access modes */
   if (((flags & O_WRONLY) == 0) || (flags & O_RDWR)) {
      desiredAccess |= GENERIC_READ;
   }

   if ((flags & O_WRONLY) || (flags & O_RDWR)) {
      desiredAccess |= GENERIC_WRITE;
   }

   /* Always allow shared read, shared write and shared "deletion" */
   shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

   /* Set up the creation disposition */
   if (flags & O_CREAT) {
      creationDisposition = OPEN_ALWAYS;
   } else {
      creationDisposition = OPEN_EXISTING;
   }

   /* Obtain a UTF-16 path name */
   path = UNICODE_GET_UTF16(pathName);

   /* Too long for this implementation to handle? */
   if (!Unicode_StartsWith(pathName, "\\\\?\\") &&
       wcslen(path) > MAX_PATH) {
      UNICODE_RELEASE_UTF16(path);

      return ENAMETOOLONG;
   }

   /*
    * Open/Create a file. This is not as easy as it sounds.
    * There are times when a file or its parent directory are being
    * manipulated and an operation will fail because something "is being
    * used at the moment". Examples of this are sharing violations and
    * access denied errors. The good news is that these situations should
    * be rare so the work-around is to attempt a few retries before giving up.
    */ 

   while (TRUE) {
      *handle = CreateFileW(path, desiredAccess, shareMode, NULL,
                            creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);

      status = (*handle == INVALID_HANDLE_VALUE) ? GetLastError() :
                                                   ERROR_SUCCESS;

      if (!FileRetryThisError(status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      Sleep(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(path);

   return FileMapErrorToErrno(__FUNCTION__, status);
}


/*
 *----------------------------------------------------------------------
 *
 *  FileLockCloseFile --
 *
 *      Close the specified file
 *
 * Results:
 *      0       success
 *      > 0     failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *----------------------------------------------------------------------
 */

int
FileLockCloseFile(FILELOCK_FILE_HANDLE handle) // IN:
{
   return CloseHandle(handle) == 0 ?
                      FileMapErrorToErrno(__FUNCTION__, GetLastError()) : 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileLockReadFile --
 *
 *      Read a file.
 *
 * Results:
 *      0       success
 *      > 0     failure (errno)
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
FileLockReadFile(FILELOCK_FILE_HANDLE handle,  // IN:
                 void *buf,                    // IN:
                 uint32 requestedBytes,        // IN:
                 uint32 *resultantBytes)       // OUT:
{
   int err;
   DWORD status;

   status = ReadFile(handle, buf, requestedBytes, resultantBytes, NULL);

   if (status == 0) {
      err = FileMapErrorToErrno(__FUNCTION__, GetLastError());
   } else {
      err = 0;
   }

   return err;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileLockWriteFile --
 *
 *      Read a file.
 *
 * Results:
 *      0       success
 *      > 0     failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *-----------------------------------------------------------------------------
 */

int
FileLockWriteFile(FILELOCK_FILE_HANDLE handle,  // IN:
                  void *buf,                    // IN:
                  uint32 requestedBytes,        // IN:
                  uint32 *resultantBytes)       // OUT:
{
   int err;
   DWORD status;

   status = WriteFile(handle, buf, requestedBytes, resultantBytes, NULL);

   if (status == 0) {
      err = FileMapErrorToErrno(__FUNCTION__, GetLastError());
   } else {
      err = 0;
   }

   return err;
}


/*
 *----------------------------------------------------------------------
 *
 *  EffectivePath --
 *
 *     Obtain the effective path to use in a locking operation.
 *
 * Results:
 *      NULL    failure
 *      !NULL   success
 *
 * Side effects:
 *      Changes the host file system.
 *
 *----------------------------------------------------------------------
 */

static char *
EffectivePath(const char *filePath) // IN:
{
   char   *path;

   char   *physdrv = "\\\\.\\PhysicalDrive";
   size_t physdrvLength = strlen(physdrv);

   DynaLink();

   if (Str_Strncasecmp(physdrv, filePath, physdrvLength) == 0) {
      // physical drive; the lock file is in a temp directory
      char *tempDir = File_GetTmpDir(TRUE);

      if (tempDir == NULL) {
         path = NULL;
      } else {
         filePath += physdrvLength;
         path = Str_SafeAsprintf(NULL, "%s%s%s", tempDir, DIRSEPS "pd",
                                 filePath);
         free(tempDir);
      }
   } else {
      // The lock file is in the same directory as the disk file

      path = File_FullPath(filePath);
   }

   return path;
}


/*
 *---------------------------------------------------------------------------
 *
 * FileLockGetExecutionID --
 *
 *      Returns the executionID of the caller.
 *
 * Results:
 *      The executionID of the caller.
 *
 * Side effects:
 *      The executionID of the caller is not thread safe. Locking is currently
 *      done at the process level - all threads of a process are treated
 *      identically.
 *
 *---------------------------------------------------------------------------
 */

char *
FileLockGetExecutionID(void)
{
   return Str_SafeAsprintf(NULL, "%d", GetCurrentProcessId());
}


/*
 *----------------------------------------------------------------------
 *
 * FileLock_Lock --
 *
 *      Obtain a lock on a file; shared or exclusive access. Also specify
 *      how long to wait on lock acquisition - msecMaxWaitTime
 *
 *      msecMaxWaitTime specifies the maximum amount of time, in
 *      milliseconds, to wait for the lock before returning the "not
 *      acquired" status. A value of FILELOCK_TRYLOCK_WAIT is the
 *      equivalent of a "try lock" - the lock will be acquired only if
 *      there is no contention. A value of FILELOCK_INFINITE_WAIT
 *      specifies "waiting forever" to acquire the lock.
 *
 * Results:
 *      NULL    Lock not acquired. Check err.
 *              err     0       Lock Timed Out
 *              err     !0      errno
 *      !NULL   Lock Acquired. This is the "lockToken" for an unlock.
 *
 * Side effects:
 *      Changes the host file system.
 *
 *----------------------------------------------------------------------
 */

void *
FileLock_Lock(ConstUnicode filePath,        // IN:
              const Bool readOnly,          // IN:
              const uint32 msecMaxWaitTime, // IN:
              int *err)                     // OUT:
{
   void *lockToken;
   Unicode effectivePath;
   char creationString[32];
   unsigned long long creationTime;

   ASSERT(err);

   effectivePath = EffectivePath(filePath);

   if (!ObtainCreationTime(GetCurrentProcess(), &creationTime)) {
      creationTime = 0ULL;
   }

   Str_Sprintf(creationString, sizeof creationString, "%"FMT64"u",
               creationTime);

   lockToken = FileLockIntrinsic(effectivePath, !readOnly, msecMaxWaitTime,
                                 creationString, err);

   Unicode_Free(effectivePath);

   return lockToken;
}


/*
 *----------------------------------------------------------------------
 *
 * FileLock_IsLocked --
 *
 *      Is a file currently locked (at the time of the call)?
 *
 * Results:
 *      TRUE    YES
 *      FALSE   NO; if err is not NULL may check *err for an error
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
FileLock_IsLocked(ConstUnicode filePath,  // IN:
                  int *err)               // OUT:
{
   Bool isLocked;
   Unicode effectivePath;

   ASSERT(filePath);

   effectivePath = EffectivePath(filePath);
   if (effectivePath == NULL) {
      if (err != NULL) {
         *err = EINVAL;
      }

      return FALSE;
   }

   isLocked = FileLockIsLocked(effectivePath, err);

   Unicode_Free(effectivePath);

   return isLocked;
}


/*
 *----------------------------------------------------------------------
 *
 * FileLock_Unlock --
 *
 *      Release a lock held on the specified file.
 *
 * Results:
 *      0       unlocked
 *      >0      errno
 *
 * Side effects:
 *      Changes the host file system.
 *
 *----------------------------------------------------------------------
 */

int
FileLock_Unlock(ConstUnicode filePath,   // IN:
                const void *lockToken)   // IN:
{
   int err;
   Unicode effectivePath;

   ASSERT(lockToken);

   effectivePath = EffectivePath(filePath);

   err = FileUnlockIntrinsic(effectivePath, lockToken);

   Unicode_Free(effectivePath);

   return err;
}


/*
 *----------------------------------------------------------------------
 *
 * FileLock_DeleteFileVMX --
 *
 *      The VMX file delete primitive.
 *
 * Results:
 *      0       unlocked
 *      >0      errno
 *
 * Side effects:
 *      Changes the host file system.
 *
 * Note:
 *      THIS IS A HORRIBLE HACK AND NEEDS TO BE REMOVED ASAP!!!
 *
 *----------------------------------------------------------------------
 */

int
FileLock_DeleteFileVMX(ConstUnicode filePath)  // IN:
{
   int err;
   Unicode effectivePath;

   effectivePath = EffectivePath(filePath);

   err = FileLockHackVMX(effectivePath);

   Unicode_Free(effectivePath);

   return err;
}
