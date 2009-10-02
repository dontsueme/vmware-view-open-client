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
 * basicHttpInt.h --
 *
 *      BasicHttp internal declarations.
 */

#ifndef _BASIC_HTTP_INT_H_
#define _BASIC_HTTP_INT_H_


#include <curl/curl.h>
#include "basicHttp.h"


typedef struct BandwidthStatistics BandwidthStatistics;

typedef short BandwidthDirection;
enum {
   BASICHTTP_UPLOAD     = 0,
   BASICHTTP_DOWNLOAD   = 1,
};

struct BandwidthStatistics {
   uint64      transferredBytes;
   uint64      windowedBytes;

   uint64      windowedRate;
   VmTimeType  windowStartTime;
   VmTimeType  lastTime;
};

struct BasicHttpBandwidthGroup {
   uint64            limits[2];
   BasicHttpRequest  *requestList;
};

struct BasicHttpCookieJar {
   CURLSH               *curlShare;     // Use CURLSH to maintain all the cookies.
   char                 *initialCookie; // Initial cookie for the jar.
   char                 *cookieFile;    // Filename to use instead of a CURLSH
   Bool                  newSession;    // next cnxn gets a new cookie session
};

struct BasicHttpSource {
   const BasicHttpSourceOps *ops;
   void *privat;
};

struct BasicHttpRequest {
   const char                *url;
   BasicHttpMethod           httpMethod;
   BasicHttpCookieJar        *cookieJar;

   CURL                      *curl;
   struct curl_slist         *headerList;
   struct curl_slist         *recvHeaderList;
   size_t                    numRecvHeaders;

   BasicHttpSource           *body;
   Bool                      ownBody;

   DynBuf                    receiveBuf;
   BasicHttpOptions          options;
   BasicHttpOnSentProc       *onSentProc;
   BasicHttpProgressProc     *sendProgressProc;
   BasicHttpProgressProc     *recvProgressProc;
   void                      *clientData;

   BasicHttpBandwidthGroup   *bwGroup;
   BasicHttpRequest          *nextInBwGroup;
   BandwidthStatistics       statistics[2];

   uint32                    pausedMask;

   BasicHttpContentInfo      recvContentInfo;

   int                       authType;
   char                      *userNameAndPassword;
   char                      *userAgent;
   char                      *proxy;
   BasicHttpProxyType        proxyType;

   BasicHttpSslCtxProc      *sslCtxProc;
   const char               *sslCAInfo;

   CURLcode                  result;
};

#endif // _BASIC_HTTP_INT_H_
