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
 * http.c --
 */

#include "vmware.h"
#include "vm_version.h"
#include "vm_basic_types.h"
#include "vm_assert.h"
#include "hashTable.h"
#include "poll.h"
#include "util.h"
#include "str.h"
#include "strutil.h"
#include "basicHttp.h"
#include "basicHttpInt.h"
#include "requestQueue.h"

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>
#include <curl/multi.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

#define DEFAULT_MAX_OUTSTANDING_REQUESTS ((size_t)-1)
#define BASIC_HTTP_TIMEOUT_DATA ((void *)1)

/*
 * Return the lenght of the matching strings or 0 (zero) if not matching.
 * BUF may not be null terminated, so don't compare if BUF_LEN is too short.
 * STR must be null terminated and can be used in the length limited compare.
 */
#define STRNICMP_NON_TERM(STR,BUF,BUF_LEN) \
        (strlen(STR) <= BUF_LEN ? (Str_Strncasecmp(BUF, STR, strlen(STR)) == 0 ? strlen(STR) : 0) : 0)

#define HTTP_HEADER_CONTENT_LENGTH_STR "Content-Length: "
#define HTTP_HEADER_CONTENT_RANGE_STR  "Content-Range: "
#define HTTP_HEADER_CONTENT_TYPE_STR   "Content-Type: "
#define HTTP_HEADER_LAST_MODIFIED_STR  "Last-Modified: "
#define HTTP_HEADER_ACCEPT_RANGES_STR  "Accept-Ranges: "
#define HTTP_HEADER_DATE_STR           "Date: "
#define HTTP_HEADER_RANGE_BYTES_STR    "bytes "

typedef enum HttpHeaderComponent {
   HTTP_HEADER_COMP_UNKNOWN,
   HTTP_HEADER_COMP_CONTENT_LENGTH,
   HTTP_HEADER_COMP_CONTENT_RANGE,
   HTTP_HEADER_COMP_CONTENT_TYPE,
   HTTP_HEADER_COMP_LAST_MODIFIED,
   HTTP_HEADER_COMP_ACCEPT_RANGES,
   HTTP_HEADER_COMP_DATE,
   HTTP_HEADER_COMP_TERMINATOR,
   HTTP_HEADER_COMPONENTS_COUNT      /* always last */
} HttpHeaderComponent;

struct CurlSocketState;

typedef struct CurlGlobalState {
   CURLM                   *curlMulti;
   struct CurlSocketState  *socketList;
   Bool                    useGlib;
   HashTable               *requests;
   Bool                    skipRemove;

   size_t                  maxOutstandingRequests;
   RequestQueue            *pending;
} CurlGlobalState;

typedef struct CurlSocketState {
   struct CurlSocketState  *next;

   curl_socket_t           socket;
   CURL                    *curl;
   int                     action;
} CurlSocketState;

static const char *defaultUserAgent = "VMware-client";

static CurlSocketState *BasicHttpFindSocket(curl_socket_t sock);

static CurlSocketState *BasicHttpAddSocket(curl_socket_t sock,
                                           CURL *curl,
                                           int action);

static void BasicHttpRemoveSocket(curl_socket_t sock);

static void BasicHttpSetSocketState(CurlSocketState *socketState,
                                    curl_socket_t sock,
                                    CURL *curl,
                                    int action);

static void BasicHttpPollAdd(CurlSocketState *socketState);

static void BasicHttpPollRemove(CurlSocketState *socketState);

static void BasicHttpSocketPollCallback(void *clientData);

static int BasicHttpSocketCurlCallback(CURL *curl,
                                       curl_socket_t sock,
                                       int action,
                                       void *clientData,
                                       void *socketp);  // private socket

static int BasicHttpTimerCurlCallback(CURLM *multi,
                                      long timeoutMS,
                                      void *clientData);

static Bool BasicHttpStartRequest(BasicHttpRequest *request);

static size_t BasicHttpHeaderCallback(void *buffer,
                                      size_t size,
                                      size_t nmemb,
                                      void *clientData);

static size_t BasicHttpReadCallback(void *buffer,
                                    size_t size,
                                    size_t nmemb,
                                    void *clientData);

static size_t BasicHttpWriteCallback(void *buffer,
                                     size_t size,
                                     size_t nmemb,
                                     void *clientData);

static curlioerr BasicHttpIoctlCallback(CURL *handle,
                                        int cmd,
                                        void *clientData);

/* Consider these the proper ways to invoke BasicHttpSource methods. */
static ssize_t BasicHttpSourceRead(BasicHttpSource *source,
                                   void *buffer,
                                   size_t size,
                                   size_t nmemb);

static Bool BasicHttpSourceRewind(BasicHttpSource *source);

static size_t BasicHttpSourceLength(BasicHttpSource *source);

#if !(LIBCURL_VERSION_MAJOR <=7 && LIBCURL_VERSION_MINOR < 18)
static void BasicHttpResumePollCallback(void *clientData);

void BasicHttpRemoveResumePollCallback(BasicHttpRequest *request);
#endif

extern void BasicHttpBandwidthReset(BandwidthStatistics *bwStat);

extern void BasicHttpBandwidthUpdate(BandwidthStatistics *bwStat,
                                     uint64 transferredBytes);

extern void BasicHttpBandwidthSlideWindow(BandwidthStatistics *bwStat);

extern VmTimeType BasicHttpBandwidthGetDelay(BasicHttpBandwidthGroup *group,
                                             BasicHttpRequest *request,
                                             BandwidthDirection direction);

static CurlGlobalState *curlGlobalState = NULL;
static BasicHttpCookieJar *defaultCookieJar = NULL;

static PollCallbackProc *pollCallbackProc = NULL;
static PollCallbackRemoveProc *pollCallbackRemoveProc = NULL;

static Bool basicHttpTrace = 0;

/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_Init --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_Init(PollCallbackProc *pollCbProc,             // IN
               PollCallbackRemoveProc *pollCbRemoveProc) // IN
{
   return BasicHttp_InitEx(pollCbProc, pollCbRemoveProc,
                           DEFAULT_MAX_OUTSTANDING_REQUESTS);
} // BasicHttp_Init


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_InitEx --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_InitEx(PollCallbackProc *pollCbProc,              // IN
                 PollCallbackRemoveProc *pollCbRemoveProc,  // IN
                 size_t maxOutstandingRequests)             // IN
{
   Bool success = TRUE;
   CURLcode code = CURLE_OK;

   char *traceVar = getenv("VMWARE_BASICHTTP_TRACE");
   if (traceVar && strcmp(traceVar, "0")) {
      basicHttpTrace = 1;
   }

   ASSERT(pollCbProc);
   ASSERT(pollCbRemoveProc);

   if (NULL != curlGlobalState) {
      NOT_IMPLEMENTED();
   }

#if defined(_WIN32) && !defined(__MINGW32__)
   code = curl_global_init(CURL_GLOBAL_WIN32);
#else
   code = curl_global_init(CURL_GLOBAL_ALL);
#endif

   if (CURLE_OK != code) {
      success = FALSE;
      goto abort;
   }

   curlGlobalState = (CurlGlobalState *) Util_SafeCalloc(1, sizeof *curlGlobalState);
   curlGlobalState->curlMulti = curl_multi_init();
   curlGlobalState->useGlib = FALSE;
   if (NULL == curlGlobalState->curlMulti) {
      success = FALSE;
      goto abort;
   }

   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_SOCKETFUNCTION,
                     BasicHttpSocketCurlCallback);
   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_SOCKETDATA,
                     NULL);
   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_TIMERFUNCTION,
                     BasicHttpTimerCurlCallback);
   curl_multi_setopt(curlGlobalState->curlMulti,
                     CURLMOPT_TIMERDATA,
                     NULL);

   curlGlobalState->requests = HashTable_Alloc(16, HASH_INT_KEY, NULL);
   curlGlobalState->skipRemove = FALSE;
   curlGlobalState->maxOutstandingRequests = maxOutstandingRequests;
   curlGlobalState->pending = RequestQueue_New();

   pollCallbackProc = pollCbProc;
   pollCallbackRemoveProc = pollCbRemoveProc;

abort:
   if (!success) {
      free(curlGlobalState);
      curlGlobalState = NULL;
   }

   return success;
} // BasicHttp_InitEx


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetSslCtxProc --
 *
 *      Sets the ssl context function for a given request.  This
 *      callback will be called after curl initializes all ssl
 *      options, but before the request is issued.
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
BasicHttp_SetSslCtxProc(BasicHttpRequest *request,       // IN
                        BasicHttpSslCtxProc *sslCtxProc) // IN
{
   request->sslCtxProc = sslCtxProc;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpRemoveFreeRequest --
 *
 *      Remove the connection for an outstanding request and then free
 *      the request.
 *
 * Results:
 *      Always 0.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static int
BasicHttpRemoveFreeRequest(BasicHttpRequest *request, // IN
                           void *value,               // IN: Unused
                           void *clientData)          // IN: Unused
{
   BasicHttp_FreeRequest(request);

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_Shutdown --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_Shutdown(void)
{
   if (NULL != curlGlobalState) {
      curlGlobalState->skipRemove = TRUE;
      HashTable_ForEach(curlGlobalState->requests,
                        (HashTableForEachCallback)BasicHttpRemoveFreeRequest,
                        NULL);
      HashTable_Free(curlGlobalState->requests);
      RequestQueue_Free(curlGlobalState->pending);
   }

   if (NULL != defaultCookieJar) {
      BasicHttp_FreeCookieJar(defaultCookieJar);
      defaultCookieJar = NULL;
   }

   if (NULL != curlGlobalState) {
      curl_multi_cleanup(curlGlobalState->curlMulti);
      curl_global_cleanup();
      free(curlGlobalState);
      curlGlobalState = NULL;
   }

} // BasicHttp_Shutdown


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateCookieJar --
 *
 *
 * Results:
 *       BasicHttpCookieJar.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpCookieJar *
BasicHttp_CreateCookieJar(void)
{
   BasicHttpCookieJar *cookieJar;

   ASSERT(NULL != curlGlobalState);

   cookieJar = (BasicHttpCookieJar *) Util_SafeMalloc(sizeof *cookieJar);
   cookieJar->curlShare = curl_share_init();
   curl_share_setopt(cookieJar->curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
   cookieJar->initialCookie = NULL;
   cookieJar->cookieFile = NULL;
   cookieJar->newSession = FALSE;

   return cookieJar;
} // BasicHttp_CreateCookieJar


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateCookieFile --
 *
 *      Create a cookie jar based on a file.
 *
 * Results:
 *      BasicHttpCookieJar.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpCookieJar *
BasicHttp_CreateCookieFile(const char *cookieFile)
{
   BasicHttpCookieJar *cookieJar;

   cookieJar = (BasicHttpCookieJar *) Util_SafeMalloc(sizeof *cookieJar);
   cookieJar->curlShare = NULL;
   cookieJar->initialCookie = NULL;
   cookieJar->cookieFile = Util_SafeStrdup(cookieFile);
   cookieJar->newSession = FALSE;

   return cookieJar;
} // BasicHttp_CreateCookieFile


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetInitialCookie --
 *
 *       Set the initial cookie for a cookie jar. This should only be called
 *       after the cookie Jar is created, and really should only be called
 *       before any requests have been made - the results will be confusing
 *       otherwise.
 *
 *       The cookie should be in either the "Set-Cookie:" format returned
 *       by an http server or netscape/mozilla cookie file format.
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
BasicHttp_SetInitialCookie(BasicHttpCookieJar *cookieJar, // IN
                           const char *cookie)            // IN
{
   ASSERT(NULL == cookieJar->initialCookie);

   cookieJar->initialCookie = Util_SafeStrdup(cookie);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_NewCookieSession --
 *
 *      New connections using this jar will start a new cookie session
 *      - session-specific cookies will be ignored.
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
BasicHttp_NewCookieSession(BasicHttpCookieJar *cookieJar)
{
   cookieJar->newSession = TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeCookieJar --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeCookieJar(BasicHttpCookieJar *cookieJar)      // IN
{
   if (NULL == cookieJar) {
      return;
   }

   if (cookieJar->curlShare) {
      curl_share_setopt(cookieJar->curlShare, CURLSHOPT_UNSHARE, CURL_LOCK_DATA_COOKIE);
      curl_share_cleanup (cookieJar->curlShare);
   }
   free(cookieJar->initialCookie);
   free(cookieJar->cookieFile);
   free(cookieJar);
} // BasicHttp_FreeCookieJar


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSocketCurlCallback --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

int
BasicHttpSocketCurlCallback(CURL *curl,                  // IN
                            curl_socket_t sock,          // IN
                            int action,                  // IN
                            void *clientData,            // IN
                            void *socketp)               // IN
{
   CurlSocketState *socketState;

   ASSERT(NULL != curlGlobalState);

   if (CURL_POLL_REMOVE == action) {
      BasicHttpRemoveSocket(sock);
   } else if (CURL_POLL_NONE != action) {
      socketState = BasicHttpFindSocket(sock);

      if (NULL == socketState) {
         BasicHttpAddSocket(sock, curl, action);
      } else {
         BasicHttpSetSocketState(socketState, sock, curl, action);
      }
   }

   return 0;
} // BasicHttpSocketCurlCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpTimerCurlCallback --
 *
 *      Callback function that libcurl calls when it wants us to adjust the
 *      timeout callback we're running on the poll loop. Curl uses this
 *      mechanism to implement timeouts on its http connections.
 *
 * Results:
 *      Always 0.
 *
 * Side effects:
 *      Old timer callback is always cleared and a new one might be registered.
 *
 *-----------------------------------------------------------------------------
 */

int
BasicHttpTimerCurlCallback(CURLM *multi,     // IN:
                           long timeoutMS,   // IN:
                           void *clientData) // IN:
{
   pollCallbackRemoveProc(POLL_CS_MAIN,
                          0,
                          BasicHttpSocketPollCallback,
                          BASIC_HTTP_TIMEOUT_DATA,
                          POLL_REALTIME);

   if (timeoutMS >= 0) {
      VMwareStatus pollResult;
      pollResult = pollCallbackProc(POLL_CS_MAIN,
                                    0,
                                    BasicHttpSocketPollCallback,
                                    BASIC_HTTP_TIMEOUT_DATA,
                                    POLL_REALTIME,
                                    timeoutMS * 1000, /* Convert to microsec */
                                    NULL /* deviceLock */);
      if (VMWARE_STATUS_SUCCESS != pollResult) {
         ASSERT(0);
      }
   }
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpFindSocket --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

CurlSocketState *
BasicHttpFindSocket(curl_socket_t sock)                  // IN
{
   CurlSocketState *socketState = NULL;

   ASSERT(NULL != curlGlobalState);

   socketState = curlGlobalState->socketList;
   while (NULL != socketState) {
      if (sock == socketState->socket)
         break;

      socketState = socketState->next;
   }

   return socketState;
} // BasicHttpFindSocket


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpAddSocket --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

CurlSocketState *
BasicHttpAddSocket(curl_socket_t sock,                   // IN
                   CURL *curl,                           // IN
                   int action)                           // IN
{
   CurlSocketState *socketState = NULL;

   ASSERT(NULL != curlGlobalState);
   ASSERT(NULL == BasicHttpFindSocket(sock));

   socketState = (CurlSocketState *) Util_SafeCalloc(1, sizeof *socketState);
   socketState->socket = sock;
   socketState->curl = curl;
   socketState->action = action;

   BasicHttpPollAdd(socketState);

   socketState->next = curlGlobalState->socketList;
   curlGlobalState->socketList = socketState;

   return socketState;
} // BasicHttpAddSocket


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpRemoveSocket --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpRemoveSocket(curl_socket_t sock)                   // IN
{
   CurlSocketState **socketState;
   CurlSocketState *socketStateToRemove = NULL;

   ASSERT(NULL != curlGlobalState);

   socketState = &(curlGlobalState->socketList);
   while (NULL != *socketState) {
      if (sock != (*socketState)->socket) {
         socketState = &((*socketState)->next);
         continue;
      }

      socketStateToRemove = *socketState;
      *socketState = (*socketState)->next;

      BasicHttpPollRemove(socketStateToRemove);
      free(socketStateToRemove);
   }
} // BasicHttpRemoveSocket


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSetSocketState --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpSetSocketState(CurlSocketState *socketState,    // IN
                        curl_socket_t sock,              // IN
                        CURL *curl,                      // IN
                        int action)                      // IN
{
   ASSERT(NULL != socketState);

   if ((socketState->socket != sock)
         || (socketState->curl != curl)
         || (socketState->action != action)) {
      BasicHttpPollRemove(socketState);
      socketState->socket = sock;
      socketState->curl = curl;
      socketState->action = action;
      BasicHttpPollAdd(socketState);
   }
} // BasicHttpSetSocketState


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpPollAdd --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpPollAdd(CurlSocketState *socketState)           // IN
{
   VMwareStatus pollResult;

   ASSERT(NULL != socketState);

   if (CURL_POLL_IN & socketState->action) {
      pollResult = pollCallbackProc(POLL_CS_MAIN,
                                    POLL_FLAG_READ |
                                    POLL_FLAG_PERIODIC |
                                    POLL_FLAG_SOCKET,
                                    BasicHttpSocketPollCallback,
                                    socketState,
                                    POLL_DEVICE,
                                    socketState->socket,
                                    NULL /* deviceLock */);
      if (VMWARE_STATUS_SUCCESS != pollResult) {
         ASSERT(0);
      }
   }
   if (CURL_POLL_OUT & socketState->action) {
      pollResult = pollCallbackProc(POLL_CS_MAIN,
                                    POLL_FLAG_WRITE |
                                    POLL_FLAG_PERIODIC |
                                    POLL_FLAG_SOCKET,
                                    BasicHttpSocketPollCallback,
                                    socketState,
                                    POLL_DEVICE,
                                    socketState->socket,
                                    NULL /* deviceLock */);
      if (VMWARE_STATUS_SUCCESS != pollResult) {
         ASSERT(0);
      }
   }
} // BasicHttpPollAdd


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpPollRemove --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpPollRemove(CurlSocketState *socketState)        // IN
{
   ASSERT(NULL != socketState);

   if (CURL_POLL_IN & socketState->action) {
      pollCallbackRemoveProc(POLL_CS_MAIN,
                             POLL_FLAG_READ |
                             POLL_FLAG_PERIODIC |
                             POLL_FLAG_SOCKET,
                             BasicHttpSocketPollCallback,
                             socketState,
                             POLL_DEVICE);
   }
   if (CURL_POLL_OUT & socketState->action) {
      pollCallbackRemoveProc(POLL_CS_MAIN,
                             POLL_FLAG_WRITE |
                             POLL_FLAG_PERIODIC |
                             POLL_FLAG_SOCKET,
                             BasicHttpSocketPollCallback,
                             socketState,
                             POLL_DEVICE);
   }
} // BasicHttpPollRemove


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpCompleteRequestCallback --
 *
 *
 * Results:
 *       Always FALSE.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

static void
BasicHttpCompleteRequestCallback(void *clientData)          // IN
{
   BasicHttpRequest *request;
   BasicHttpResponse *response;
   BasicHttpErrorCode errorCode;
   size_t contentLength;
   char *effectiveURL;

   ASSERT(NULL != clientData);

   request = (BasicHttpRequest *) clientData;
   response = (BasicHttpResponse *) Util_SafeCalloc(1, sizeof *response);
   curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &response->responseCode);

   if (CURLE_OK == curl_easy_getinfo(request->curl, CURLINFO_EFFECTIVE_URL,
                                     &effectiveURL)) {
      response->effectiveURL = strdup(effectiveURL);
   }

   /* Map error codes. */
   switch (request->result) {
   /* 1:1 mappings. */
   case CURLE_OK:
      errorCode = BASICHTTP_ERROR_NONE;
      break;
   case CURLE_UNSUPPORTED_PROTOCOL:
      errorCode = BASICHTTP_ERROR_UNSUPPORTED_PROTOCOL;
      break;
   case CURLE_URL_MALFORMAT:
      errorCode = BASICHTTP_ERROR_URL_MALFORMAT;
      break;
   case CURLE_COULDNT_RESOLVE_PROXY:
      errorCode = BASICHTTP_ERROR_COULDNT_RESOLVE_PROXY;
      break;
   case CURLE_COULDNT_RESOLVE_HOST:
      errorCode = BASICHTTP_ERROR_COULDNT_RESOLVE_HOST;
      break;
   case CURLE_COULDNT_CONNECT:
      errorCode = BASICHTTP_ERROR_COULDNT_CONNECT;
      break;
   case CURLE_HTTP_RETURNED_ERROR:
      errorCode = BASICHTTP_ERROR_HTTP_RETURNED_ERROR;
      break;
   case CURLE_OPERATION_TIMEDOUT:
      errorCode = BASICHTTP_ERROR_OPERATION_TIMEDOUT;
      break;
   case CURLE_SSL_CONNECT_ERROR:
      errorCode = BASICHTTP_ERROR_SSL_CONNECT_ERROR;
      break;
   case CURLE_TOO_MANY_REDIRECTS:
      errorCode = BASICHTTP_ERROR_TOO_MANY_REDIRECTS;
      break;
   /* n:1 mappings */
   case CURLE_WRITE_ERROR:
   case CURLE_READ_ERROR:
   case CURLE_SEND_ERROR:
   case CURLE_RECV_ERROR:
      errorCode = BASICHTTP_ERROR_TRANSFER;
      break;
   case CURLE_SSL_ENGINE_NOTFOUND:
   case CURLE_SSL_ENGINE_SETFAILED:
   case CURLE_SSL_CERTPROBLEM:
   case CURLE_SSL_CIPHER:
   case CURLE_SSL_CACERT:
   case CURLE_SSL_ENGINE_INITFAILED:
   case CURLE_SSL_CACERT_BADFILE:
   case CURLE_SSL_SHUTDOWN_FAILED:
      errorCode = BASICHTTP_ERROR_SSL_SECURITY;
      break;
   default:
      errorCode = BASICHTTP_ERROR_GENERIC;
      break;
   }
   response->errorCode = errorCode;

   contentLength = DynBuf_GetSize(&request->receiveBuf);
   response->content = (char *) Util_SafeMalloc(contentLength + 1);
   if (contentLength > 0) {
      memcpy(response->content,
             DynBuf_Get(&request->receiveBuf),
             contentLength);
   }
   response->content[contentLength] = '\0';

   if (basicHttpTrace) {
      Log("BasicHTTP: RECEIVED RECEIVED RECEIVED RECEIVED RECEIVED RECEIVED\n");
      Log("  Content-Length: %"FMTSZ"u.\n", contentLength);
      Log("  Content: %s\n\n", response->content);
   }

   curl_easy_setopt(request->curl, CURLOPT_COOKIELIST, "FLUSH");

   (request->onSentProc)(request, response, request->clientData);

   /*
    * Don't use request after this point. Let's assume request has
    * been deleted by the callback.
    */
} // BasicHttpCompleteRequestCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpProcessCURLMulti --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Completition notifications are queued up to run asynchronously.
 *
 *-----------------------------------------------------------------------------
 */

static void
BasicHttpProcessCURLMulti(void)
{
   while (TRUE) {
      CURLMsg *msg;
      int msgsLeft;

      msg = curl_multi_info_read(curlGlobalState->curlMulti, &msgsLeft);
      if (NULL == msg) {
         break;
      }

      if (CURLMSG_DONE == msg->msg) {
         CURL *curl;
         CURLcode curlCode;
         BasicHttpRequest *request = NULL;

         /*
          * Save state as msg is unavailable after _multi_remove_handle.
          */
         curl = msg->easy_handle;
         curlCode = msg->data.result;
         curl_multi_remove_handle(curlGlobalState->curlMulti, curl);

         curl_easy_getinfo(curl, CURLINFO_PRIVATE, &request);
         if (NULL != request) {
            ASSERT(curl == request->curl);

            if (NULL != request->cookieJar) {
               curl_easy_setopt(request->curl, CURLOPT_SHARE, NULL);
            }

            /*
             * Store easy error code to handle later.
             */
            request->result = curlCode;

            /*
             * If the request is in a bandwidth group, remove from it.
             */
            if (NULL != request->bwGroup) {
               BasicHttp_RemoveRequestFromBandwidthGroup(request->bwGroup,
                                                         request);
            }

            /*
            * We are done. Invoke the callback function.
            */
            if (NULL != request->onSentProc) {
               VMwareStatus pollResult;
               pollResult = pollCallbackProc(POLL_CS_MAIN,
                                             0,
                                             BasicHttpCompleteRequestCallback,
                                             request,
                                             POLL_REALTIME,
                                             0, /* Convert to microsec */
                                             NULL /* deviceLock */);
               if (VMWARE_STATUS_SUCCESS != pollResult) {
                  ASSERT(0);
               }
            }
         }
      }
   }
} // BasicHttpProcessCURLMulti


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSocketPollCallback --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpSocketPollCallback(void *clientData)         // IN
{
   CurlSocketState *socketState;
   CURLMcode curlMErr;
   curl_socket_t socket = 0;
   Bool isTimeout;

   isTimeout = (clientData == BASIC_HTTP_TIMEOUT_DATA);
   if (isTimeout) {
      clientData = NULL;
   }

   socketState = (CurlSocketState *) clientData;
   if (socketState) {
      socket = socketState->socket;
   }

   ASSERT(NULL != curlGlobalState);
   while (TRUE) {
      int runningHandles;

      if (isTimeout) {
         curlMErr = curl_multi_socket(curlGlobalState->curlMulti,
                                      CURL_SOCKET_TIMEOUT,
                                      &runningHandles);
      } else if (socketState) {
         curlMErr = curl_multi_socket(curlGlobalState->curlMulti,
                                      socket,
                                      &runningHandles);
      } else {
         /*
          * Before calling curl_multi_socket_all, we need to process all
          * the pending curl multi results. Otherwise, one curl connection
          * could be assigned to more than one curl easy handles.
          *
          * There's a bug(?) in cUrl implementation up to 7.16.0 in that
          * the connection is returned to pool as soon as the request
          * becomes COMPLETED. However, it's not removed from easy multi
          * handle until curl_multi_remove_handle is called. If curl_multi
          * _socket_all is called when this happens, the same connection
          * could be	assigned to 2 curl easy handles which would cause mess
          * later on.
          */
         BasicHttpProcessCURLMulti();
         curlMErr = curl_multi_socket_all(curlGlobalState->curlMulti,
                                          &runningHandles);
      }

      if (CURLM_CALL_MULTI_PERFORM != curlMErr) {
         /* 
          * A CURL internal bug cause returning CURLM_BAD_SOCKET before 
          * a curl handle is able to transit to the final complete state.
            
          * It is timing related and the chance is exactly 1%. When this
          * happens, we need to redrive the curl handle using the 
          * curl_multi_socket_all API. Hence we set socketState to NULL

          * Note redrive using curl_multi_socket will not work as it could
          * not find the removed socket in hash and returns CURLM_BAD_SOCKET
          * before get a chance to finish the final state transition.
          */
         if (CURLM_BAD_SOCKET == curlMErr) {
            socketState = NULL;
            continue;
         }

         ASSERT( CURLM_OK == curlMErr );

         break;
      }
   }

   BasicHttpProcessCURLMulti();

   while (curlGlobalState->pending->size > 0 &&
          HashTable_GetNumElements(curlGlobalState->requests) <
                                   curlGlobalState->maxOutstandingRequests) {
      BasicHttpRequest *request = RequestQueue_PopHead(curlGlobalState->pending);
      BasicHttpStartRequest(request);
   }
} // BasicHttpSocketPollCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateRequest --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpRequest *
BasicHttp_CreateRequest(const char *url,                 // IN
                        BasicHttpMethod httpMethod,      // IN
                        BasicHttpCookieJar *cookieJar,   // IN
                        const char *header,              // IN
                        const char *body)                // IN
{
   return BasicHttp_CreateRequestWithSSL(url, httpMethod, cookieJar, header,
                                         body, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateRequestWithSSL --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpRequest *
BasicHttp_CreateRequestWithSSL(const char *url,          // IN
                        BasicHttpMethod httpMethod,      // IN
                        BasicHttpCookieJar *cookieJar,   // IN
                        const char *header,              // IN
                        const char *body,                // IN
                        const char *sslCAInfo) // IN: SSL Root Certiifcate File
{
   BasicHttpSource *sourceBody = BasicHttp_AllocStringSource(body);
   BasicHttpRequest *ret = NULL;

   ret = BasicHttp_CreateRequestEx(url, httpMethod, cookieJar, header,
                                   sourceBody, sslCAInfo);
   /* Need BasicHttp_FreeRequest to free sourceBody. */
   ret->ownBody = TRUE;

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CreateRequestEx --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpRequest *
BasicHttp_CreateRequestEx(const char *url,               // IN
                        BasicHttpMethod httpMethod,      // IN
                        BasicHttpCookieJar *cookieJar,   // IN
                        const char *header,              // IN
                        BasicHttpSource *body,           // IN
                        const char *sslCAInfo) // IN: SSL Root Certiifcate
                                               // File. If NULL, don't verify
                                               // peer certificate.
{
   BasicHttpRequest *request = NULL;

   if ((NULL == url)
      || (httpMethod < BASICHTTP_METHOD_GET)
      || (httpMethod > BASICHTTP_METHOD_HEAD)) {
      goto abort;
   }

   if (BASICHTTP_DEFAULT_COOKIEJAR == cookieJar) {
      if (NULL == defaultCookieJar) {
         defaultCookieJar = BasicHttp_CreateCookieJar();
      }
      cookieJar = defaultCookieJar;
   }

   request = (BasicHttpRequest *) Util_SafeCalloc(1, sizeof *request);
   request->url = Util_SafeStrdup(url);
   request->httpMethod = httpMethod;
   request->cookieJar = cookieJar;
   BasicHttp_AppendRequestHeader(request, header);
   request->body = body;
   DynBuf_Init(&request->receiveBuf);
   request->recvContentInfo.totalSize = BASICHTTP_UNKNOWN_SIZE;
   request->recvContentInfo.expectedLength = BASICHTTP_UNKNOWN_SIZE;
   request->recvContentInfo.rangeStart = 0;
   request->recvContentInfo.rangeEnd = BASICHTTP_UNKNOWN_SIZE;
   request->pausedMask = 0;
   request->authType = BASICHTTP_AUTHENTICATION_NONE;
   request->userNameAndPassword = NULL;
   request->userAgent = NULL;
   request->proxy = NULL;
   request->proxyType = BASICHTTP_PROXY_NONE;
   if (sslCAInfo) {
      request->sslCAInfo = Util_SafeStrdup(sslCAInfo);
   } else {
      request->sslCAInfo = NULL;
   }

abort:
   return request;
} // BasicHttp_CreateRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AppendRequestHeader --
 *
 *       Append to the request header.
 *
 * Results:
 *       Boolean - TRUE on success, FALSE on failure.
 *
 * Side effects:
 *       On success, the header list returned will contain the header passed
 *       in, in addition to any previously appended headers.
 *       On failure, the entire header list will be retained, but the request
 *       will not succeed as the caller intends, and so, should be aborted.
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_AppendRequestHeader(BasicHttpRequest *request,    // IN
                              const char *header)           // IN
{
   struct curl_slist *newList = NULL;

   if (!header || !request) {
      goto exit;
   }

   newList = curl_slist_append(request->headerList, header);

   /*
    * If the above call succeeded, save the result header list.
    * If the above call failed, the previous header list is unchanged.
    */
   if (newList) {
      request->headerList = newList;
   } else {
      Log("BasicHTTP: AppendRequestHeader failed to append to the request header. Insufficient memory.\n");
   }

exit:
   /*
    * Return the result. The result will be TRUE, successful, only if the
    * parameters passed in were valid and the new header was successfully
    * appended to the header list.
    */
   return (newList != NULL);
} // BasicHttp_AppendRequestHeader


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AppendRangeRequestHeader --
 *
 *       Append "Range: bytes=<start>-<end>\r\n" to the request header.
 *
 * Results:
 *       Boolean - TRUE on success, FALSE on failure.
 *
 * Side effects:
 *       This will affect the range of the content processed by the request.
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_AppendRangeRequestHeader(BasicHttpRequest *request,     // IN
                                   int64 start,                   // IN
                                   int64 size)                    // IN (OPT)
{
   char temp[65];
   int64 end = start + size - 1;
   int shouldAlwaysWork;
   Bool rslt = FALSE;

   if (size > 0) {
      shouldAlwaysWork = Str_Snprintf(temp,
                                      64,
                                      "Range:bytes=%"FMT64"d-%"FMT64"d",
                                      start,
                                      end);
   } else {
      shouldAlwaysWork = Str_Snprintf(temp,
                                      64,
                                      "Range:bytes=%"FMT64"d-",
                                      start);
   }

   ASSERT(shouldAlwaysWork >= 0);
   if (shouldAlwaysWork < 0) {
      Log("BasicHTTP: Formatting Range request header failed. Not expected.\n");
      goto exit;
   }

   rslt = BasicHttp_AppendRequestHeader(request, temp);
   if (!rslt) {
      Log("BasicHTTP: AppendRequestHeader failed. Not expected.\n");
   }

exit:
   return rslt;
} // BasicHttp_AppendRequestHeader


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetRequestNameAndPassword --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_SetRequestNameAndPassword(BasicHttpRequest *request,     // IN
                                    int authenticationType,        // IN
                                    const char *userName,          // IN
                                    const char *userPassword)      // IN
{
   if ((NULL == request)
         || (authenticationType < BASICHTTP_AUTHENTICATION_NONE)) {
      ASSERT(0);
      return;
   }

   request->authType = authenticationType;

   free(request->userNameAndPassword);
   request->userNameAndPassword = NULL;
   if ((NULL != userName) && (NULL != userPassword)) {
      size_t strLength = strlen(userName) + strlen(userPassword) + 2;
      request->userNameAndPassword = (char *) Util_SafeCalloc(1, strLength);
      snprintf(request->userNameAndPassword,
                  strLength,
                  "%s:%s",
                  userName,
                  userPassword);
   }
} // BasicHttp_SetRequestNameAndPassword


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetUserAgent --
 *
 *       Sets the userAgent string for the HTTP request.
 *
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
BasicHttp_SetUserAgent(BasicHttpRequest *request,     // IN
                       const char *userAgent)         // IN: New UserAgent string
{
   ASSERT(request);
   if (NULL == request) {
      return;
   }

   free(request->userAgent);
   request->userAgent = Util_SafeStrdup(userAgent);
} // BasicHttp_SetUserAgent


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SetProxy --
 *
 *      Sets the proxy string for the HTTP request.
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
BasicHttp_SetProxy(BasicHttpRequest *request,    // IN
                   const char *proxy,            // IN
                   BasicHttpProxyType proxyType) // IN
{
    ASSERT(request);
    if (proxyType != BASICHTTP_PROXY_NONE) {
        ASSERT(proxy != NULL);
    }
    if (NULL == request) {
        return;
    }

    free(request->proxy);
    request->proxy = Util_SafeStrdup(proxy);
    request->proxyType = proxyType;
} // BasicHttp_SetProxy


/*
 *-----------------------------------------------------------------------------
 *
 *  BasicHttp_SetConnectTimeout --
 *
 *     Sets the maximum time in seconds to allow connecting to the server to take.
 *     Once the connection has been made, this option is of no use.
 *     Set to 0 to disable connection timeout.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_SetConnectTimeout(BasicHttpRequest *request,      // IN:
                            unsigned long seconds)          // IN:
{
   if (0 != seconds) {
      NOT_IMPLEMENTED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSslCtxCb --
 *
 *      Callback from curl, after all of its ssl options have been
 *      set, before the connection has been made.  Pass the sslctx on
 *      to the caller, if it has set a callback.
 *
 * Results:
 *      OK
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static CURLcode
BasicHttpSslCtxCb(CURL *curl,   // IN
                  void *sslctx, // IN
                  void *parm)   // IN
{
    BasicHttpRequest *request = parm;
    if (request->sslCtxProc) {
        request->sslCtxProc(request, sslctx, request->clientData);
    }
    return CURLE_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpStartRequest --
 *
 *
 * Results:
 *       A Boolean indicating whether the request has been successfully started
 *       or not.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

static Bool
BasicHttpStartRequest(BasicHttpRequest *request)            // IN
{
   Bool success = TRUE;
   CURLMcode curlMErr;

   request->curl = curl_easy_init();
   if (NULL == request->curl) {
      success = FALSE;
      goto abort;
   }

   ASSERT(NULL != request->url);
   curl_easy_setopt(request->curl, CURLOPT_URL, request->url);
   if (!request->sslCAInfo) {
      /* Do not verify peer.*/
      curl_easy_setopt(request->curl, CURLOPT_SSL_VERIFYPEER, (long) 0);
   } else {
      /* Do verify server certificate using certificate(s) from path. */
      curl_easy_setopt(request->curl, CURLOPT_SSL_VERIFYPEER, (long) 1);
      curl_easy_setopt(request->curl, CURLOPT_CAINFO, request->sslCAInfo);
   }

   curl_easy_setopt(request->curl, CURLOPT_SSL_VERIFYHOST, (long) 0);
   if (request->sslCtxProc) {
       curl_easy_setopt(request->curl, CURLOPT_SSL_CTX_FUNCTION,
                        BasicHttpSslCtxCb);
       curl_easy_setopt(request->curl, CURLOPT_SSL_CTX_DATA, request);
   }
   curl_easy_setopt(request->curl, CURLOPT_FOLLOWLOCATION, (long) 1);
#ifdef CURLOPT_POST301
   curl_easy_setopt(request->curl, CURLOPT_POST301, (long) 1);
#endif
   curl_easy_setopt(request->curl, CURLOPT_NOSIGNAL, (long) 1);
   curl_easy_setopt(request->curl, CURLOPT_CONNECTTIMEOUT, (long) 60);
#ifdef _WIN32
   /*
    * Set a dummy random file, this is pretty much a no-op in libcurl
    * however, it triggers the libcurl to check if the random seed has enough
    * entrophy and skips a lengthy rand_screen() if that is the case.
    */
   curl_easy_setopt(request->curl, CURLOPT_RANDOM_FILE, "");
#endif

   if ((BASICHTTP_AUTHENTICATION_NONE != request->authType)
         && (NULL != request->userNameAndPassword)) {
      curl_easy_setopt(request->curl, CURLOPT_USERPWD, request->userNameAndPassword);
      switch(request->authType) {
      case BASICHTTP_AUTHENTICATION_BASIC:
         curl_easy_setopt(request->curl, CURLOPT_HTTPAUTH, (long) CURLAUTH_BASIC);
         break;
      case BASICHTTP_AUTHENTICATION_DIGEST:
         curl_easy_setopt(request->curl, CURLOPT_HTTPAUTH, (long) CURLAUTH_DIGEST);
         break;
      case BASICHTTP_AUTHENTICATION_NTLM:
         curl_easy_setopt(request->curl, CURLOPT_PROXYAUTH, (long) CURLAUTH_NTLM);
         break;
      case BASICHTTP_AUTHENTICATION_ANY:
      default:
         curl_easy_setopt(request->curl, CURLOPT_PROXYAUTH, (long) CURLAUTH_ANY);
         break;
      }
   } // Set the username/password.

   curl_easy_setopt(request->curl, CURLOPT_USERAGENT,
                    request->userAgent ? request->userAgent : defaultUserAgent);

   if (NULL == request->cookieJar) {
      curl_easy_setopt(request->curl, CURLOPT_COOKIEFILE, "");
   } else {
      if (request->cookieJar->newSession) {
         curl_easy_setopt(request->curl, CURLOPT_COOKIESESSION, (long)1);
         request->cookieJar->newSession = FALSE;
      }
      if (NULL != request->cookieJar->curlShare) {
         curl_easy_setopt(request->curl, CURLOPT_SHARE, request->cookieJar->curlShare);
         curl_easy_setopt(request->curl, CURLOPT_COOKIEFILE, "");
      } else if (NULL != request->cookieJar->cookieFile) {
         curl_easy_setopt(request->curl, CURLOPT_COOKIEFILE, request->cookieJar->cookieFile);
         curl_easy_setopt(request->curl, CURLOPT_COOKIEJAR, request->cookieJar->cookieFile);
      } else {
         NOT_REACHED();
      }

      /*
       * Curl can be so insane sometimes. You can share a cookie jar but you can't put
       * anything into it until you have an actual easy handle. So we have to store the
       * initial cookie until the first handle comes along, and then set it then.
       */
      if (NULL != request->cookieJar->initialCookie) {
         curl_easy_setopt(request->curl,
                          CURLOPT_COOKIELIST,
                          request->cookieJar->initialCookie);
         free(request->cookieJar->initialCookie);
         request->cookieJar->initialCookie = NULL;
      }
   }

   switch (request->proxyType) {
   case BASICHTTP_PROXY_NONE:
       break;
   case BASICHTTP_PROXY_HTTP:
       curl_easy_setopt(request->curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
       break;
   case BASICHTTP_PROXY_SOCKS4:
       curl_easy_setopt(request->curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
       break;
   default:
       NOT_IMPLEMENTED();
       break;
   }
   if (request->proxy != NULL) {
       curl_easy_setopt(request->curl, CURLOPT_PROXY, request->proxy);
   }

   if (basicHttpTrace) {
   curl_easy_setopt(request->curl, CURLOPT_VERBOSE, (long) 1);
   }

   switch (request->httpMethod) {
      case BASICHTTP_METHOD_GET:
         curl_easy_setopt(request->curl, CURLOPT_HTTPGET, (long) 1);
         break;

      case BASICHTTP_METHOD_POST:
         curl_easy_setopt(request->curl, CURLOPT_POST, (long) 1);
         /* Refer to bug 376040 before changing this to CURLOPT_POSTFIELDSIZE_LARGE. */
         curl_easy_setopt(request->curl, CURLOPT_POSTFIELDSIZE,
                          (long) BasicHttpSourceLength(request->body));
         break;

      case BASICHTTP_METHOD_HEAD:
      default:
         // TODO: add later
         success = FALSE;
         goto abort;
   }

   if (NULL != request->headerList) {
      curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headerList);
   }

   curl_easy_setopt(request->curl, CURLOPT_HEADERFUNCTION, BasicHttpHeaderCallback);
   curl_easy_setopt(request->curl, CURLOPT_WRITEHEADER, request);

   curl_easy_setopt(request->curl, CURLOPT_READFUNCTION, BasicHttpReadCallback);
   curl_easy_setopt(request->curl, CURLOPT_READDATA, request);

   curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, BasicHttpWriteCallback);
   curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, request);

   curl_easy_setopt(request->curl, CURLOPT_IOCTLFUNCTION, BasicHttpIoctlCallback);
   curl_easy_setopt(request->curl, CURLOPT_IOCTLDATA, request);

   curl_easy_setopt(request->curl, CURLOPT_PRIVATE, request);

   HashTable_Insert(curlGlobalState->requests, (void *)request, NULL);
   curlMErr = curl_multi_add_handle(curlGlobalState->curlMulti, request->curl);
   if (CURLM_OK != curlMErr) {
      success = FALSE;
      goto abort;
   }


   if (basicHttpTrace) {
      Log("BasicHTTP: SENDING SENDING SENDING SENDING SENDING SENDING\n");
      if (request->url)
         Log("  URL: %s\n", request->url);
   }

   BasicHttpSocketPollCallback(NULL);

 abort:
   return success;
} // BasicHttpStartRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SendRequestEx --
 *
 *       The callback function onSentProc will be responsible for
 *       deleteing request and response.
 *
 * Results:
 *       Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_SendRequestEx(BasicHttpRequest *request,                   // IN
                        BasicHttpOptions options,                    // IN
                        BasicHttpProgressProc *sendProgressProc,     // IN:OPT
                        BasicHttpProgressProc *recvProgressProc,     // IN:OPT
                        BasicHttpOnSentProc *onSentProc,             // IN
                        void *clientData)                            // IN
{
   Bool success = TRUE;

   if ((NULL == request) || (NULL == onSentProc)) {
      success = FALSE;
      goto abort;
   }

   ASSERT(NULL != curlGlobalState);
   ASSERT(NULL == request->curl);

   request->options = options;
   request->sendProgressProc = sendProgressProc;
   request->recvProgressProc = recvProgressProc;
   request->onSentProc = onSentProc;
   request->clientData = clientData;

   if (HashTable_GetNumElements(curlGlobalState->requests) >=
       curlGlobalState->maxOutstandingRequests) {
      // Queue up request.
      RequestQueue_PushTail(curlGlobalState->pending, request);
   } else {
      success = BasicHttpStartRequest(request);
   }

abort:
   return success;
} // BasicHttp_SendRequestWithProgress


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_SendRequest --
 *
 *       The callback function onSentProc will be responsible for
 *       deleteing request and response.
 *
 * Results:
 *       Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_SendRequest(BasicHttpRequest *request,                  // IN
                      BasicHttpOnSentProc *onSentProc,            // IN
                      void *clientData)                           // IN (OPT)
{
   return BasicHttp_SendRequestEx(request,
                                  0,
                                  NULL,
                                  NULL,
                                  onSentProc,
                                  clientData);
} // BasicHttp_SendRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpPauseRequest --
 *
 *       Internal pause or resume the request.
 *
 * Results:
 *       Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *       The desired callback function will not be called until unpaused.
 *
 *-----------------------------------------------------------------------------
 */

#if !(LIBCURL_VERSION_MAJOR <= 7 && LIBCURL_VERSION_MINOR < 18)
static Bool
BasicHttpPauseRequest(BasicHttpRequest *request, // IN
                      int mask)                  // IN
{
   CURLcode rslt = CURLE_OK;
   if (NULL == request) {
      return FALSE;
   }

   /*
    * Remove the possible scheduled callback for bandwidth control.
    */
   BasicHttpRemoveResumePollCallback(request);

   if (NULL != request->curl) {
      rslt = curl_easy_pause(request->curl, mask);
      if (rslt == CURLE_OK) {
         request->pausedMask = mask;
         BasicHttpSocketPollCallback(NULL);
      }
   }
   return (rslt == CURLE_OK);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_PauseRecvRequest --
 *
 *       Pause or resume the request.
 *
 * Results:
 *       Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *       The write callback function will not be called until unpaused.
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_PauseRecvRequest(BasicHttpRequest *request, // IN
                           Bool pause)                // IN
{
#if LIBCURL_VERSION_MAJOR <= 7 && LIBCURL_VERSION_MINOR < 18
   return FALSE;
#else
   if (NULL != request && NULL != request->curl) {
      int mask = pause ? request->pausedMask | CURLPAUSE_RECV :
         request->pausedMask & (~CURLPAUSE_RECV);
      return BasicHttpPauseRequest(request, mask);
   }
   return FALSE;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_PauseSendRequest --
 *
 *       Pause or resume the send request.
 *
 * Results:
 *       Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *       The read callback function will not be called until unpaused.
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttp_PauseSendRequest(BasicHttpRequest *request, // IN
                           Bool pause)                // IN
{
#if LIBCURL_VERSION_MAJOR <= 7 && LIBCURL_VERSION_MINOR < 18
   return FALSE;
#else
   if (NULL != request && NULL != request->curl) {
      int mask = pause ? request->pausedMask | CURLPAUSE_SEND :
         request->pausedMask & (~CURLPAUSE_SEND);
      return BasicHttpPauseRequest(request, mask);
   }
   return FALSE;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_CancelRequest --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_CancelRequest(BasicHttpRequest *request)       // IN
{
   if (NULL == request) {
      return;
   }

   ASSERT(NULL != curlGlobalState);

   if (NULL != request->curl) {
      curl_multi_remove_handle(curlGlobalState->curlMulti, request->curl);
   }

   if (NULL != request->bwGroup) {
      BasicHttp_RemoveRequestFromBandwidthGroup(request->bwGroup, request);
   }
} // BasicHttp_CancelRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpParseContentRange --
 *
 *       Internal function that parses the Content-Range response header.
 *
 * Results:
 *       Boolean - TRUE on success, FALSE on failure.
 *       On success, the output values are set to the values in the header.
 *       Note that the size output can be BASICHTTP_UNKNOWN_SIZE (-1).
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

static Bool
BasicHttpParseContentRange(char * headerCompValue,       // IN
                           size_t sizeHeaderCompValue,   // IN
                           int64 *start,                 // OUT
                           int64 *end,                   // OUT
                           int64 *size)                  // OUT
{
   Bool rslt = FALSE;
   int64 contentRangeStart = BASICHTTP_UNKNOWN_SIZE;
   int64 contentRangeEnd = BASICHTTP_UNKNOWN_SIZE;
   int64 totalContentSize = BASICHTTP_UNKNOWN_SIZE;
   size_t lenBytesMatch = 0;
   unsigned int index = 0;

   ASSERT(headerCompValue && start && end && size);
   if (!headerCompValue || !start || !end || !size) {
      goto exit;
   }

   /*
    * Parse Content-Range: bytes <digits>-<digits>[/<digits>]
    * "Content-Range: " has already been parsed. First look for the 
    * units value of "bytes ".
    */
   if ((lenBytesMatch = STRNICMP_NON_TERM(HTTP_HEADER_RANGE_BYTES_STR,
                                          headerCompValue, sizeHeaderCompValue))
       == 0) {
      Log("BasicHTTP: Error parsing Content-Range. Range-Type bytes expected.\n");
      /*
       * Bail with rslt of FALSE, defaulted to FALSE above. (FALSE is a failure.)
       */
      goto exit;
   }

   /*
    * Now look for the start of the range. (Digits before the '-' separator.)
    */
   headerCompValue += lenBytesMatch;
   if (!StrUtil_GetNextInt64Token(&contentRangeStart, &index, headerCompValue, "-") ||
       headerCompValue[index] != '-') {
      Log("BasicHTTP: Error parsing Content-Range. <digits>- expected.\n");
      /*
       *  Bail with rslt of FALSE, defaulted to FALSE above. (FALSE is a failure.)
       */
      goto exit;
   }

   /*
    * Now look for the end of the range. (Digits after the '-' separator but before
    * the optional '/' seperator.)
    */
   index++;
   if (!StrUtil_GetNextInt64Token(&contentRangeEnd, &index, headerCompValue, "/")) {
      Log("BasicHTTP: Error parsing Content-Range. <digits>-<digits> expected.\n");
      /*
       *  Bail with rslt of FALSE, defaulted to FALSE above. (FALSE is a failure.)
       */
      goto exit;
   }

   /*
    * If there was a '/' separator, look for the object size.
    */
   if (headerCompValue[index] == '/') {
      index++;
      if (!StrUtil_StrToInt64(&totalContentSize, headerCompValue + index)) {
         Log("BasicHTTP: Error parsing Content-Range. <digits>-<digits>/<digits> expected.\n");
         /*
          *  Bail with rslt of FALSE, defaulted to FALSE above. (FALSE is a failure.)
          */
         goto exit;
      }
   }

   *start = contentRangeStart;
   *end = contentRangeEnd;
   *size = totalContentSize;
   rslt = TRUE;

exit:
   return rslt;
} // BasicHttpParseContentRange


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpHeaderCallback --
 *
 *        Process header lines. Called one header line at a time.
 *        Note: Header lines passed in are not null terminated.
 *        Also: Header lines passed in have a 0x0d, 0x0a, at the end.
 *
 * Results:
 *       Size of header data processed is returned.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpHeaderCallback(void *buffer,                     // IN
                        size_t size,                      // IN
                        size_t nmemb,                     // IN
                        void *clientData)                 // IN
{
   BasicHttpRequest *request = (BasicHttpRequest *) clientData;
   char * headerData = (char *)buffer;
   size_t bufferSize = size * nmemb;
   size_t rslt = 0;
   HttpHeaderComponent headerComponent = HTTP_HEADER_COMP_UNKNOWN;
   size_t lenHdrMatch = 0;
   char * headerCompValue = NULL;
   size_t sizeHeaderCompValue = 0;

   ASSERT(NULL != request);

   if (bufferSize == 0) {
      Log("BasicHTTP: Header callback called with empty buffer. Not expected. No harm. Nothing to do.\n");
      /*
       * Bail with rslt of 0, defaulted to 0 above. (0 is a failure.)
       */
      goto exit;
   }

   /*
    * Convert the various header component strings into a switchable enum value.
    * Additional side effect is lenHdrMatch should be set to the length
    * of the leading header component string where the data for the component follows.
    * Keep in mind, the header data isn't guaranteed to be null terminated.
    */
   if ((lenHdrMatch = STRNICMP_NON_TERM(HTTP_HEADER_CONTENT_LENGTH_STR, headerData, bufferSize)) > 0) {
      headerComponent = HTTP_HEADER_COMP_CONTENT_LENGTH;
   } else if ((lenHdrMatch = STRNICMP_NON_TERM(HTTP_HEADER_CONTENT_RANGE_STR, headerData, bufferSize)) > 0) {
      headerComponent = HTTP_HEADER_COMP_CONTENT_RANGE;
   } else if ((lenHdrMatch = STRNICMP_NON_TERM(HTTP_HEADER_CONTENT_TYPE_STR, headerData, bufferSize)) > 0) {
      headerComponent = HTTP_HEADER_COMP_CONTENT_TYPE;
   } else if ((lenHdrMatch = STRNICMP_NON_TERM(HTTP_HEADER_LAST_MODIFIED_STR, headerData, bufferSize)) > 0) {
      headerComponent = HTTP_HEADER_COMP_LAST_MODIFIED;
   } else if ((lenHdrMatch = STRNICMP_NON_TERM(HTTP_HEADER_ACCEPT_RANGES_STR, headerData, bufferSize)) > 0) {
      headerComponent = HTTP_HEADER_COMP_ACCEPT_RANGES;
   } else if ((lenHdrMatch = STRNICMP_NON_TERM(HTTP_HEADER_DATE_STR, headerData, bufferSize)) > 0) {
      headerComponent = HTTP_HEADER_COMP_DATE;
   } else if (bufferSize == 2 && headerData[0] == 0x0d && headerData[1] == 0x0a) {
      headerComponent = HTTP_HEADER_COMP_TERMINATOR;
   }

   /*
    * Put header component value data into "clean" usable shape.
    * Determine the location and size of the value data and trim traling
    * non-text. There should always be a trailing CRLF - 0x0d, 0x0a.
    * There is no null terminator guaranteed, so add one over the CRLF
    * instead of making a copy of the data in a dynamically sized
    * buffer. We'll use the fact that the incoming buffer is writable
    * and there should be a trailing CRLF that we can overwrite.
    * (libCurl is "giving" us this data to handle. It's writable by us.)
    */
   if (headerComponent == HTTP_HEADER_COMP_UNKNOWN) {
      /*
       * Null-terminate unknown headers but do not attempt to parse.
       */
      if (bufferSize > 2) {
         headerData[bufferSize - 2] = 0x00;
      } else {
         Log("BasicHTTP: Unexpected error null-terminating unknown header.\n");
         /*
          * Bail with rslt of 0, defaulted to 0 above. (0 is a failure.)
          */
         goto exit;
      }
   } else if (headerComponent != HTTP_HEADER_COMP_TERMINATOR) {
      ASSERT(bufferSize > lenHdrMatch);
      headerCompValue = headerData + lenHdrMatch;
      sizeHeaderCompValue = bufferSize - lenHdrMatch;
      if (sizeHeaderCompValue > 2 &&
          headerCompValue[sizeHeaderCompValue - 1] == 0x0a &&
          headerCompValue[sizeHeaderCompValue - 2] == 0x0d) {
         sizeHeaderCompValue -= 2;
         headerCompValue[sizeHeaderCompValue] = 0;
      } else {
         Log("BasicHTTP: Unexpected error parsing header.\n");
         /*
          * Bail with rslt of 0, defaulted to 0 above. (0 is a failure.)
          */
         goto exit;
      }
   }

   /*
    * Handle the various header components.
    */
   switch (headerComponent)
   {
   case HTTP_HEADER_COMP_CONTENT_LENGTH:
      {
         int64 contentLength = BASICHTTP_UNKNOWN_SIZE;
         if (sizeHeaderCompValue == 0 || headerCompValue == NULL ||
             !StrUtil_StrToInt64(&contentLength, headerCompValue)) {
            Log("BasicHTTP: Unexpected error parsing Content-Length.\n");
            /*
             * Bail with rslt of 0, defaulted to 0 above. (0 is a failure.)
             */
            break;
         }
         request->recvContentInfo.expectedLength = contentLength;
      }

      /*
       * Exit with rslt of bufferSize. (This is the normal success result.)
       */
      rslt = bufferSize;
      break;

   case HTTP_HEADER_COMP_CONTENT_RANGE:
      {
         int64 contentRangeStart = BASICHTTP_UNKNOWN_SIZE;
         int64 contentRangeEnd = BASICHTTP_UNKNOWN_SIZE;
         int64 totalContentSize = BASICHTTP_UNKNOWN_SIZE;

         if (!BasicHttpParseContentRange(headerCompValue,
                                         sizeHeaderCompValue,
                                         &contentRangeStart,
                                         &contentRangeEnd,
                                         &totalContentSize)) {
            Log("BasicHTTP: Parsing Content-Range header failed.\n");
            /*
             * Bail with rslt of 0, defaulted to 0 above. (0 is a failure.)
             */
            break;
         }

         request->recvContentInfo.totalSize = totalContentSize;
         request->recvContentInfo.rangeStart = contentRangeStart;
         request->recvContentInfo.rangeEnd = contentRangeEnd;
      }

      /*
       * Exit with rslt of bufferSize. (This is the normal success result.)
       */
      rslt = bufferSize;
      break;

   case HTTP_HEADER_COMP_UNKNOWN:
   {
      struct curl_slist *newList = NULL;
      newList = curl_slist_append(request->recvHeaderList, headerData);
      /* Keep header list unchanged if failed. */
      if (newList) {
         request->recvHeaderList = newList;
         request->numRecvHeaders++;
      } else {
         Log("BasicHTTP: failure to append to the receive header. Insufficient "
             "memory.\n");
      }
   }
      /* Fall through */
   case HTTP_HEADER_COMP_CONTENT_TYPE:
   case HTTP_HEADER_COMP_LAST_MODIFIED:
   case HTTP_HEADER_COMP_ACCEPT_RANGES:
   case HTTP_HEADER_COMP_DATE:
   case HTTP_HEADER_COMP_TERMINATOR:
      /*
       * Just ignore header components we don't care about.
       * Exit with rslt of bufferSize. This is the normal successful result.
       */
      rslt = bufferSize;
      break;

   default:
      /*
       * The above unknown case should be the fallback default. We should never get here.
       * All possible enum headerComponent cases should be handled above.
       * This is just a defensive paranoia check for a bug.
       */
      ASSERT(TRUE);
      /*
       * Bail with rslt of 0, defaulted to 0 above. (0 is a failure.)
       */
      break;
   }

exit:
   return rslt;
} // BasicHttpHeaderCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpReadCallback --
 *
 *
 * Results:
 *       The amount of data read is returned.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpReadCallback(void *buffer,                      // IN, OUT
                      size_t size,                       // IN
                      size_t nmemb,                      // IN
                      void *clientData)                  // IN
{
   static const size_t readError = (size_t) -1;
   BasicHttpRequest *request = (BasicHttpRequest*) clientData;
   size_t ret;
#if !(LIBCURL_VERSION_MAJOR <=7 && LIBCURL_VERSION_MINOR < 18)
   BandwidthStatistics *bwStatistics;
   double uploaded;
#endif

   ASSERT(NULL != request);

#if !(LIBCURL_VERSION_MAJOR <=7 && LIBCURL_VERSION_MINOR < 18)
   bwStatistics = &(request->statistics[BASICHTTP_UPLOAD]);

   curl_easy_getinfo(request->curl, CURLINFO_SIZE_UPLOAD, &uploaded);
   BasicHttpBandwidthUpdate(bwStatistics, (uint64) uploaded);

   if (request->bwGroup) {
      VmTimeType delay;

      delay = BasicHttpBandwidthGetDelay(request->bwGroup,
                                         request,
                                         BASICHTTP_UPLOAD);

      if (delay > 0) {
         VMwareStatus pollResult;
         pollResult = pollCallbackProc(POLL_CS_MAIN,
                                       0,
                                       BasicHttpResumePollCallback,
                                       request,
                                       POLL_REALTIME,
                                       delay, /* in microsec */
                                       NULL /* deviceLock */);
         if (VMWARE_STATUS_SUCCESS != pollResult) {
            ASSERT(0);
         }

         /*
          * Don't set request->pausedMask here. BasicHttpResumePollCallback
          * will un-pause the transfer after delay timeout.
          */
         ret = CURL_READFUNC_PAUSE;
         goto exit;
      }
   }

   BasicHttpBandwidthSlideWindow(bwStatistics);

   if (NULL != request->sendProgressProc) {
      Bool success;

      success = (request->sendProgressProc)(request,
                                            0,
                                            NULL,
                                            bwStatistics->transferredBytes,
                                            bwStatistics->windowedRate,
                                            request->clientData);
      if (!success) {
         /*
          * Pause the transfer. The transfer must be resumed by calling
          * BasicHttp_PauseSendRequest().
          */
         request->pausedMask |= CURLPAUSE_SEND;
         ret = CURL_READFUNC_PAUSE;
         goto exit;
      }
   }
#endif

   ret = (size_t) BasicHttpSourceRead(request->body, buffer, size, nmemb);

   if (readError == ret) {
      ret = CURL_READFUNC_ABORT;
      goto exit;
   }

exit:
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpWriteCallback --
 *
 *
 * Results:
 *       The amount of data written is returned.
 *
 * Side effects:
 *       Depending on the results returned by the external progress callback,
 *       the transfer could be paused or canceled.
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpWriteCallback(void *buffer,                     // IN
                       size_t size,                      // IN
                       size_t nmemb,                     // IN
                       void *clientData)                 // IN
{
   BasicHttpRequest *request = (BasicHttpRequest *) clientData;
   size_t bufferSize = size * nmemb;
   size_t ret = 0;
#if !(LIBCURL_VERSION_MAJOR <=7 && LIBCURL_VERSION_MINOR < 18)
   BandwidthStatistics *bwStatistics;
   double downloaded;
#endif

   ASSERT(NULL != request);

#if !(LIBCURL_VERSION_MAJOR <=7 && LIBCURL_VERSION_MINOR < 18)
   bwStatistics = &(request->statistics[BASICHTTP_DOWNLOAD]);

   curl_easy_getinfo(request->curl, CURLINFO_SIZE_DOWNLOAD, &downloaded);
   BasicHttpBandwidthUpdate(bwStatistics, (uint64) downloaded);

   if (request->bwGroup) {
      VmTimeType delay;

      delay = BasicHttpBandwidthGetDelay(request->bwGroup,
                                         request,
                                         BASICHTTP_DOWNLOAD);

      if (delay > 0) {
         VMwareStatus pollResult;
         pollResult = pollCallbackProc(POLL_CS_MAIN,
                                       0,
                                       BasicHttpResumePollCallback,
                                       request,
                                       POLL_REALTIME,
                                       delay, /* in microsec */
                                       NULL /* deviceLock */);
         if (VMWARE_STATUS_SUCCESS != pollResult) {
            ASSERT(0);
         }

         /*
          * Don't set request->pausedMask here. BasicHttpResumePollCallback
          * will un-pause the transfer after delay timeout.
          */
         ret = CURL_WRITEFUNC_PAUSE;
         goto exit;
      }
   }

   BasicHttpBandwidthSlideWindow(&(request->statistics[BASICHTTP_DOWNLOAD]));

   if (NULL != request->recvProgressProc) {
      Bool success;

      success = (request->recvProgressProc)(request,
                                            bufferSize,
                                            buffer,
                                            bwStatistics->transferredBytes,
                                            bwStatistics->windowedRate,
                                            request->clientData);
      if (!success) {
         /*
          * Pause the transfer. The transfer must be resumed by calling
          * BasicHttp_PauseRecvRequest().
          */
         request->pausedMask |= CURLPAUSE_RECV;
         ret = CURL_WRITEFUNC_PAUSE;
         goto exit;
      }
   }
#endif

   /*
    * If the caller set BASICHTTP_NO_RESPONSE_CONTENT, it means the caller
    * doesn't want to receive the response content from response->content.
    * Otherwise, append the partial result here into request->receiveBuf.
    */
   if (!(BASICHTTP_NO_RESPONSE_CONTENT & request->options)) {
      if (!DynBuf_Append(&request->receiveBuf, buffer, bufferSize)) {
         /*
          * If Append() fails, return 0 to stop the transfer.
          */
         Log("BasicHTTP: Failed to allocate memory for received data.\n");
         goto exit;
      }
   }

   ret = bufferSize;

exit:
   return ret;

} // BasicHttpWriteCallback


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpIoctlCallback --
 *        Callback for curl ioctl.
 *
 * Results:
 *        Handles CURLIOCMD_NOP and CURLIOCMD_RESTARTREAD.
 *
 * Side effects:
 *
 *
 *-----------------------------------------------------------------------------
 */

curlioerr
BasicHttpIoctlCallback(CURL *handle,
                       int cmd,
                       void *clientData)
{
   BasicHttpRequest *request = (BasicHttpRequest*) clientData;
   curlioerr ret = CURLIOE_UNKNOWNCMD;

   switch(cmd) {
      case CURLIOCMD_NOP:
         ret = CURLIOE_OK;
         break;
      case CURLIOCMD_RESTARTREAD:
         if (BasicHttpSourceRewind(request->body)) {
            BasicHttpBandwidthReset(&(request->statistics[BASICHTTP_UPLOAD]));
            BasicHttpBandwidthReset(&(request->statistics[BASICHTTP_DOWNLOAD]));
            ret = CURLIOE_OK;
         } else {
            ret = CURLIOE_FAILRESTART;
         }
         break;
      default:
         break;
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpResumePollCallback --
 *
 *       Callback to resume the transfer after it's been paused due to
 *       bandwidth control.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

#if !(LIBCURL_VERSION_MAJOR <=7 && LIBCURL_VERSION_MINOR < 18)
void
BasicHttpResumePollCallback(void *clientData)         // IN
{
   BasicHttpRequest *request;

   ASSERT(NULL != clientData);
   request = (BasicHttpRequest *) clientData;

   curl_easy_pause(request->curl, request->pausedMask);

   /*
    * The socket is already in the signaled state.
    */
   BasicHttpSocketPollCallback(NULL);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpRemoveResumePollCallback --
 *
 *       Remove BasicHttpResumePollCallback from poll.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpRemoveResumePollCallback(BasicHttpRequest *request)   // IN
{
#if CURL_VERSION_MAJOR <=7 && CURL_VERSION_MINOR < 18
   NOT_IMPLEMENTED();
#else
   ASSERT(NULL != request);

   if (NULL == request->bwGroup) {
      return;
   }

   pollCallbackRemoveProc(POLL_CS_MAIN,
                          0,
                          BasicHttpResumePollCallback,
                          request,
                          POLL_REALTIME);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AllocSource --
 *       Create a new source.
 *
 * Results:
 *       A pointer to a source. Caller must call BasicHttp_FreeSource.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpSource*
BasicHttp_AllocSource(const BasicHttpSourceOps *ops, void *privat)
{
   BasicHttpSource *ret = Util_SafeCalloc(1, sizeof(*ret));

   ret->ops = ops;
   ret->privat = privat;

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeSource --
 *       Free a source.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeSource(BasicHttpSource *source)                  // IN
{
   if (source) {
      if (source->ops && source->ops->destructProc) {
         source->ops->destructProc(source->privat);
      }
      free(source);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSourceRead --
 *      Safely read from a source.
 *
 * Results:
 *      Length in bytes read on success, -1 on failure.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

ssize_t
BasicHttpSourceRead(BasicHttpSource *source,    // IN
                    void *buffer,               // IN/OUT
                    size_t size,                // IN
                    size_t nmemb)               // IN
{
   ssize_t ret = 0;

   ASSERT(source);

   ASSERT(source->ops && source->ops->readProc);
   ret = source->ops->readProc(source->privat, buffer, size, nmemb);

   /* Valid return values of a read method. */
   ASSERT(-1 <= ret);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSourceRewind --
 *      Safely rewind a source.
 *
 * Results:
 *      TRUE on success.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttpSourceRewind(BasicHttpSource *source)        // IN
{
   ASSERT(source);

   ASSERT(source->ops && source->ops->rewindProc);
   return source->ops->rewindProc(source->privat);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpSourceLength --
 *      Safely find the length of a source.
 *
 * Results:
 *
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpSourceLength(BasicHttpSource *source)        // IN
{
   size_t ret = 0;

   ASSERT(source);

   ASSERT(source->ops && source->ops->lengthProc);
   ret = (curl_off_t) source->ops->lengthProc(source->privat);

   /* Valid return values of a length method. */
   ASSERT(0 <= ret);
   return ret;
}


/* BasicHttpMemorySource declarations. */
static ssize_t BasicHttpMemorySourceRead(void *privat,
                                         void *buffer, size_t size, size_t nmemb);
static Bool BasicHttpMemorySourceRewind(void *privat);
static size_t BasicHttpMemorySourceLength(void *privat);
static void BasicHttpMemorySourceDestruct(void *privat);

static BasicHttpSourceOps BasicHttpMemorySourceOps = {
   BasicHttpMemorySourceRead,
   BasicHttpMemorySourceRewind,
   BasicHttpMemorySourceLength,
   BasicHttpMemorySourceDestruct
};

typedef struct BasicHttpMemorySource BasicHttpMemorySource;


/* BasicHttpMemorySource implementation. */
struct BasicHttpMemorySource {
   uint8 *data;
   size_t dataLen;
   BasicHttpFreeProc *dataFreeProc;
   const uint8 *readPtr;
   size_t sizeLeft;
};


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AllocMemorySource --
 *       Create a new memory source. If dataFreeProc is not NULL, the memory
 *       source will take ownership of the data passed and call dataFreeProc
 *       on it in its destructor. Otherwise, the memory source will make its
 *       own copy of the data.
 *
 * Results:
 *       A pointer to a memory source. Caller must call BasicHttp_FreeSource.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpSource*
BasicHttp_AllocMemorySource(uint8 *data,                       // IN
                            size_t dataLen,                    // IN
                            BasicHttpFreeProc *dataFreeProc)   // IN
{
   BasicHttpMemorySource *source = (BasicHttpMemorySource*) Util_SafeCalloc(1, sizeof(*source));
   BasicHttpSource *ret = NULL;

   source->dataFreeProc = dataFreeProc;
   if (dataFreeProc) {
      source->data = data;
   } else {
      source->data = Util_SafeCalloc(1, dataLen);
      memcpy(source->data, data, dataLen);
   }
   source->dataLen = dataLen;
   source->readPtr = source->data;
   source->sizeLeft = dataLen;

   ret = BasicHttp_AllocSource(&BasicHttpMemorySourceOps, source);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_AllocStringSource --
 *       Create a new string memory source.
 *
 * Results:
 *       A pointer to a memory source. Caller must call BasicHttp_FreeSource.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

BasicHttpSource*
BasicHttp_AllocStringSource(const char *data)                // IN
{
   return BasicHttp_AllocMemorySource((uint8*) data, strlen(data), NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpMemorySourceDestruct --
 *       Free a BasicHttpMemorySource.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpMemorySourceDestruct(void *privat)                  // IN
{
   BasicHttpMemorySource *source = (BasicHttpMemorySource *) privat;

   if (source) {
      if (source->data) {
         if (source->dataFreeProc) {
            source->dataFreeProc(source->data);
         } else {
            free(source->data);
         }
      }
      free(source);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpMemorySourceRead --
 *       Read from a memory source.
 *
 * Results:
 *       The amount of data read is returned.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

ssize_t
BasicHttpMemorySourceRead(void *privat,                      // IN
                          void *buffer,                      // IN, OUT
                          size_t size,                       // IN
                          size_t nmemb)                      // IN
{
   BasicHttpMemorySource *source;
   size_t bufferSize;
   size_t readSize = 0;

   source = (BasicHttpMemorySource *) privat;
   ASSERT(NULL != source);

   bufferSize = size * nmemb;
   if (bufferSize < 1) {
      readSize = 0;
      goto abort;
   }

   if (source->sizeLeft > 0) {
      if (source->sizeLeft < bufferSize) {
         bufferSize = source->sizeLeft;
      }
      memcpy(buffer, source->readPtr, bufferSize);
      source->readPtr += bufferSize;
      source->sizeLeft -= bufferSize;

      readSize = bufferSize;
      goto abort;
   }
   else { // reset since curl may need to retry if the connection is broken.
      BasicHttpMemorySourceRewind(source);
   }

abort:
   return readSize;
} // BasicHttpMemorySourceRead


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpMemorySourceRewind --
 *       Rewind a memory source.
 *
 * Results:
 *       TRUE on success.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

Bool
BasicHttpMemorySourceRewind(void *privat)                     // IN
{
   BasicHttpMemorySource *source;

   source = (BasicHttpMemorySource *) privat;
   ASSERT(NULL != source);

   source->readPtr = source->data;
   source->sizeLeft = source->dataLen;

   return TRUE;
} // BasicHttpMemorySourceRewind


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpMemorySourceLength --
 *       Length of a BasicHttpMemorySource.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttpMemorySourceLength(void *privat)                  // IN
{
   BasicHttpMemorySource *source;

   source = (BasicHttpMemorySource *) privat;
   ASSERT(NULL != source);

   return source->dataLen;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttpFreeRequestBody --
 *       Free the objects related to the body. This is to be called after
 *       curl_easy_cleanup.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttpFreeRequestBody(BasicHttpRequest *request)            // IN
{
   /* Caller is responsible for freeing the source. If ownBody is true, then
      BasicHttp created the source. */

   if (request->ownBody) {
      BasicHttp_FreeSource(request->body);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeRequest --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeRequest(BasicHttpRequest *request)            // IN
{
   if (NULL == request) {
      return;
   }

   BasicHttp_CancelRequest(request);

   free((void *) request->url);
   free((void *) request->sslCAInfo);
   curl_slist_free_all(request->headerList);
   curl_slist_free_all(request->recvHeaderList);
   DynBuf_Destroy(&request->receiveBuf);
   free(request->userNameAndPassword);
   free(request->userAgent);
   free(request->proxy);
   if (NULL != request->curl) {
      curl_easy_cleanup(request->curl);
   }
   if (NULL != request->bwGroup) {
      BasicHttp_RemoveRequestFromBandwidthGroup(request->bwGroup, request);
   }
   BasicHttpFreeRequestBody(request);
   if (!curlGlobalState->skipRemove) {
      HashTable_Delete(curlGlobalState->requests, (void *)request);
   }
   free(request);
} // BasicHttp_FreeRequest


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_FreeResponse --
 *
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_FreeResponse(BasicHttpResponse *response)         // IN
{
   if (NULL == response) {
      return;
   }

   free(response->content);
   free(response->effectiveURL);
   free(response);
} // BasicHttp_FreeResponse


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_GetRecvContentInfo --
 *
 *       Get the receive content information for the request. This function
 *       can be called any time after request is created. But it will return
 *       useful information only after the header has been processed, for
 *       example, in the recvProgress callback.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

void
BasicHttp_GetRecvContentInfo(BasicHttpRequest *request,           // IN
                             BasicHttpContentInfo *contentInfo)   // IN:OUT
{
   if ((NULL == request) || (NULL == contentInfo)) {
      ASSERT(0);
      Log("BasicHttp_GetRecvContentInfo: Invalid argument.\n");
      return;
   }

   *contentInfo = request->recvContentInfo;
} // BasicHttp_GetRecvContentInfo


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_GetNumResponseHeaders --
 *
 *       Get the number of unhandled headers in the response to a request.
 *       This can be called at any time but will not return accurate results
 *       until after the response has been fully obtained (eg: In the SentProc
 *       callback).
 *
 * Results:
 *       Number of unhandled headers.
 *
 * Side effects:
 *       None
 *
 *-----------------------------------------------------------------------------
 */

size_t
BasicHttp_GetNumResponseHeaders(BasicHttpRequest *request) // IN
{
   return request->numRecvHeaders;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BasicHttp_GetResponseHeader --
 *
 *       Get a particular response header.
 *       This can be called at any time but will not return accurate results
 *       until after the response has been fully obtained (eg: In the SentProc
 *       callback).
 *
 * Results:
 *       The header as a string owned by BasicHttp.
 *
 * Side effects:
 *       None
 *
 *-----------------------------------------------------------------------------
 */

const char *
BasicHttp_GetResponseHeader(BasicHttpRequest *request, // IN
                            size_t header)             // IN
{
   size_t i = 0;
   struct curl_slist *headerList = request->recvHeaderList;

   ASSERT(header < request->numRecvHeaders);
   if (header >= request->numRecvHeaders) {
      return NULL;
   }

   for (i = 0; i < header; i++) {
      headerList = headerList->next;
      ASSERT(headerList);
   }

   return headerList->data;
}
