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
 * tunnelProxy.c --
 *
 *      Multi-channel socket proxy over HTTP with control messages, lossless
 *      reconnect, heartbeats, etc.
 */


#include <sys/time.h>   /* For gettimeofday */
#include <time.h>
#include <sys/types.h>
#ifndef __MINGW32__
#include <sys/socket.h> /* For getsockname */
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#else
#include "ws2tcpip.h"
#include "winsockerr.h"
#include "mingw32Util.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include <fcntl.h>


#include "base64.h"
#include "tunnelProxy.h"
#include "poll.h"


#if 0
#define DEBUG_DATA(x) g_debug x
#else
#define DEBUG_DATA(x)
#endif

#ifdef VMX86_DEBUG
#define DEBUG_MSG(x) g_debug x
#else
#define DEBUG_MSG(x)
#endif

typedef enum {
   TP_CHUNK_TYPE_ACK     = 65, // 'A'
   TP_CHUNK_TYPE_DATA    = 68, // 'D'
   TP_CHUNK_TYPE_MESSAGE = 77, // 'M'
} TPChunkType;

#ifdef __MINGW32__
typedef gint32 in_addr_t;
#endif


#define TP_MSGID_MAXLEN 24
#define TP_PORTNAME_MAXLEN 24
#define TP_BUF_MAXLEN 1024 * 10 // Tunnel reads/writes limited to 10K due to
                                // buffer pooling in tunnel server.
#define TP_MAX_UNACKNOWLEDGED 4
#define TP_MAX_START_FLOW_CONTROL 4 * TP_MAX_UNACKNOWLEDGED
#define TP_MIN_END_FLOW_CONTROL TP_MAX_UNACKNOWLEDGED


typedef struct {
   char type;
   unsigned int ackId;
   unsigned int chunkId;
   unsigned int channelId;
   char msgId[TP_MSGID_MAXLEN];
   char *body;
   int len;
} TPChunk;


typedef struct {
   char msgId[TP_MSGID_MAXLEN];
   TunnelProxyMsgHandlerCb cb;
   void *userData;
} TPMsgHandler;


typedef struct {
   TunnelProxy *tp;
   char portName[TP_PORTNAME_MAXLEN];
   unsigned int port;
   int fd;
   gboolean singleUse;
} TPListener;


typedef struct {
   TunnelProxy *tp;
   unsigned int channelId;
   char portName[TP_PORTNAME_MAXLEN];
   int fd;
   char recvByte;
} TPChannel;


struct TunnelProxy
{
   char *capID;
   char *hostIp;   // From TunnelProxy_Connect
   char *hostAddr; // From TunnelProxy_Connect
   char *reconnectSecret;     // From TP_MSG_AUTHENTICATED
   gint64 lostContactTimeout;  // From TP_MSG_AUTHENTICATED
   gint64 disconnectedTimeout; // From TP_MSG_AUTHENTICATED
   gint64 sessionTimeout;      // From TP_MSG_AUTHENTICATED

   guint lostContactTimeoutId;
   guint echoTimeoutId;

   struct timeval lastConnect;

   TunnelProxyNewListenerCb listenerCb;
   void *listenerCbData;
   TunnelProxyNewChannelCb newChannelCb;
   void *newChannelCbData;
   TunnelProxyEndChannelCb endChannelCb;
   void *endChannelCbData;
   TunnelProxySendNeededCb sendNeededCb;
   void *sendNeededCbData;
   TunnelProxyDisconnectCb disconnectCb;
   void *disconnectCbData;

   unsigned int maxChannelId;
   gboolean flowStopped;

   unsigned int lastChunkIdSeen;
   unsigned int lastChunkAckSeen;
   unsigned int lastChunkIdSent;
   unsigned int lastChunkAckSent;

   // Outgoing fifo
   GQueue *queueOut;
   GQueue *queueOutNeedAck;

   GList *listeners;
   GList *channels;
   GList *msgHandlers;

   GByteArray *readBuf;
   GByteArray *writeBuf;
};


/* Helpers */

static TunnelProxyErr TunnelProxyDisconnect(TunnelProxy *tp, const char *reason,
                                            gboolean closeSockets, gboolean notify);
static void TunnelProxyFireSendNeeded(TunnelProxy *tp);
static void TunnelProxySendChunk(TunnelProxy *tp, TPChunkType type,
                                 unsigned int channelId, const char *msgId,
                                 char *body, int bodyLen);
static void TunnelProxyFreeChunk(TPChunk *chunk);
static void TunnelProxyFreeMsgHandler(TPMsgHandler *handler);
static void TunnelProxyResetTimeouts(TunnelProxy *tp, gboolean requeue);


/* Default Msg handler callbacks */

static gboolean TunnelProxyEchoRequestCb(TunnelProxy *tp, const char *msgId,
                                         const char *body, int len, void *userData);
static gboolean TunnelProxyEchoReplyCb(TunnelProxy *tp, const char *msgId,
                                       const char *body, int len, void *userData);
static gboolean TunnelProxyStopCb(TunnelProxy *tp, const char *msgId,
                                  const char *body, int len, void *userData);
static gboolean TunnelProxyAuthenticatedCb(TunnelProxy *tp, const char *msgId,
                                           const char *body, int len, void *userData);
static gboolean TunnelProxyReadyCb(TunnelProxy *tp, const char *msgId,
                                   const char *body, int len, void *userData);
static gboolean TunnelProxySysMsgCb(TunnelProxy *tp, const char *msgId,
                                    const char *body, int len, void *userData);
static gboolean TunnelProxyErrorCb(TunnelProxy *tp, const char *msgId,
                                   const char *body, int len, void *userData);
static gboolean TunnelProxyPleaseInitCb(TunnelProxy *tp, const char *msgId,
                                        const char *body, int len, void *userData);
static gboolean TunnelProxyRaiseReplyCb(TunnelProxy *tp, const char *msgId,
                                        const char *body, int len, void *userData);
static gboolean TunnelProxyListenRequestCb(TunnelProxy *tp, const char *msgId,
                                           const char *body, int len, void *userData);
static gboolean TunnelProxyUnlistenRequestCb(TunnelProxy *tp, const char *msgId,
                                             const char *body, int len, void *userData);
static gboolean TunnelProxyLowerCb(TunnelProxy *tp, const char *msgId,
                                   const char *body, int len, void *userData);

/* Timer callbacks */

static void TunnelProxyEchoTimeoutCb(void *userData);
static void TunnelProxyLostContactTimeoutCb(void *userData);

/* Poll callbacks */

static void TunnelProxySocketConnectCb(void *userData);
static void TunnelProxySocketRecvCb(void *userData);


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_RemovePoll --
 *
 *      Remove a poll callback and data.
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
TunnelProxy_RemovePoll(PollerFunction f, /* IN */
                       void *clientData) /* IN */
{
   Poll_CallbackRemove(POLL_CS_MAIN, POLL_FLAG_READ | POLL_FLAG_SOCKET,
                       f, clientData, POLL_DEVICE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_PollAdd --
 *
 *      Add a poll callback and data.  They are removed first if
 *      already present.
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
TunnelProxy_AddPoll(PollerFunction f, /* IN */
                    void *clientData, /* IN */
                    int fd)           /* IN */
{
   TunnelProxy_RemovePoll(f, clientData);
   Poll_Callback(POLL_CS_MAIN, POLL_FLAG_READ | POLL_FLAG_SOCKET, f,
                 clientData, POLL_DEVICE, fd, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Create --
 *
 *       Create a TunnelProxy object, and add all the default message handlers.
 *
 * Results:
 *       Newly allocated TunnelProxy.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxy *
TunnelProxy_Create(const char *connectionId,             // IN
                   TunnelProxyNewListenerCb listenerCb,  // IN/OPT
                   void *listenerCbData,                 // IN/OPT
                   TunnelProxyNewChannelCb newChannelCb, // IN/OPT
                   void *newChannelCbData,               // IN/OPT
                   TunnelProxyEndChannelCb endChannelCb, // IN/OPT
                   void *endChannelCbData)               // IN/OPT
{
   TunnelProxy *tp = g_new0(TunnelProxy, 1);

   tp->capID = g_strdup(connectionId);

   tp->listenerCb = listenerCb;
   tp->listenerCbData = listenerCbData;
   tp->newChannelCb = newChannelCb;
   tp->newChannelCbData = newChannelCbData;
   tp->endChannelCb = endChannelCb;
   tp->endChannelCbData = endChannelCbData;

   tp->queueOut = g_queue_new();
   tp->queueOutNeedAck = g_queue_new();

#define TP_AMH(_msg, _cb) TunnelProxy_AddMsgHandler(tp, _msg, _cb, NULL)
   TP_AMH(TP_MSG_AUTHENTICATED, TunnelProxyAuthenticatedCb);
   TP_AMH(TP_MSG_ECHO_RQ,       TunnelProxyEchoRequestCb);
   TP_AMH(TP_MSG_ECHO_RP,       TunnelProxyEchoReplyCb);
   TP_AMH(TP_MSG_ERROR,         TunnelProxyErrorCb);
   TP_AMH(TP_MSG_LISTEN_RQ,     TunnelProxyListenRequestCb);
   TP_AMH(TP_MSG_LOWER,         TunnelProxyLowerCb);
   TP_AMH(TP_MSG_PLEASE_INIT,   TunnelProxyPleaseInitCb);
   TP_AMH(TP_MSG_RAISE_RP,      TunnelProxyRaiseReplyCb);
   TP_AMH(TP_MSG_READY,         TunnelProxyReadyCb);
   TP_AMH(TP_MSG_STOP,          TunnelProxyStopCb);
   TP_AMH(TP_MSG_SYSMSG,        TunnelProxySysMsgCb);
   TP_AMH(TP_MSG_UNLISTEN_RQ,   TunnelProxyUnlistenRequestCb);
#undef TP_AMH

   return tp;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyFireSendNeeded --
 *
 *       Utility to call the TunnelProxy's sendNeededCb if there are chunks
 *       that can be sent.
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
TunnelProxyFireSendNeeded(TunnelProxy *tp) // IN
{
   g_assert(tp);
   if (tp->sendNeededCb && TunnelProxy_HTTPSendNeeded(tp)) {
      tp->sendNeededCb(tp, tp->sendNeededCbData);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySendChunk --
 *
 *       Create and queue a new outgoing TPChunk object, specifying all the
 *       content.  The chunk will be appended to the TunnelProxy's outgoing
 *       queue, and the sendNeededCb passed to TunnelProxy_Connect invoked.
 *       Body content is always duplicated.
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
TunnelProxySendChunk(TunnelProxy *tp,        // IN
                     TPChunkType type,       // IN
                     unsigned int channelId, // IN/OPT
                     const char *msgId,      // IN/OPT
                     char *body,             // IN/OPT
                     int bodyLen)            // IN/OPT
{
   TPChunk *newChunk;

   g_assert(tp);

   newChunk = g_new0(TPChunk, 1);
   newChunk->type = type;

   newChunk->channelId = channelId;
   if (msgId) {
      strncpy(newChunk->msgId, msgId, TP_MSGID_MAXLEN);
   }
   if (body) {
      newChunk->len = bodyLen;
      newChunk->body = g_malloc(bodyLen + 1);
      newChunk->body[bodyLen] = 0;
      memcpy(newChunk->body, body, bodyLen);
   }

   g_queue_push_tail(tp->queueOut, newChunk);

   TunnelProxyFireSendNeeded(tp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Free --
 *
 *       Free a TunnelProxy object.  Calls TunnelProxyDisconnect to close all
 *       sockets, frees all pending in and out chunks.
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
TunnelProxy_Free(TunnelProxy *tp) // IN
{
   TPChunk *chunk;

   g_assert(tp);

   TunnelProxyDisconnect(tp, NULL, TRUE, FALSE);

   while ((chunk = (TPChunk *)g_queue_pop_head(tp->queueOut))) {
      TunnelProxyFreeChunk(chunk);
   }
   g_queue_free(tp->queueOut);

   while ((chunk = (TPChunk *)g_queue_pop_head(tp->queueOutNeedAck))) {
      TunnelProxyFreeChunk(chunk);
   }
   g_queue_free(tp->queueOutNeedAck);

   while (tp->msgHandlers) {
      TunnelProxyFreeMsgHandler((TPMsgHandler *)tp->msgHandlers->data);
      tp->msgHandlers = g_list_delete_link(tp->msgHandlers, tp->msgHandlers);
   }

   g_free(tp->capID);
   g_free(tp->hostIp);
   g_free(tp->hostAddr);
   g_free(tp->reconnectSecret);
   g_free(tp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyFreeChunk --
 *
 *       Free a TPChunk object, and remove it from the queue passed.
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
TunnelProxyFreeChunk(TPChunk *chunk) // IN
{
   g_assert(chunk);
   g_free(chunk->body);
   g_free(chunk);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyFreeMsgHandler --
 *
 *       Free a TPMsgHandler object, and remove it from the queue passed.
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
TunnelProxyFreeMsgHandler(TPMsgHandler *handler) // IN
{
   g_assert(handler);
   g_free(handler);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_AddMsgHandler --
 *
 *       Allocate a new msg handler object which handles messages of the given
 *       msgId, and add it to the TunnelProxy handler queue.
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
TunnelProxy_AddMsgHandler(TunnelProxy *tp,            // IN
                          const char *msgId,          // IN
                          TunnelProxyMsgHandlerCb cb, // IN
                          void *userData)             // IN/OPT
{
   TPMsgHandler *msgHandler;

   g_assert(tp);
   g_assert(msgId);

   msgHandler = g_new0(TPMsgHandler, 1);
   strncpy(msgHandler->msgId, msgId, TP_MSGID_MAXLEN);
   msgHandler->userData = userData;
   msgHandler->cb = cb;

   tp->msgHandlers = g_list_append(tp->msgHandlers, msgHandler);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_SendMsg --
 *
 *       Append a message of msgId with the given body to the outgoing message
 *       queue.  The chunk will be assigned the next serial chunk id.  The
 *       body buffer will be copied.
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
TunnelProxy_SendMsg(TunnelProxy *tp,   // IN
                    const char *msgId, // IN
                    const char *body,  // IN
                    int len)           // IN
{
   g_assert(tp);
   g_assert(msgId && strlen(msgId) < TP_MSGID_MAXLEN);

   TunnelProxySendChunk(tp, TP_CHUNK_TYPE_MESSAGE, 0, msgId, (char*) body, len);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_ReadMsg --
 *
 *       Parse a formatted message using a key=type:value markup syntax, with
 *       the value destination pointer passed in to the next argument, similar
 *       to sscanf.  Final argument must be NULL.  Supported types are:
 *
 *          S - a base64 encoded utf8 string
 *          E - a base64 encoded utf8 error string
 *          I - integer
 *          L - 64-bit integer
 *          B - boolean, 1, "true", and "yes" are all considered TRUE
 *
 *       e.g. TunnelProxy_ReadMsg(body, len, "reason=S", &reasonStr, NULL);
 *
 *       Returned strings must be freed by the caller.  Non-null string args
 *       must be freed regardless of return value, in case of partial success.
 *
 * Results:
 *       TRUE if all key=type:value pairs parsed correctly, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
TunnelProxy_ReadMsg(const char *body,        // IN
                    int len,                 // IN
                    const char* nameTypeKey, // IN
                    ...)                     // OUT
{
   va_list args;
   va_start(args, nameTypeKey);
   gboolean success = TRUE;
   char *valueStr = NULL;

   while (nameTypeKey) {
      int nameLen = strlen(nameTypeKey);
      char *prefix = strstr(body, nameTypeKey);
      char *start;
      char *end;
      gint32 *I;
      gint64 *L;
      gboolean *B;

      if (!prefix || prefix[nameLen] != ':' ||
          (prefix != body && prefix[-1] != '|')) {
         success = FALSE;
         break;
      }

      /*
       * Find value string before '|' which starts the next element.  Skip the
       * colon ':' after nameTypeKey, separating the type-id char from the value
       * string.
       */
      start = prefix + nameLen + 1;
      end = strchr(start, '|');
      valueStr = end ? g_strndup(start, end - start) : g_strdup(start);

      switch (prefix[nameLen-1]) {
      case 'S':
      case 'E': {
         char **S = va_arg(args, char**);
         size_t decodeLen;
         guint8 *decodeBuf = NULL;

         decodeLen = Base64_DecodedLength(valueStr, strlen(valueStr));
         decodeBuf = g_malloc(decodeLen + 1);

         success = Base64_Decode(valueStr, decodeBuf, decodeLen, &decodeLen);

         if (success) {
            decodeBuf[decodeLen] = '\0';
            *S = (char*) decodeBuf;
         } else {
            g_free(decodeBuf);
            *S = NULL;
            goto exit;
         }
         break;
      }
      case 'I':
         I = va_arg(args, gint32*);
         *I = strtol(valueStr, NULL, 10);
         break;
      case 'L':
         L = va_arg(args, gint64*);
         *L = strtoll(valueStr, NULL, 10);
         break;
      case 'B':
         B = va_arg(args, gboolean*);
         *B = FALSE;
         if (strcmp(valueStr, "1") == 0 ||
             g_ascii_strcasecmp(valueStr, "true") == 0 ||
             g_ascii_strcasecmp(valueStr, "yes") == 0) {
            *B = TRUE;
         }
         break;
      default:
         g_assert_not_reached();
      }

      g_free(valueStr);
      valueStr = NULL;

      nameTypeKey = va_arg(args, char*);
   }

exit:
   va_end(args);
   g_free(valueStr);

   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_FormatMsg --
 *
 *       Compose a formatted message using a key=type:value markup syntax,
 *       where the value is taken from the next argument.  Final argument
 *       must be NULL.  See TunnelProxy_ReadMsg for supported types.
 *
 *       e.g. TunnelProxy_FormatMsg(&val, &len, "portName=S", portName, NULL);
 *
 *       Returned body must be freed by the caller.
 *
 * Results:
 *       TRUE if all name=type:value pairs parsed correctly, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
TunnelProxy_FormatMsg(char **body,             // OUT
                      int *len,                // OUT
                      const char* nameTypeKey, // IN
                      ...)                     // IN
{
   va_list args;
   GString *builder;
   gboolean success = TRUE;

   *body = NULL;
   *len = -1;

   builder = g_string_new(NULL);
   va_start(args, nameTypeKey);

   while (nameTypeKey) {
      int nameLen = strlen(nameTypeKey);
      const char *S;
      char *SEncoded;
      gint32 I;
      gint64 L;
      gboolean B;

      g_string_append(builder, nameTypeKey);
      g_string_append_c(builder, ':');

      switch (nameTypeKey[nameLen-1]) {
      case 'S':
      case 'E':
         // Strings are always Base64 encoded
         S = va_arg(args, const char*);
         g_assert(S);
         success = Base64_EasyEncode(S, strlen(S), &SEncoded);
         if (!success) {
            g_debug("Failed to base64-encode \"%s\"", S);
            goto exit;
         }
         g_string_append(builder, SEncoded);
         g_free(SEncoded);
         break;
      case 'I':
         I = va_arg(args, gint32);
         g_string_append_printf(builder, "%d", I);
         break;
      case 'L':
         L = va_arg(args, gint64);
         g_string_append_printf(builder, "%"G_GINT64_MODIFIER"d", L);
         break;
      case 'B':
         B = va_arg(args, int);
         g_string_append(builder, B ? "true" : "false");
         break;
      default:
         g_assert_not_reached();
      }

      g_string_append_c(builder, '|');

      nameTypeKey = va_arg(args, char*);
   }

exit:
   va_end(args);

   if (success) {
      *len = builder->len;
      *body = g_string_free(builder, FALSE);
   } else {
      g_string_free(builder, TRUE);
   }

   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_GetConnectUrl --
 *
 *       Create a URL to use when POSTing, based on a server URL retreived
 *       during broker tunnel initialization.  If the TunnelProxy has been
 *       connected before and there is a valid reconnectSecret, the URL will be
 *       different from an initial connection.
 *
 * Results:
 *       The URL of the tunnel connect/reconnect.  Caller must free it.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

char *
TunnelProxy_GetConnectUrl(TunnelProxy *tp,       // IN
                          const char *serverUrl) // IN
{
   if (tp->capID) {
      if (tp->reconnectSecret) {
         return g_strdup_printf("%s"TP_RECONNECT_URL_PATH"?%s&%s", serverUrl,
                                tp->capID, tp->reconnectSecret);
      } else {
         return g_strdup_printf("%s"TP_CONNECT_URL_PATH"?%s", serverUrl,
                                tp->capID);
      }
   } else {
      return g_strdup_printf("%s"TP_CONNECT_URL_PATH, serverUrl);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Connect --
 *
 *       Connect or reconnect a TunnelProxy.  Queues an INIT msg.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       Reinitializes tunnel read/write buffers.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_Connect(TunnelProxy *tp,                      // IN
                    const char *hostIp,                   // IN/OPT
                    const char *hostAddr,                 // IN/OPT
                    TunnelProxySendNeededCb sendNeededCb, // IN/OPT
                    void *sendNeededCbData,               // IN/OPT
                    TunnelProxyDisconnectCb disconnectCb, // IN/OPT
                    void *disconnectCbData)               // IN/OPT
{
   gboolean isReconnect;

   g_assert(tp);

   isReconnect = (tp->lastConnect.tv_sec > 0);
   if (isReconnect && !tp->reconnectSecret) {
      return TP_ERR_INVALID_RECONNECT;
   }

   gettimeofday(&tp->lastConnect, NULL);

   g_free(tp->hostIp);
   g_free(tp->hostAddr);
   tp->hostIp = g_strdup(hostIp ? hostIp : "127.0.0.1");
   tp->hostAddr = g_strdup(hostAddr ? hostAddr : "localhost");

   tp->sendNeededCb = sendNeededCb;
   tp->sendNeededCbData = sendNeededCbData;
   tp->disconnectCb = disconnectCb;
   tp->disconnectCbData = disconnectCbData;

   if (tp->readBuf) {
      g_byte_array_free(tp->readBuf, TRUE);
   }
   tp->readBuf = g_byte_array_new();
   if (tp->writeBuf) {
      g_byte_array_free(tp->writeBuf, TRUE);
   }
   tp->writeBuf = g_byte_array_new();

   if (isReconnect) {
      TPChunk *chunk;
      TunnelProxyResetTimeouts(tp, TRUE);

      while ((chunk = g_queue_pop_head(tp->queueOut))) {
         g_queue_push_tail(tp->queueOutNeedAck, chunk);
      }

      /* Want to ACK the last chunk ID we saw */
      tp->lastChunkAckSent = 0;

      TunnelProxyFireSendNeeded(tp);
   } else {
      char *initBody = NULL;
      int initLen = 0;
      /* XXX: Need our own type, and version. */
      TunnelProxy_FormatMsg(&initBody, &initLen,
                            "type=S", "C", /* "simple" C client */
                            "v1=I", 3, "v2=I", 1, "v3=I", 4,
                            "cid=S", "1234",
                            NULL);
      TunnelProxy_SendMsg(tp, TP_MSG_INIT, initBody, initLen);
      g_free(initBody);
   }

   return TP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyDisconnect --
 *
 *       Disconnect a TunnelProxy.  If closeSockets is true, all sockets and
 *       channels are shutdown, and corresponding UNLISTEN_RP messages are
 *       sent.  If notify is true, the disconnect callback is invoked with
 *       with reason argument to allow reconnection.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxyDisconnect(TunnelProxy *tp,       // IN
                      const char *reason,    // IN
                      gboolean closeSockets, // IN
                      gboolean notify)       // IN
{
   TunnelProxyErr err;

   if (tp->lastConnect.tv_sec == 0) {
      return TP_ERR_NOT_CONNECTED;
   }

   /* Cancel any existing timeouts */
   TunnelProxyResetTimeouts(tp, FALSE);

   if (closeSockets) {
      while (tp->listeners) {
         TPListener *listener = (TPListener *)tp->listeners->data;
         /* This will close all the channels as well */
         err = TunnelProxy_CloseListener(tp, listener->portName);
         g_assert(TP_ERR_OK == err);
      }
   }

   if (notify && tp->disconnectCb) {
      g_assert(reason);
      tp->disconnectCb(tp, tp->reconnectSecret, reason, tp->disconnectCbData);
   }

   return TP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_Disconnect --
 *
 *       Disconnect a TunnelProxy.  All sockets and channels are shutdown, and
 *       corresponding UNLISTEN_RP messages are sent.  The disconnect callback
 *       passed to TunnelProxy_Connect is not invoked.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_Disconnect(TunnelProxy *tp) // IN
{
   return TunnelProxyDisconnect(tp, NULL, TRUE, FALSE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_CloseListener --
 *
 *       Close a listening socket identified by portName.  All socket channels
 *       are closed, and an UNLISTEN_RP msg is sent to the tunnel server.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_CloseListener(TunnelProxy *tp,      // IN
                          const char *portName) // IN
{
   TPListener *listener = NULL;
   char *unlisten = NULL;
   int unlistenLen = 0;
   TunnelProxyErr err;
   GList *li;

   g_assert(tp);
   g_assert(portName);

   for (li = tp->listeners; li; li = li->next) {
      TPListener *listenerIter = (TPListener *)li->data;
      if (strcmp(listenerIter->portName, portName) == 0) {
         listener = listenerIter;
         break;
      }
   }
   if (!listener) {
      return TP_ERR_INVALID_LISTENER;
   }

   TunnelProxy_RemovePoll(TunnelProxySocketConnectCb, listener);
   close(listener->fd);

   tp->listeners = g_list_remove(tp->listeners, listener);
   g_free(listener);

   /*
    * Send an UNLISTEN_RQ in any case of closing.  It might not be an actual
    * reply if closing due to max connections being hit.
    */
   TunnelProxy_FormatMsg(&unlisten, &unlistenLen, "portName=S", portName, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_UNLISTEN_RP, unlisten, unlistenLen);
   g_free(unlisten);

   /* Close all the channels */
   li = tp->channels;
   while (li) {
      TPChannel *channel = (TPChannel *)li->data;
      li = li->next;
      if (strcmp(channel->portName, portName) == 0) {
         err = TunnelProxy_CloseChannel(tp, channel->channelId);
         g_assert(TP_ERR_OK == err);
      }
   }

   return TP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_CloseChannel --
 *
 *       Close an individual socket channel identified by its channelId.  If
 *       the channel's listner is single-use, TunnelProxy_CloseListener is
 *       invoked.  Otherwise, a LOWER message is sent to the tunnel server.
 *
 * Results:
 *       TunnelProxyErr.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

TunnelProxyErr
TunnelProxy_CloseChannel(TunnelProxy *tp,        // IN
                         unsigned int channelId) // IN
{
   TPChannel *channel = NULL;
   TunnelProxyErr err;
   GList *li;

   g_assert(tp);

   for (li = tp->channels; li; li = li->next) {
      TPChannel *chanIter = (TPChannel *)li->data;
      if (chanIter->channelId == channelId) {
         channel = chanIter;
         break;
      }
   }
   if (!channel) {
      return TP_ERR_INVALID_CHANNELID;
   }

   for (li = tp->listeners; li; li = li->next) {
      TPListener *listener = (TPListener *)li->data;

      if (listener->singleUse &&
          strcmp(listener->portName, channel->portName) == 0) {
         g_debug("Closing single-use listener \"%s\" after channel \"%d\" "
                 "disconnect.", channel->portName, channelId);

         err = TunnelProxy_CloseListener(tp, channel->portName);
         g_assert(TP_ERR_OK == err);

         /* Channel is no more */
         channel = NULL;
         break;
      }
   }

   if (channel) {
      char *lower = NULL;
      int lowerLen = 0;

      if (channel->fd >= 0) {
         TunnelProxy_RemovePoll(TunnelProxySocketRecvCb, channel);
         close(channel->fd);
      }

      TunnelProxy_FormatMsg(&lower, &lowerLen, "chanID=I", channelId, NULL);
      TunnelProxy_SendMsg(tp, TP_MSG_LOWER, lower, lowerLen);
      g_free(lower);

      tp->channels = g_list_remove(tp->channels, channel);
      g_free(channel);
   }

   return TP_ERR_OK;
}


/*
 * Tunnel channel connect and IO handlers
 */


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySocketRecvCb --
 *
 *       Read IO callback handler for a given socket channel.  An attempt is
 *       made to read all data available on the socket in a non-blocking
 *       fashion.  If an error occurs while reading, TunnelProxy_CloseChannel
 *       is called.
 *
 *       If any data is read, a new outgoing data chunk is queued with all the
 *       data.  Max data size for one chunk is 10K (see
 *       wswc_tunnel/conveyor.cpp).
 *
 *       AsyncSocket_Recv for 1 byte is issued to cause this callback to be
 *       invoked the next time there is at least one byte of data to read.
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
TunnelProxySocketRecvCb(void *clientData) /* IN: TPChannel */
{
   TPChannel *channel = clientData;
   int throttle = 3;
   char recvBuf[TP_BUF_MAXLEN];
   ssize_t recvLen = 0;
   TunnelProxyErr err;

   g_assert(channel);

   while (throttle--) {
#ifdef __MINGW32__
      recvLen = recv(channel->fd, recvBuf, sizeof(recvBuf), 0);
      if (recvLen == SOCKET_ERROR) {
         if (WSAGetLastError() == WSAEWOULDBLOCK) {
            goto pollAgain;
         } else {
            g_printerr(
               _("Error reading from tunnel HTTP socket: %d\n"),
               WSAGetLastError());
            recvLen = 0;
         }
      }
#else
      recvLen = read(channel->fd, recvBuf, sizeof(recvBuf));
#endif
      switch (recvLen) {
      case -1:
         switch (errno) {
         case EINTR:
            goto loopAgain;
         case EAGAIN:
            goto pollAgain;
         default:
            break;
         }
         g_printerr("Error reading from channel \"%d\": %s\n",
                    channel->channelId, strerror(errno));
         /* fall through... */
      case 0:
         if (channel->tp->endChannelCb) {
            channel->tp->endChannelCb(channel->tp, channel->portName,
                                      channel->fd,
                                      channel->tp->endChannelCbData);
         }
         err = TunnelProxy_CloseChannel(channel->tp, channel->channelId);
         g_assert(TP_ERR_OK == err);
         return;
      default:
         TunnelProxySendChunk(channel->tp, TP_CHUNK_TYPE_DATA,
                              channel->channelId, NULL, recvBuf, recvLen);
         break;
      }
   loopAgain:
      ;
   }
pollAgain:
   TunnelProxy_AddPoll(TunnelProxySocketRecvCb, channel, channel->fd);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySocketConnectCb --
 *
 *       Connection handler callback to notify of a new local socket
 *       connection for a given TPListener.  Creates a new channel and adds it
 *       to the TunnelProxy's channel queue.
 *
 *       Sends a RAISE_RQ to the tunnel server to notify it of the new channel
 *       connection.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Calls the TunnelProxy's newChannelCb to notify of channel create.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxySocketConnectCb(void *clientData) /* IN */
{
   TPListener *listener = clientData;
   TunnelProxy *tp;
   char *raiseBody;
   int raiseLen;
   TPChannel *newChannel;
   int newChannelId;
   int fd;
   int nodelay = 1;
   long flags;

   g_assert(listener);

   tp = listener->tp;
   g_assert(tp);

   fd = accept(listener->fd, NULL, NULL);
   if (fd < 0) {
#ifdef __MINGW32__
      if (WSAGetLastError() != WSAEWOULDBLOCK) {
         g_printerr("Could not accept client socket: %d\n", WSAGetLastError());
      }
#else
      if (errno != EWOULDBLOCK) {
         g_printerr("Could not accept client socket: %s\n", strerror(errno));
      }
#endif
      goto fin;
   }

   setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
              (const void *)&nodelay, sizeof(nodelay));

#ifdef __MINGW32__
   SET_NONBLOCKING(fd);
#else
#ifdef O_NONBLOCK
   flags = O_NONBLOCK;
#else
   flags = O_NDELAY;
#endif
   fcntl(fd, F_SETFL, flags);
#endif

   if (tp->newChannelCb && !tp->newChannelCb(tp, listener->portName, fd,
                                             tp->newChannelCbData)) {
      g_debug("Rejecting new channel connection to listener \"%s\"",
              listener->portName);
      close(fd);
      goto fin;
   }

   newChannelId = ++tp->maxChannelId;

   g_debug("Creating new channel \"%d\" to listener \"%s\".",
           newChannelId, listener->portName);

   newChannel = g_new0(TPChannel, 1);
   newChannel->channelId = newChannelId;
   strncpy(newChannel->portName, listener->portName, TP_PORTNAME_MAXLEN);
   newChannel->fd = fd;
   newChannel->tp = tp;

   tp->channels = g_list_append(tp->channels, newChannel);

   TunnelProxy_FormatMsg(&raiseBody, &raiseLen,
                         "chanID=I", newChannel->channelId,
                         "portName=S", newChannel->portName, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_RAISE_RQ, raiseBody, raiseLen);
   g_free(raiseBody);

fin:
   TunnelProxy_AddPoll(TunnelProxySocketConnectCb, listener, listener->fd);
}


/*
 * HTTP IO driver interface
 */


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadHex --
 *
 *       Inline stream parsing helper.  Attempts to read a hex-encoded integer
 *       string followed by a trailing char, and returns it in the out param.
 *       Advances the idx param past the trailing char.
 *
 * Results:
 *       TRUE if a terminated hex number was read, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static inline gboolean
TunnelProxyReadHex(char *buf,  // IN
                   int len,    // IN
                   char trail, // IN
                   int *idx,   // IN/OUT
                   int *out)   // OUT
{
   int numDigits = 0;
   long value = 0;

   while (len > *idx + numDigits) {
      char digit = buf[*idx + numDigits];

      if (digit == trail) {
         *idx += numDigits + 1;
         *out = value;
         return TRUE;
      }

      numDigits++;
      value = value << 4; /* Shift four places (multiply by 16) */

      /* Add in digit value */
      if (digit >= '0' && digit <= '9') {
         value += digit - '0';      /* Number range 0..9 */
      } else if (digit >= 'A' && digit <= 'F') {
         value += digit - 'A' + 10; /* Character range 10..15 */
      } else if (digit >= 'a' && digit <= 'f') {
         value += digit - 'a' + 10; /* Character range 10..15 */
      } else {
         g_debug("TunnelProxyReadHex: Invalid number character: %u", digit);
         return FALSE;
      }
   }

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadStr --
 *
 *       Inline stream parsing helper.  Given a string length, attempts to
 *       verify that the entire string is available and is terminated by a ';'.
 *       Advances the idx param past the ';'.  Sets the start param to the
 *       beginning of the string.
 *
 * Results:
 *       TRUE if a ';' terminated string number was read, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static inline gboolean
TunnelProxyReadStr(char *buf,    // IN
                   int len,      // IN
                   int *idx,     // IN/OUT: current index
                   char **start, // OUT: start of string
                   int strLen)   // IN: expecting this size
{
   if (len > *idx + strLen + 1 && buf[*idx + strLen] == ';') {
      *start = &buf[*idx];
      *idx += strLen + 1;
      return TRUE;
   }
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadChunk --
 *
 *       Attempts to read a single well-formatted Ack, Data or Message chunk
 *       from the buf param.  Fills the chunk arg with the read data, without
 *       copying.
 *
 *       If httpChunked is true, the buf is assumed to be HTTP-chunked encoded,
 *       with '%x\r\n.....\r\n' surrounding each chunk.
 *
 * Results:
 *       Size of data read from buf, 0 if no chunk was read.
 *
 * Side effects:
 *       If successful, sets readLen to the amount of data read from the
 *       beginning of buf to construct the chunk.
 *
 *-----------------------------------------------------------------------------
 */

static unsigned int
TunnelProxyReadChunk(char *buf,        // IN
                     int len,          // IN
                     gboolean httpChunked, // IN
                     TPChunk *chunk)   // IN/OUT
{
   int minLen = httpChunked ? 10 : 3;
   int readLen = 0;

#define READHEX(_val) \
   if (!TunnelProxyReadHex(buf, len, ';', &readLen, _val)) { return 0; }
#define READSTR(_out, _len) \
   if (!TunnelProxyReadStr(buf, len, &readLen, _out, _len)) { return 0; }

   g_assert(chunk);

   if (len < minLen) {
      return 0;
   }

   if (httpChunked) {
      /* Chunked header looks like %x\r\n.....\r\n */
      int chunkLen = 0;
      if (!TunnelProxyReadHex(buf, len, '\r', &readLen, &chunkLen) ||
          readLen + 1 + chunkLen + 2 > len) {
         return FALSE;
      }

      g_assert(buf[readLen] == '\n');
      readLen++;
   }

   {
      char *typeStr = NULL;
      READSTR(&typeStr, 1)
      chunk->type = *typeStr;
   }

   switch (chunk->type) {
   case TP_CHUNK_TYPE_ACK:
      READHEX(&chunk->ackId)
      DEBUG_DATA(("RECV-ACK(ackId=%d)", chunk->ackId));
      break;
   case TP_CHUNK_TYPE_MESSAGE: {
      char *hdr = NULL;
      int hdrLen = 0;
      char *msgId = NULL;

      READHEX(&chunk->chunkId)
      READHEX(&chunk->ackId)

      READHEX(&hdrLen)
      READSTR(&hdr, hdrLen)

      READHEX(&chunk->len)
      READSTR(&chunk->body, chunk->len)

      if (!TunnelProxy_ReadMsg(hdr, hdrLen, "messageType=S", &msgId, NULL)) {
         g_debug("Invalid messageType in tunnel message header!");
         return FALSE;
      }

      strncpy(chunk->msgId, msgId, TP_MSGID_MAXLEN);
      g_free(msgId);

      DEBUG_DATA(("RECV-MSG(id=%d, ack=%d, msgid=%s, length=%d): %.*s",
                  chunk->chunkId, chunk->ackId,
                  chunk->msgId,
                  chunk->len, chunk->len, chunk->body));
      break;
   }
   case TP_CHUNK_TYPE_DATA:
      READHEX(&chunk->chunkId)
      READHEX(&chunk->ackId)
      READHEX(&chunk->channelId)

      READHEX(&chunk->len)
      READSTR(&chunk->body, chunk->len)

      DEBUG_DATA(("RECV-DATA(id=%d, ack=%d, channel=%d, length=%d)",
                  chunk->chunkId, chunk->ackId, chunk->channelId, chunk->len));
      break;
   default:
      g_debug("Invalid tunnel message type identifier \"%c\" (%d).",
              chunk->type, chunk->type);
      g_assert_not_reached();
   }

   if (httpChunked) {
      g_assert(buf[readLen] == '\r');
      g_assert(buf[readLen + 1] == '\n');

      /* Move past trailing \r\n */
      readLen += 2;
   }

   return readLen;

#undef READHEX
#undef READSTR
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyHandleInChunk --
 *
 *       Processes the next chunk in the incoming chunk queue.  If the chunk
 *       is an Ack, a message in the outgoing needs-ACK queue with the
 *       corresponding chunkId is found and freed.  If a Message chunk, a
 *       handler for the chunk's msgId header is found and invoked with the
 *       chunk data.  If a data chunk, the socket channel with the
 *       corresponding channelId is located and the chunk data written to the
 *       socket using AsyncSocket_Send.
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
TunnelProxyHandleInChunk(TunnelProxy *tp, // IN
                         TPChunk *chunk)  // IN
{
   g_assert(tp);
   g_assert(chunk);

   if (chunk->chunkId > 0) {
      if (chunk->chunkId <= tp->lastChunkIdSeen) {
         /* This chunk has been replayed... skip it. */
         g_debug("Skipping replayed chunk ID '%d'.", chunk->chunkId);
         return;
      }
      tp->lastChunkIdSeen = chunk->chunkId;
   }

   if (chunk->ackId > 0) {
      TPChunk *outChunk;

      if (chunk->ackId > tp->lastChunkIdSent) {
         g_debug("Unknown ACK ID '%d' in received tunnel message.", chunk->ackId);
      }

      for (outChunk = g_queue_peek_head(tp->queueOutNeedAck);
           outChunk && chunk->chunkId >= outChunk->chunkId;
           outChunk = g_queue_peek_head(tp->queueOutNeedAck)) {
         g_queue_pop_head(tp->queueOutNeedAck);
         TunnelProxyFreeChunk(outChunk);
      }

      tp->lastChunkAckSeen = chunk->ackId;
   }

   switch (chunk->type) {
   case TP_CHUNK_TYPE_MESSAGE: {
      gboolean found = FALSE;
      GList *li;

      g_assert(chunk->msgId);
      for (li = tp->msgHandlers; li; li = li->next) {
         TPMsgHandler *handler = (TPMsgHandler *)li->data;
         if (g_ascii_strcasecmp(handler->msgId, chunk->msgId) == 0) {
            g_assert(handler->cb);
            found = TRUE;
            if (handler->cb(tp, chunk->msgId, chunk->body, chunk->len,
                            handler->userData)) {
               /* True means the handler handled the message, so stop here. */
               break;
            }
         }
      }

      if (!found) {
         DEBUG_MSG(("Unhandled message type '%s' received.", chunk->msgId));
      }
      break;
   }
   case TP_CHUNK_TYPE_DATA: {
      gboolean found = FALSE;
      GList *li;

      for (li = tp->channels; li; li = li->next) {
         TPChannel *channel = (TPChannel *)li->data;

         if (channel->channelId == chunk->channelId) {
            ssize_t bytesWritten = 0;
            char *buf = chunk->body;
            int len = chunk->len;
            /*
             * XXX: Yes, this is a blocking write.  The data is
             * usually small, and IOChannels are buffered, so maybe it
             * doesn't matter? If it becomes a problem we'll need to
             * add a writeBuf to our channel, and an G_IO_OUT callback
             * that drains it.
             */
            while (len) {
#ifdef __MINGW32__
               bytesWritten = send(channel->fd, buf, len, 0);
               if (bytesWritten == SOCKET_ERROR) {
                  DWORD error = WSAGetLastError();
                  if (error == WSAEWOULDBLOCK) {
                     errno = EWOULDBLOCK;
                  } else {
                     g_printerr("Error writing to chunk: %d\n",
                        WSAGetLastError());
                  }
                  bytesWritten = -1;
               }
#else
               bytesWritten = write(channel->fd, buf, len);
#endif
               if (bytesWritten < 0) {
                  if (errno != EAGAIN &&
                      errno != EINTR &&
                      errno != EWOULDBLOCK) {
                     g_printerr("Error writing to chunk: %s\n",
                        strerror(errno));
                     len = 0;
                     break;
                  }
               } else {
                  len -= bytesWritten;
                  buf += bytesWritten;
               }
            }
            found = TRUE;
            break;
         }
      }

      if (!found) {
         DEBUG_MSG(("Data received for unknown channel id '%d'.",
                    chunk->channelId));
      }
      break;
   }
   case TP_CHUNK_TYPE_ACK:
      /* Let the common ACK handling happen */
      break;
   default:
      g_assert_not_reached();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_HTTPRecv --
 *
 *       Process incoming tunnel data read from an unknown HTTP source.  The
 *       buffer contents are assumed *not* to be in HTTP chunked encoding.
 *
 *       Appends the buffer data to the TunnelProxy's readBuf, and attempts to
 *       construct and queue incoming chunks from the data by calling
 *       TunnelProxyReadChunk.  The data read from the readBuf and used
 *       to construct full chunks is removed from the front of the readBuf.
 *
 *       TunnelProxyHandleInChunk is called repeatedly to process any
 *       existing or newly queued incoming chunks.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Many.
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelProxy_HTTPRecv(TunnelProxy *tp,  // IN
                     const char *buf,  // IN
                     int bufSize,      // IN
                     gboolean httpChunked) // IN
{
   int totalReadLen = 0;

   g_assert(tp);
   g_assert(buf && bufSize > 0);

   g_byte_array_append(tp->readBuf, buf, bufSize);

   while (TRUE) {
      unsigned int readLen;
      TPChunk chunk;

      memset(&chunk, 0, sizeof(chunk));
      readLen = TunnelProxyReadChunk(tp->readBuf->data + totalReadLen,
                                     tp->readBuf->len - totalReadLen,
                                     httpChunked, &chunk);
      if (!readLen) {
         break;
      }

      TunnelProxyHandleInChunk(tp, &chunk);
      totalReadLen += readLen;
   }

   if (!totalReadLen) {
      return;
   }

   /* Shrink the front of the read buffer */
   g_byte_array_remove_range(tp->readBuf, 0, totalReadLen);

   /* Reset timeouts after successfully reading a chunk. */
   TunnelProxyResetTimeouts(tp, TRUE);

   /* Toggle flow control if needed */
   {
      unsigned int unackCnt = tp->lastChunkIdSent - tp->lastChunkAckSeen;

      if ((unackCnt > TP_MAX_START_FLOW_CONTROL) && !tp->flowStopped) {
         DEBUG_MSG(("Starting flow control (%d unacknowledged chunks)",
                    unackCnt));
         tp->flowStopped = TRUE;
      } else if ((unackCnt < TP_MIN_END_FLOW_CONTROL) && tp->flowStopped) {
         DEBUG_MSG(("Ending flow control"));
         tp->flowStopped = FALSE;
         TunnelProxyFireSendNeeded(tp);
      }
   }

   /* Queue new ACK if we haven't sent one in a while */
   if (tp->lastChunkIdSeen - tp->lastChunkAckSent >= TP_MAX_UNACKNOWLEDGED) {
      TunnelProxySendChunk(tp, TP_CHUNK_TYPE_ACK, 0, NULL, NULL, 0);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyWriteNextOutChunk --
 *
 *       Serialize the next chunk in the outgoing chunk queue into the
 *       TunnelProxy's writeBuf.  If the chunk to be serialized is a Data
 *       chunk, its body is first base64 encoded.
 *
 *       If the outgoing chunk doesn't have an ackId field set, the next chunk
 *       on the incoming to-be-ACK's queue is used, and is freed.
 *
 *       Once processed the chunk is moved to the outgoing needs-ACK queue.
 *
 * Results:
 *       TRUE if a chunk was written successfully, FALSE otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyWriteNextOutChunk(TunnelProxy *tp,  // IN
                             gboolean httpChunked) // IN
{
   TPChunk *chunk = NULL;
   char *msg = NULL;
   GList *li;
   size_t msgLen = 0;

   g_assert(tp);

   for (li = g_queue_peek_head_link(tp->queueOut); li; li = li->next) {
      TPChunk *chunkIter = (TPChunk *)li->data;
      if (!tp->flowStopped || chunkIter->type != TP_CHUNK_TYPE_DATA) {
         chunk = chunkIter;
         break;
      }
   }
   if (!chunk) {
      return FALSE;
   }

   /*
    * Assign the next chunk ID if this is not an ACK or a resent chunk
    * following a reconnect.
    */
   if (chunk->chunkId == 0 && chunk->type != TP_CHUNK_TYPE_ACK) {
      chunk->chunkId = ++tp->lastChunkIdSent;
   }
   if (tp->lastChunkAckSent < tp->lastChunkIdSeen) {
      chunk->ackId = tp->lastChunkIdSeen;
      tp->lastChunkAckSent = chunk->ackId;
   }

   switch (chunk->type) {
   case TP_CHUNK_TYPE_MESSAGE: {
      char *hdr;
      int hdrLen = 0;
      if (!TunnelProxy_FormatMsg(&hdr, &hdrLen, "messageType=S", chunk->msgId,
                                 NULL)) {
         g_debug("Failed to create tunnel msg header chunkId=%d.",
                 chunk->chunkId);
         return FALSE;
      }
      msg = g_strdup_printf("M;%X;%.0X;%X;%.*s;%X;%.*s;",
                            chunk->chunkId, chunk->ackId,
                            hdrLen, hdrLen, hdr, chunk->len,
                            chunk->len, chunk->body);
      msgLen = strlen(msg);
      g_free(hdr);

      DEBUG_DATA(("SEND-MSG(id=%d, ack=%d, msgid=%s, length=%d): %.*s",
                  chunk->chunkId, chunk->ackId,
                  chunk->msgId,
                  chunk->len, chunk->len, chunk->body));
      break;
   }
   case TP_CHUNK_TYPE_DATA: {
      msg = g_strdup_printf("D;%X;%.0X;%X;%X;", chunk->chunkId,
                            chunk->ackId, chunk->channelId, chunk->len);
      msgLen = strlen(msg);

      msg = g_realloc(msg, msgLen + chunk->len + 1);
      memcpy(msg + msgLen, chunk->body, chunk->len);
      msg[msgLen + chunk->len] = ';';
      msgLen += chunk->len + 1;

      DEBUG_DATA(("SEND-DATA(id=%d, ack=%d, channel=%d, length=%d)",
                  chunk->chunkId, chunk->ackId, chunk->channelId, chunk->len));
      break;
   }
   case TP_CHUNK_TYPE_ACK:
      g_assert(chunk->ackId > 0);
      msg = g_strdup_printf("A;%X;", chunk->ackId);
      msgLen = strlen(msg);

      DEBUG_DATA(("SEND-ACK(ackId=%d)", chunk->ackId));
      break;
   default:
      g_assert_not_reached();
   }

   g_assert(msg);

   if (httpChunked) {
      char *chunkHdr = g_strdup_printf("%X\r\n", (int)msgLen);
      g_byte_array_append(tp->writeBuf, chunkHdr, strlen(chunkHdr));
      g_free(chunkHdr);
      g_byte_array_append(tp->writeBuf, msg, msgLen);
      g_byte_array_append(tp->writeBuf, "\r\n", 2);
   } else {
      g_byte_array_append(tp->writeBuf, msg, msgLen);
   }

   /*
    * Move outgoing Data/Message chunk to need-ACK outgoing list.
    * TunnelProxyHandleInChunk assumes queueOutNeedAck is sorted by
    * ascending chunk ID, so queue at the end.
    */
   g_queue_remove(tp->queueOut, chunk);
   g_queue_push_tail(tp->queueOutNeedAck, chunk);

   g_free(msg);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_HTTPSend --
 *
 *       Write outgoing chunk data to the buffer supplied.  The buffer is
 *       intended to be written to the tunnel server over HTTP.  The buffer
 *       contents are *not* separated by HTTP chunked encoding.
 *
 *       The data written from the writeBuf is removed from the front of the
 *       writeBuf and the amount of data written returned in the bufSize
 *       param.
 *
 *       TunnelProxyWriteNextOutChunk is called repeatedly to serialize any
 *       existing or newly queued outgoing chunks.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Many.
 *
 *-----------------------------------------------------------------------------
 */

void
TunnelProxy_HTTPSend(TunnelProxy *tp,  // IN
                     char *buf,        // IN/OUT
                     int *bufSize,     // IN/OUT
                     gboolean httpChunked) // IN
{
   g_assert(tp);
   g_assert(buf && *bufSize > 0);

   /*
    * If we don't do the HTTP chunked encoding ourselves, the caller has to,
    * so only serialize a single message at a time so the caller can chunk
    * encode it.
    */
   while (TunnelProxyWriteNextOutChunk(tp, httpChunked) && httpChunked) {
      /* Do nothing. */
   }

   *bufSize = MIN(tp->writeBuf->len, *bufSize);
   memcpy(buf, tp->writeBuf->data, *bufSize);

   if (*bufSize) {
      /* Shrink the front of the write buffer */
      g_byte_array_remove_range(tp->writeBuf, 0, *bufSize);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxy_HTTPSendNeeded --
 *
 *       Determine if TunnelProxy_HTTPSend should be called in order to
 *       serialize outgoing tunnel chunks, so as to be written over HTTP.
 *
 * Results:
 *       TRUE if there is chunk data to send, FALSE if there's nothing to send.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
TunnelProxy_HTTPSendNeeded(TunnelProxy *tp) // IN
{
   TPChunk *chunk = NULL;
   GList *li;

   g_assert(tp);

   for (li = g_queue_peek_head_link(tp->queueOut); li; li = li->next) {
      chunk = (TPChunk *)li->data;
      if (!tp->flowStopped || chunk->type != TP_CHUNK_TYPE_DATA) {
         return TRUE;
      }
   }
   return FALSE;
}


/*
 * Default Msg handler impls
 */

/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyEchoRequestCb --
 *
 *       ECHO_RQ tunnel msg handler.  Sends an ECHO_RP msg.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyEchoRequestCb(TunnelProxy *tp,   // IN
                         const char *msgId, // IN/UNUSED
                         const char *body,  // IN
                         int len,           // IN
                         void *userData)    // IN: not used
{
   TunnelProxy_SendMsg(tp, TP_MSG_ECHO_RP, NULL, 0);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyEchoReplyCb --
 *
 *       ECHO_RP tunnel msg handler.  Does nothing other than avoid "Unhandled
 *       message" in logs.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyEchoReplyCb(TunnelProxy *tp,   // IN
                       const char *msgId, // IN
                       const char *body,  // IN
                       int len,           // IN
                       void *userData)    // IN: not used
{
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyStopCb --
 *
 *       STOP tunnel msg handler.  Disconnects the TunnelProxy.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyStopCb(TunnelProxy *tp,   // IN
                  const char *msgId, // IN
                  const char *body,  // IN
                  int len,           // IN
                  void *userData)    // IN: not used
{
   TunnelProxyErr err;
   char *reason;

   TunnelProxy_ReadMsg(body, len, "reason=S", &reason, NULL);
   g_printerr("TUNNEL STOPPED: %s\n", reason);

   /* Reconnect secret isn't valid after a STOP */
   g_free(tp->reconnectSecret);
   tp->reconnectSecret = NULL;

   err = TunnelProxyDisconnect(tp, reason, TRUE, TRUE);
   g_assert(TP_ERR_OK == err);

   g_free(reason);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyAuthenticatedCb --
 *
 *       AUTHENTICATED tunnel msg handler.  Stores reconnection and timeout
 *       information in the TunnelProxy object.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyAuthenticatedCb(TunnelProxy *tp,   // IN
                           const char *msgId, // IN
                           const char *body,  // IN
                           int len,           // IN
                           void *userData)    // IN: not used
{
   char *capID = NULL;
   gboolean allowAutoReconnection = FALSE;

   /* Ignored body contents:
    *    "sessionTimeout" long, time until the session will die
    */
   if (!TunnelProxy_ReadMsg(body, len,
                            "allowAutoReconnection=B", &allowAutoReconnection,
                            "capID=S", &capID,
                            "lostContactTimeout=L", &tp->lostContactTimeout,
                            "disconnectedTimeout=L", &tp->disconnectedTimeout,
                            NULL)) {
      g_assert_not_reached();
   }

   if (tp->capID && strcmp(capID, tp->capID) != 0) {
      g_printerr("Tunnel authenticated capID \"%s\" does not match expected "
                 "value \"%s\".\n", capID, tp->capID);
   } else {
      tp->capID = capID;
      capID = NULL;
   }

   g_free(tp->reconnectSecret);
   tp->reconnectSecret = NULL;

   if (allowAutoReconnection &&
       !TunnelProxy_ReadMsg(body, len,
                            "reconnectSecret=S", &tp->reconnectSecret,
                            NULL)) {
      g_printerr("Tunnel automatic reconnect disabled: no reconnect secret in "
                 "auth_rp.\n");
   }

   /* Kick off echo & disconnect timeouts */
   TunnelProxyResetTimeouts(tp, TRUE);

   g_free(capID);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyReadyCb --
 *
 *       READY tunnel msg handler.  Just prints a message.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyReadyCb(TunnelProxy *tp,   // IN: not used
                   const char *msgId, // IN: not used
                   const char *body,  // IN: not used
                   int len,           // IN: not used
                   void *userData)    // IN: not used
{
   g_printerr("TUNNEL READY\n");
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxySysMsgCb --
 *
 *       SYSMSG tunnel msg handler.  Prints the system message to stdout.
 *
 *       XXX: Should do something better here, like notify the user.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxySysMsgCb(TunnelProxy *tp,   // IN/UNUSED
                    const char *msgId, // IN/UNUSED
                    const char *body,  // IN
                    int len,           // IN
                    void *userData)    // IN: not used
{
   char *msg;

   TunnelProxy_ReadMsg(body, len, "msg=S", &msg, NULL);
   g_printerr("TUNNEL SYSTEM MESSAGE: %s\n", msg ? msg : "<Invalid Message>");
   g_free(msg);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyErrorCb --
 *
 *       ERROR tunnel msg handler.  Prints the error and panics.
 *
 *       XXX: Should do something better here, like notify the user.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyErrorCb(TunnelProxy *tp,   // IN/UNUSED
                   const char *msgId, // IN/UNUSED
                   const char *body,  // IN
                   int len,           // IN
                   void *userData)    // IN: not used
{
   char *msg;

   TunnelProxy_ReadMsg(body, len, "msg=S", &msg, NULL);
   g_printerr("TUNNEL ERROR: %s\n", msg ? msg : "<Invalid Error>");
   g_free(msg);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyPleaseInitCb --
 *
 *       PLEASE_INIT tunnel msg handler.  Sends a START message in response
 *       containing the host's ip address, hostname, and time.
 *
 *       XXX: Need to figure out the host's IP address and hostname, or at
 *            least make a decent guess.
 *
 * Results:
 *       TRUE if the correlation-id matches that sent in INIT msg.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyPleaseInitCb(TunnelProxy *tp,   // IN
                        const char *msgId, // IN/UNUSED
                        const char *body,  // IN
                        int len,           // IN
                        void *userData)    // IN: not used
{
   /* Ignored body contents:
    *    "plugins" string array
    *    "criticalities" ?? string array
    */
   char *startBody = NULL;
   int startLen = 0;
   gint64 t1 = 0;

   g_assert(tp->hostIp && tp->hostAddr);

   {
      char *cid = NULL;
      TunnelProxy_ReadMsg(body, len, "cid=S", &cid, NULL);
      if (!cid || strcmp(cid, "1234") != 0) {
         g_printerr("Incorrect correlation-id in tunnel PLEASEINIT: %s.\n", cid);
         return FALSE;
      }
      g_free(cid);
   }

   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      t1 = tv.tv_sec * 1000;
      t1 += tv.tv_usec / 1000;
   }

   TunnelProxy_FormatMsg(&startBody, &startLen,
                         "ipaddress=S", tp->hostIp,
                         "hostaddress=S", tp->hostAddr,
                         "capID=S", tp->capID ? tp->capID : "",
                         "type=S", "C", // "simple" C client
                         "t1=L", t1, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_START, startBody, startLen);
   g_free(startBody);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyRaiseReplyCb --
 *
 *       RAISE_RP tunnel msg handler.  If the message does not contain an
 *       error, we start up socket channel IO for the channel id referred to
 *       by chanId in the message by calling TunnelProxySocketRecvCb,
 *       otherwise calls TunnelProxy_CloseChannel to teardown the
 *       server-disallowed socket.
 *
 * Results:
 *       TRUE if IO was started or the channel was closed, FALSE if the
 *       channel is unknown.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyRaiseReplyCb(TunnelProxy *tp,   // IN
                        const char *msgId, // IN
                        const char *body,  // IN
                        int len,           // IN
                        void *userData)    // IN: not used
{
   char *problem = NULL;
   int chanId = 0;
   TPChannel *channel = NULL;
   TunnelProxyErr err;
   GList *li;

   if (!TunnelProxy_ReadMsg(body, len, "chanID=I", &chanId, NULL)) {
      g_assert_not_reached();
   }

   for (li = tp->channels; li; li = li->next) {
      TPChannel *chanIter = (TPChannel *)li->data;
      if (chanIter->channelId == chanId) {
         channel = chanIter;
         break;
      }
   }
   if (!channel) {
      g_debug("Invalid channel \"%d\" in raise reply.", chanId);
      return FALSE;
   }

   TunnelProxy_ReadMsg(body, len, "problem=E", &problem, NULL);

   if (problem) {
      g_debug("Error raising channel \"%d\": %s", chanId, problem);

      err = TunnelProxy_CloseChannel(tp, channel->channelId);
      g_assert(TP_ERR_OK == err);
   } else {
      /* Kick off channel reading */
      TunnelProxy_AddPoll(TunnelProxySocketRecvCb, channel, channel->fd);
   }

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyListenSocket --
 *
 *      Create a listening GIOChannel on the given IP address and port.
 *
 * Results:
 *      A new GIOChannel or NULL on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static int
TunnelProxyListenSocket(const char *ipStr, // IN
                        int port)          // IN
{
   int fd;
   in_addr_t ipAddr;
   struct sockaddr_in local_addr = { 0 };
   int nodelay = 1;
   long flags;

   g_debug("Creating new listening socket on port %d", port);

   ipAddr = inet_addr(ipStr);
   if (ipAddr == INADDR_NONE) {
      g_printerr("Could not convert address: %s\n", ipStr);
      return -1;
   }

   fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0) {
      g_printerr("Could not create socket: %s\n", strerror(errno));
      return -1;
   }

#ifndef _WIN32
   /*
    * Don't ever use SO_REUSEADDR on Windows; it doesn't mean what you think
    * it means.
    */
   {
      int reuse = port != 0;
      if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     (const void *)&reuse, sizeof(reuse)) != 0) {
         g_printerr("Could not set SO_REUSEADDR: %s\n", strerror(errno));
      }
   }
#endif

#ifdef _WIN32
   /*
    * Always set SO_EXCLUSIVEADDRUSE on Windows, to prevent other applications
    * from stealing this socket. (Yes, Windows is that stupid).
    */
   {
      int exclusive = 1;
      if (setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     (const void *) &exclusive, sizeof(exclusive)) != 0) {
         g_printerr("Could not set SO_EXCLUSIVEADDRUSE, error %d: %s\n",
                    errno, strerror(errno));
      }
   }
#endif

   local_addr.sin_family = AF_INET;
   local_addr.sin_addr.s_addr = ipAddr;
   local_addr.sin_port = htons(port);

   if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr))) {
      g_printerr("Could not bind socket: %s\n", strerror(errno));
      close(fd);
      return -1;
   }

   if (listen(fd, 5)) {
      g_printerr("Could not listen on socket: %s\n", strerror(errno));
      close(fd);
      return -1;
   }

   setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
              (const void *)&nodelay, sizeof(nodelay));

#ifdef __MINGW32__
   SET_NONBLOCKING(fd);
#else
#ifdef O_NONBLOCK
   flags = O_NONBLOCK;
#else
   flags = O_NDELAY;
#endif
#if 1
   fcntl(fd, F_SETFL, flags);
#endif
#endif

   return fd;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyListenRequestCb --
 *
 *       LISTEN_RQ tunnel msg handler.  Creates a local listener socket, and a
 *       TPListener object to manage it.  Sends LISTEN_RP message in reply if
 *       we were able to listen successfully.  Calls the TunnelProxy's
 *       listenerCb to notify of a new listener creation.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyListenRequestCb(TunnelProxy *tp,   // IN
                           const char *msgId, // IN/UNUSED
                           const char *body,  // IN
                           int len,           // IN
                           void *userData)    // IN: not used
{
   int bindPort = -1;
   char *serverHost = NULL;
   int serverPort = 0;
   char *portName;
   int maxConns;
   int cid;
   char *bindAddr = NULL;
   char *reply = NULL;
   int replyLen = 0;
   int fd = -1;
   TPListener *listener = NULL;
   char *problem = NULL;

   if (!TunnelProxy_ReadMsg(body, len,
                            "clientPort=I", &bindPort,
                            "serverHost=S", &serverHost,
                            "serverPort=I", &serverPort,
                            "portName=S", &portName,
                            "maxConnections=I", &maxConns,
                            "cid=I", &cid, NULL)) {
      g_assert_not_reached();
   }

   if (bindPort == -1) {
      bindPort = INADDR_ANY;
   }

   /* clientHost is often null, so parse it optionally */
   TunnelProxy_ReadMsg(body, len, "clientHost=S", &bindAddr);
   if (!bindAddr) {
      bindAddr = g_strdup("127.0.0.1");
   }

   /* Create the listener early, so it can be the ConnectCb user data */
   listener = g_new0(TPListener, 1);
   fd = TunnelProxyListenSocket(bindAddr, bindPort);
   if (fd < 0) {
      g_debug("Error creating new listener \"%s\" on %s:%d to server %s:%d",
              portName, bindAddr, bindPort, serverHost, serverPort);
      goto error;
   }

   TunnelProxy_AddPoll(TunnelProxySocketConnectCb, listener, listener->fd);

   if (bindPort == 0) {
      /* Find the local port we've bound. */
      struct sockaddr addr = { 0 };
      socklen_t addrLen = sizeof(addr);

      if (getsockname(fd, &addr, &addrLen) < 0) {
         g_assert_not_reached();
      }

      bindPort = ntohs(((struct sockaddr_in*) &addr)->sin_port);
   }
   g_assert(bindPort > 0);

   if (tp->listenerCb && !tp->listenerCb(tp, portName, bindAddr,
                                         bindPort, tp->listenerCbData)) {
      TunnelProxy_RemovePoll(TunnelProxySocketConnectCb, listener);

      close(fd);

      g_debug("Rejecting new listener \"%s\" on %s:%d to server %s:%d.",
              portName, bindAddr, bindPort, serverHost, serverPort);

      problem = g_strdup("User Rejected");
      goto error;
   }

   g_debug("Creating new listener \"%s\" on %s:%d to server %s:%d.",
           portName, bindAddr, bindPort, serverHost, serverPort);

   strncpy(listener->portName, portName, TP_PORTNAME_MAXLEN);
   listener->port = bindPort;
   listener->fd = fd;
   listener->singleUse = maxConns == 1;
   listener->tp = tp;

   tp->listeners = g_list_append(tp->listeners, listener);

   TunnelProxy_FormatMsg(&reply, &replyLen,
                         "cid=I", cid,
                         "portName=S", portName,
                         "clientHost=S", bindAddr,
                         "clientPort=I", bindPort, NULL);

exit:
   TunnelProxy_SendMsg(tp, TP_MSG_LISTEN_RP, reply, replyLen);

   g_free(bindAddr);
   g_free(serverHost);
   g_free(portName);
   g_free(reply);

   return TRUE;

error:
   g_assert(problem && !reply);
   TunnelProxy_FormatMsg(&reply, &replyLen, "cid=I", cid, "problem=E", problem,
                         NULL);
   g_free(problem);
   g_free(listener);
   goto exit;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyUnlistenRequestCb --
 *
 *       UNLISTEN_RQ tunnel msg handler.  Looks up the portName provided in
 *       the message and calls TunnelProxy_CloseListener to close the listener
 *       and all its socket channels.  Sends an UNLISTEN_RP to verify the
 *       close completed successfully.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyUnlistenRequestCb(TunnelProxy *tp,   // IN
                             const char *msgId, // IN
                             const char *body,  // IN
                             int len,           // IN
                             void *userData)    // IN: not used
{
   char *portName = NULL;
   char *reply = NULL;
   int replyLen = 0;

   if (!TunnelProxy_ReadMsg(body, len, "portName=S", &portName, NULL)) {
      g_assert_not_reached();
   }

   if (!portName || TP_ERR_OK != TunnelProxy_CloseListener(tp, portName)) {
      TunnelProxy_FormatMsg(&reply, &replyLen, "problem=E", "Invalid portName",
                            NULL);
   }

   TunnelProxy_SendMsg(tp, TP_MSG_UNLISTEN_RP, reply, replyLen);

   g_free(portName);
   g_free(reply);
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyLowerCb --
 *
 *       LOWER tunnel msg handler.  Looks up the channel for the channel ID
 *       provided in the message and calls TunnelProxy_CloseChannel to close
 *       the channel and its socket.
 *
 * Results:
 *       TRUE.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelProxyLowerCb(TunnelProxy *tp,   // IN
                   const char *msgId, // IN
                   const char *body,  // IN
                   int len,           // IN
                   void *userData)    // IN: not used
{
   int chanId = 0;
   TunnelProxyErr err;

   if (!TunnelProxy_ReadMsg(body, len, "chanID=I", &chanId, NULL)) {
      g_assert_not_reached();
   }

   g_printerr("Tunnel requested socket channel close (chanID: %d)\n", chanId);
   err = TunnelProxy_CloseChannel(tp, chanId);
   if (err != TP_ERR_OK) {
       g_printerr("Error closing socket channel %d: %d\n", chanId, err);
   }

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyResetTimeouts --
 *
 *       Cancel pending echo and lost contact timeouts and requeues them if
 *       the TunnelProxy has a lostContactTimeout as received in the
 *       AUTHENTICATED msg.
 *
 *       The echo timeout is 1/3 the time of the lost contact timeout, to
 *       mimic wswc_tunnel.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Poll timeouts removed/added.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyResetTimeouts(TunnelProxy *tp,  // IN
                         gboolean requeue) // IN: restart timeouts
{
   g_assert(tp);

   Poll_CB_RTimeRemove(TunnelProxyLostContactTimeoutCb, tp, FALSE);
   Poll_CB_RTimeRemove(TunnelProxyEchoTimeoutCb, tp, TRUE);

   if (requeue && tp->lostContactTimeout > 0) {
      Poll_CB_RTime(TunnelProxyLostContactTimeoutCb,
                    tp, tp->lostContactTimeout * 1000, FALSE, NULL);
      Poll_CB_RTime(TunnelProxyEchoTimeoutCb,
                    tp, tp->lostContactTimeout * 1000 / 3, TRUE, NULL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyEchoTimeoutCb --
 *
 *       Echo poll timeout callback.  Sends an ECHO_RQ with a "now" field
 *       containing the current time in millis.
 *
 *       NOTE: There is no ECHO_RP message handler currently, as the handler
 *       in wswc_tunnel does nothing.
 *
 * Results:
 *       None.
 *
 * Side effects:
 *       Sends a ECHO_RQ tunnel msg.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelProxyEchoTimeoutCb(void *userData) // IN: TunnelProxy
{
   TunnelProxy *tp = userData;
   char *req = NULL;
   int reqLen = 0;
   gint64 now = 0;

   g_assert(tp);
   tp->echoTimeoutId = 0;

   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      now = tv.tv_sec * 1000;
      now += tv.tv_usec / 1000;
   }

   TunnelProxy_FormatMsg(&req, &reqLen, "now=L", now, NULL);
   TunnelProxy_SendMsg(tp, TP_MSG_ECHO_RQ, req, reqLen);
   g_free(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelProxyLostContactTimeoutCb --
 *
 *       Lost contact timeout callback.  Calls TunnelProxyDisconnect to notify
 *       the client of the disconnect, and allows reconnection without
 *       destroying our listening ports.
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
TunnelProxyLostContactTimeoutCb(void *userData) // IN: TunnelProxy
{
   TunnelProxy *tp = userData;

   g_assert(tp);
   tp->lostContactTimeoutId = 0;

   TunnelProxyDisconnect(tp, _("Client disconnected following no activity"),
                         FALSE, TRUE);
}
