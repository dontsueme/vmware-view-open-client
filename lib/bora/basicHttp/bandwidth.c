/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
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
 * bandwidth.c --
 */

#include "vmware.h"
#include "vm_version.h"
#include "vm_basic_types.h"
#include "vm_assert.h"
#include "util.h"
#include "basicHttp.h"
#include "basicHttpInt.h"

#ifdef _WIN32
#include <sys/timeb.h>
#else // Linux
#include <sys/time.h>
#endif


#define TIMERATE_FACTOR    1000000     // The rate is in bytes/sec while the time in microsec

#define BANDWIDTH_WINDOW_SIZE    64 * 1024  // 64K

void BasicHttpBandwidthReset(BandwidthStatistics *bwStat);

void BasicHttpBandwidthUpdate(BandwidthStatistics *bwStat,
                              uint64 transferredBytes);

void BasicHttpBandwidthSlideWindow(BandwidthStatistics *bwStat);

VmTimeType BasicHttpBandwidthGetDelay(BasicHttpBandwidthGroup *group,
                                      BasicHttpRequest *request,
                                      BandwidthDirection direction);

extern void BasicHttpRemoveResumePollCallback(BasicHttpRequest *request);


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateBandwidthGroup --
 *
 *       Create a data structure to group a set of BasicHttpRequest for
 *       bandwidth shaping. Bandwidth is controlled at the group level. Unused
 *       bandwidths from slow connections will be shared by fast connections.
 *
 *       uploadLimit and downloadLimit are in bytes per second.
 *
 * Results:
 *       BasicHttpBandwidthGroup *.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpBandwidthGroup *
BasicHttp_CreateBandwidthGroup(uint64 uploadLimit,          // IN
                               uint64 downloadLimit)        // IN
{
   BasicHttpBandwidthGroup *group = NULL;

   group = (BasicHttpBandwidthGroup *) Util_SafeCalloc(1, sizeof *group);
   group->limits[BASICHTTP_UPLOAD] = uploadLimit;
   group->limits[BASICHTTP_DOWNLOAD] = downloadLimit;

   return group;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AddRequestToBandwidthGroup --
 *
 *       Add a BasicHttpRequest to the bandwidth group.
 *
 * Results:
 *       Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_AddRequestToBandwidthGroup(BasicHttpBandwidthGroup *group,    // IN
                                     BasicHttpRequest *request)         // IN
{
   Bool success = FALSE;

   if ((NULL == group) || (NULL == request)) {
      goto abort;
   }

   request->bwGroup = group;
   request->nextInBwGroup = group->requestList;
   group->requestList = request;

   success = TRUE;

abort:
   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_RemoveRequestFromBandwidthGroup --
 *
 *       Remove a BasicHttpRequest to the bandwidth group. After that, the
 *       bandwidth of the request is not under any control.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_RemoveRequestFromBandwidthGroup(BasicHttpBandwidthGroup *group,  // IN
                                          BasicHttpRequest *request)       // IN
{
   BasicHttpRequest **curRequest;

   if ((NULL == group) || (NULL == request) || (request->bwGroup != group)) {
      goto abort;
   }

   curRequest = &(group->requestList);
   while (NULL != *curRequest) {
      if (*curRequest == request) {
         *curRequest = (*curRequest)->nextInBwGroup;

         BasicHttpRemoveResumePollCallback(request);
         request->bwGroup = NULL;
         request->nextInBwGroup = NULL;

         break;
      }

      curRequest = &((*curRequest)->nextInBwGroup);
   }

abort:
   return;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_ChangeBandwidthGroup --
 *
 *       Change the upload/download bandwidth limit for the group.
 *
 *       uploadLimit and downloadLimit are in bytes per second.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_ChangeBandwidthGroup(BasicHttpBandwidthGroup *group,       // IN
                               uint64 uploadLimit,                   // IN
                               uint64 downloadLimit)                 // IN
{
   if (NULL == group) {
      return;
   }

   group->limits[BASICHTTP_UPLOAD] = uploadLimit;
   group->limits[BASICHTTP_DOWNLOAD] = downloadLimit;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_DeleteBandwidthGroup --
 *
 *       Delete the bandwidth group. All its contained requests are still alive
 *       after this call. But their bandwidth are not under control any more.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_DeleteBandwidthGroup(BasicHttpBandwidthGroup *group)       // IN
{
   BasicHttpRequest *request;

   if (NULL == group) {
      return;
   }

   for (request = group->requestList; request; request = request->nextInBwGroup) {
      BasicHttpRemoveResumePollCallback(request);
      request->bwGroup = NULL;
   }

   free(group);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpGetTimeOfDay --
 *
 *       A helper function to get the time of day in microsecond. A simple
 *       function here to avoid dragging in a lot of dependencies from
 *       lib/misc/hostif: Hostinfo_GetTimeOfDay.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static void
BasicHttpGetTimeOfDay(VmTimeType *time)         // OUT
{
#if _WIN32
   struct _timeb t;
   _ftime(&t);
   *time = (t.time * 1000000) + t.millitm * 1000;
#else // Linux
   struct timeval tv;
   gettimeofday(&tv, NULL);
   *time = ((VmTimeType)tv.tv_sec * 1000000) + tv.tv_usec;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpBandwidthReset --
 *
 *       Reset the statistics.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpBandwidthReset(BandwidthStatistics *bwStat)        // IN
{
   ASSERT(bwStat);
   memset(bwStat, 0, sizeof *bwStat);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpBandwidthUpdate --
 *
 *       Update the bandwidth statistics including the current transfer rate.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpBandwidthUpdate(BandwidthStatistics *bwStat,             // IN
                         uint64 transferredBytes)                 // IN
{
   VmTimeType elapsed;
   uint64 bytesDelta;

   ASSERT(bwStat);

   if (transferredBytes < bwStat->transferredBytes) {
      /*
       * Could happen after redirect.
       */
      BasicHttpBandwidthReset(bwStat);
   }

   BasicHttpGetTimeOfDay(&bwStat->lastTime);
   if (0 == bwStat->lastTime) {
      Log("BasicHttpBandwidthUpdate: Unable to get current time.\n");
      goto abort;
   }

   if (0 == bwStat->windowStartTime) {
      bwStat->windowStartTime = bwStat->lastTime;
   }

   elapsed = bwStat->lastTime - bwStat->windowStartTime;

   /*
    * Calc windowedBytes from rate if possible.
    */
   if ((bwStat->windowedBytes == 0) && (bwStat->windowedRate > 0)) {
      bwStat->windowedBytes = elapsed * bwStat->windowedRate / TIMERATE_FACTOR;
   }

   bytesDelta = transferredBytes - bwStat->transferredBytes;
   bwStat->transferredBytes = transferredBytes;
   bwStat->windowedBytes += bytesDelta;

   if (0 == elapsed) {
      goto abort;
   }

   bwStat->windowedRate = bwStat->windowedBytes * TIMERATE_FACTOR / elapsed;

abort:
   return;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpBandwidthSlideWindow --
 *
 *       Update the statistics window. For better results, the stat window is
 *       not slided if the current transfer rate exceeds the entitled limit and
 *       the transfer needs to be paused. Instead, the sliding is deferred
 *       until the transfer is resumed.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpBandwidthSlideWindow(BandwidthStatistics *bwStat)        // IN
{
   /*
    * Slid window to its 1/3.
    */
   static uint64 newWindowBytes = BANDWIDTH_WINDOW_SIZE / 3;

   ASSERT(bwStat);

   if ((bwStat->windowedBytes >= BANDWIDTH_WINDOW_SIZE)
         && (bwStat->windowedRate > 0)) {
      /*
       * Don't slide if windowedRate is 0. This could happen if the
       * first transferred buffer is larger than the window size.
       */

      bwStat->windowStartTime =
            bwStat->lastTime - newWindowBytes * TIMERATE_FACTOR / bwStat->windowedRate;
      bwStat->windowedBytes = newWindowBytes;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpBandwidthGetDelay --
 *
 *       Calculate the needed delay period. If the transfer doesn't exceed the
 *       entitled limit, no delay is needed hence return 0. Otherwise, delay
 *       an amount of time so that the next time when the transfer is resumed,
 *       windowedRate is the same as the entitlement.
 *
 * Results:
 *       Delay time in microsecond.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

VmTimeType
BasicHttpBandwidthGetDelay(BasicHttpBandwidthGroup *group,        // IN
                           BasicHttpRequest *request,             // IN
                           BandwidthDirection direction)          // IN
{
#if LIBCURL_VERSION_MAJOR <= 7 && LIBCURL_VERSION_MINOR < 18
   NOT_IMPLEMENTED();
   return 0;
#else
   BandwidthStatistics *stats;
   VmTimeType delay = 0;
   uint64 quota;
   uint64 pool = 0;
   BasicHttpRequest *curRequest;
   uint32 requestCount = 0;
   uint32 pausedMaskToCheck = 0;

   ASSERT(group && request);
   ASSERT(request->bwGroup == group);
   ASSERT((BASICHTTP_UPLOAD == direction) || (BASICHTTP_DOWNLOAD == direction));

   if (BASICHTTP_UPLOAD == direction) {
      pausedMaskToCheck = CURLPAUSE_SEND;
   } else if (BASICHTTP_DOWNLOAD == direction) {
      pausedMaskToCheck = CURLPAUSE_RECV;
   }

   if (request->pausedMask & pausedMaskToCheck) {
      Log("BasicHttpBandwidthGetDelay: This %s transfer is paused.\n",
          (BASICHTTP_UPLOAD == direction) ? "upload" : "download");
      goto abort;
   }

   /*
    * Go through the request list to count unpaused transfers.
    */
   curRequest = group->requestList;
   for (; curRequest; curRequest = curRequest->nextInBwGroup) {
      if (!(curRequest->pausedMask & pausedMaskToCheck)) {
         ++requestCount;
      }
   }

   if (requestCount <= 0) {
      Log("BasicHttpBandwidthGetDelay: All %s transfers are paused.\n",
          (BASICHTTP_UPLOAD == direction) ? "upload" : "download");
      goto abort;
   }

   stats = &(request->statistics[direction]);

   quota = group->limits[direction] / requestCount;

   if (stats->windowedRate <= quota) {
      goto abort;
   }

   /*
    * Collect unused bandwidth from slow connections and allot to fast ones.
    */
   curRequest = group->requestList;
   for (; curRequest; curRequest = curRequest->nextInBwGroup) {
      if (!(curRequest->pausedMask & pausedMaskToCheck)
            && (curRequest->statistics[direction].windowedRate < quota)) {
         pool += quota - curRequest->statistics[direction].windowedRate;
         --requestCount;
      }
   }

   if (requestCount > 0) {
      quota += pool / requestCount;
   }

   if (stats->windowedRate <= quota) {
      goto abort;
   }

   /*
    * Delay so that the next time, windowedRate is the same as quota.
    *    winRate=winBytes/T --> quota=winBytes/(T+dT)
    *    ==> dT = winBytes/quota - winBytes/winRate
    */
   delay = stats->windowedBytes * TIMERATE_FACTOR / quota -
             stats->windowedBytes * TIMERATE_FACTOR / stats->windowedRate;

abort:
   return delay;
#endif
}
