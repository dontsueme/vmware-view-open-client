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
 * tunnelMain.c --
 *
 *      Multi-channel socket proxy over HTTP with control messages, lossless
 *      reconnect, heartbeats, etc.
 */


#include <glib/gi18n.h>
#ifndef __MINGW32__
#include <arpa/inet.h>  /* For inet_ntoa */
#include <errno.h>
#include <netdb.h>      /* For getnameinfo */
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h> /* For getsockname */
#else
#include "ws2tcpip.h"
#include "winsockerr.h"
#include "mingw32Util.h"
#endif
#include <locale.h>     /* For setlocale */
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "tunnelProxy.h"
#include "cdkProxy.h"
#include "cdkUrl.h"
#include "poll.h"
#include "loglevel_tools.h"

#if 0
#define DEBUG_IO(x) g_debug x
#else
#define DEBUG_IO(x)
#endif

#define APPNAME "vmware-view-tunnel"
#define TMPBUFSIZE 1024 * 16 /* arbitrary */
#define BLOCKING_TIMEOUT_MS 1000 * 3 /* 3 seconds, arbitrary */

static char *gServerArg = NULL;
static char *gConnectionIdArg = NULL;

static TunnelProxy *gTunnelProxy = NULL;
static int gFd = -1;
static gboolean gRecvHeaderDone = FALSE;
static GByteArray *gRecvBuf = NULL;

static SSL_CTX *gSslCtx = NULL;
static SSL *gSsl = NULL;
static BIO *gInBio = NULL;
static BIO *gOutBio = NULL;

static void TunnelConnect(void);
static void TunnelSocketConnectCb(int fd, void *userData);
static void TunnelSocketProxyRecvCb(void *);
static void TunnelSocketRecvCb(void *);
static void TunnelSocketSslHandshakeRecvCb(void *);

LogLevelState logLevelState;
const int8 *logLevelPtr = logLevelState.initialLevels;

/* Log everything */
int _loglevel_offset_user = 0;

void
Log(const char *fmt, // IN
    ...)             // IN
{
   va_list args;
   va_start(args, fmt);
   g_logv(NULL, G_LOG_LEVEL_DEBUG, fmt, args);
   va_end(args);
}


void
Warning(const char *fmt, // IN
        ...)             // IN
{
   va_list args;
   va_start(args, fmt);
   g_logv(NULL, G_LOG_LEVEL_WARNING, fmt, args);
   va_end(args);
}


void
Panic(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   g_logv(NULL, G_LOG_LEVEL_ERROR, fmt, args);
   va_end(args);
   /*
    *declaration says this function shouldn't return, so make the
    * compiler believe this
    */
   while (TRUE) { }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelDisconnectCb --
 *
 *      TunnelProxy disconnected callback.  If there is a reconnect secret,
 *      calls TunnelConnect attempt reconnect, otherwise exits with error.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May call TunnelConnect.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelDisconnectCb(TunnelProxy *tp,             // IN
                   const char *reconnectSecret, // IN
                   const char *reason,          // IN
                   void *userData)              // IN: not used
{
   TunnelProxy_RemovePoll(TunnelSocketProxyRecvCb, GINT_TO_POINTER(gFd));
   TunnelProxy_RemovePoll(TunnelSocketRecvCb, GINT_TO_POINTER(gFd));
   TunnelProxy_RemovePoll(TunnelSocketSslHandshakeRecvCb,
                          GINT_TO_POINTER(gFd));

   close(gFd);
   gFd = -1;

   gRecvHeaderDone = FALSE;
   if (reconnectSecret) {
      g_printerr("TUNNEL RESET: %s\n", reason ? reason : "Unknown reason");
      TunnelConnect();
   } else if (reason) {
      g_printerr("TUNNEL DISCONNECT: %s\n", reason);
      exit(1);
   } else {
      g_printerr("TUNNEL EXIT\n");
      exit(0);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketRead --
 *
 *      Utility to read all pending data from an async socket non-blocking,
 *      first prepending buf, and append it all to a DynBuf.
 *
 * Results:
 *      Byte count of buf + newly read bytes that were appended to recvBuf,
 *      or -1 if read failed.
 *
 * Side effects:
 *      Calls AsyncSocket_RecvBlocking with 0 timeout, TunnelDisconnectCb on
 *      read error.
 *
 *-----------------------------------------------------------------------------
 */

static int
TunnelSocketRead(int fd,              // IN
                 SSL *ssl,            // IN
                 BIO *bio,            // IN
                 GByteArray *dynBuf)  // IN
{
   int origBufSize = dynBuf ? dynBuf->len : 0;
   char tmpBuf[TMPBUFSIZE];
   ssize_t recvLen;
   char *reason = NULL;

   do {
#ifdef __MINGW32__
      recvLen = recv(fd, tmpBuf, sizeof(tmpBuf), 0);
      if (recvLen == SOCKET_ERROR) {
         if (WSAGetLastError() == WSAEWOULDBLOCK) {
            recvLen = -1;
            errno = EAGAIN;
         } else {
            reason = g_strdup_printf(
               _("Error reading from tunnel HTTP socket: %d"),
               WSAGetLastError());
            recvLen = 0;
         }
      }
#else
      recvLen = read(fd, tmpBuf, sizeof(tmpBuf));
#endif
      switch (recvLen) {
      case -1:
         switch (errno) {
         case EAGAIN:
         case EINTR:
            break;
         default:
            reason = g_strdup_printf(
               _("Error reading from tunnel HTTP socket: %s"), strerror(errno));
            break;
         }
         if (!reason) {
            continue;
         }
         /* fall through */
      case 0:
         TunnelDisconnectCb(gTunnelProxy, NULL, reason, NULL);
         g_free(reason);
         return -1;
      default:
         if (bio) {
            int sslWritten = 0;
            sslWritten = BIO_write(bio, tmpBuf, recvLen);
            DEBUG_IO(("Wrote %d bytes to BIO", sslWritten));
            g_assert(sslWritten == recvLen);
         } else {
            g_byte_array_append(dynBuf, tmpBuf, recvLen);
         }
         break;
      }
   } while (recvLen > 0 || errno == EINTR);

   if (bio) {
      BIO_flush(bio);
   }

   if (ssl) {
      while (TRUE) {
         int readLen = SSL_read(ssl, tmpBuf, sizeof(tmpBuf));
         DEBUG_IO(("Read %d bytes from SSL", readLen));
         if (readLen > 0) {
            g_byte_array_append(dynBuf, tmpBuf, readLen);
         } else if (readLen == 0) {
            TunnelDisconnectCb(gTunnelProxy, NULL,
                               _("SSL connection was shut down while reading"), NULL);
            return -1;
         } else {
             readLen = SSL_get_error(ssl, readLen);
             if (readLen == SSL_ERROR_WANT_READ) {
                 break;
             }
             // XXX: Handle errors.
             ERR_print_errors_fp(stderr);
             g_printerr("SSL error while reading: %d\n", readLen);
             g_assert_not_reached();
         }
      }
   }

   return dynBuf ? dynBuf->len - origBufSize : 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketWrite --
 *
 *      Write bytes to our IO channel.
 *
 *      If ssl is not NULL, they will be written to the SSL object.
 *
 *      If bio is not NULL, bytes will be read from it and written to
 *      the IO channel.
 *
 *      If len is < 0, a NIL-terminated string is assumed.
 *
 *      Note that this does a blocking/buffered write.
 *
 * Results:
 *      Number of bytes written.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static int
TunnelSocketWrite(int fd,              // IN
                  SSL *ssl,            // IN/OPT
                  BIO *bio,            // IN/OPT
                  const char *bytes,   // IN/OPT
                  gssize len)          // IN/OPT
{
   GByteArray *buf = NULL;
   char *reason = NULL;

   if (len < 0) {
      len = bytes ? strlen(bytes) : 0;
   }

   if (ssl && bytes) {
      int sslWritten = 0;
      DEBUG_IO(("Writing %d bytes to ssl...", (int)len));
      sslWritten = SSL_write(ssl, bytes, len);
      if (sslWritten > 0) {
         g_assert(sslWritten == len);
      } else if (sslWritten == 0) {
         TunnelDisconnectCb(gTunnelProxy, NULL,
                            _("SSL connect was shut down while writing"),
                            NULL);
         return -1;
      } else {
         // XXX: handle errors.
         ERR_print_errors_fp(stderr);
         g_assert_not_reached();
      }
   }

   if (bio) {
      char tmpBuf[TMPBUFSIZE];
      int bytesRead = 0;
      buf = g_byte_array_new();
      while (TRUE) {
         bytesRead = BIO_read(bio, tmpBuf, sizeof(tmpBuf));
         DEBUG_IO(("Read %d bytes from BIO.", bytesRead));
         if (bytesRead == 0) {
            break;
         } else if (bytesRead < 0) {
            if (BIO_should_retry(bio)) {
               break;
            }
            // XXX: handle error.
            ERR_print_errors_fp(stderr);
            g_assert_not_reached();
         }
         g_byte_array_append(buf, tmpBuf, bytesRead);
      }
      bytes = buf->data;
      len = buf->len;
   }

   if (bytes) {
      ssize_t bytesWritten;
      size_t toWrite = len;
      while (toWrite > 0) {
#ifdef __MINGW32__
         bytesWritten = send(fd, bytes, toWrite, 0);
         if (bytesWritten == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
               errno = EWOULDBLOCK;
            } else {
               reason = g_strdup_printf(
                  _("Error writing to tunnel HTTP socket: %d\n"),
                  WSAGetLastError());
               TunnelDisconnectCb(gTunnelProxy, NULL, reason, NULL);
               return -1;
            }
            bytesWritten = -1;
         }
#else
         bytesWritten = write(fd, bytes, toWrite);
#endif
         if (bytesWritten < 0) {
            if (errno != EWOULDBLOCK &&
                errno != EAGAIN &&
                errno != EINTR) {
               reason = g_strdup_printf(
                  _("Error writing to tunnel HTTP socket: %s\n"), strerror(errno));
               TunnelDisconnectCb(gTunnelProxy, NULL, reason, NULL);
               return -1;
            }
         } else {
            toWrite -= bytesWritten;
            bytes += bytesWritten;
         }
      }
   }

   if (buf) {
      g_byte_array_free(buf, TRUE);
   }

   return len;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketParseHeader --
 *
 *      Simple HTTP header parsing.  Just looks in the DynBuf for the "\r\n\r\n"
 *      that terminates an HTTP header from the body. If found, the header is
 *      removed from the front of the DynBuf.
 *
 *      XXX: Parse response header, instead of assuming server/proxy will kill
 *      connection on failure.
 *
 * Results:
 *      True if headers have been received, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelSocketParseHeader(GByteArray *recvBuf) // IN
{
   char *dataStart;

   if (recvBuf->len == 0) {
      return FALSE;
   }

   /* Look for end of header */
   dataStart = g_strstr_len(recvBuf->data, recvBuf->len, "\r\n\r\n");
   if (!dataStart) {
      return FALSE;
   }

   /* Remove header from beginning of recvBuf */
   g_byte_array_remove_range(recvBuf, 0, 4 + dataStart - (char *)recvBuf->data);

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketSslHandshakeRecvCb --
 *
 *      Input callback while we are doing an ssl handshake.  This is
 *      called when we have data from the server to read; we read it
 *      into the ssl bio, and then continue with the handshake.
 *
 * Results:
 *      FALSE to remove this handler (it's added by
 *      TunnelSocketConnectCb if needed).
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelSocketSslHandshakeRecvCb(void *userData)      // IN
{
   int fd = GPOINTER_TO_INT(userData);

   /*
    * Don't pass gSsl here since we haven't done the handshake yet.
    */
   if (TunnelSocketRead(fd, NULL, gInBio, NULL) >= 0) {
      // Call the connect cb again to continue the handshake.
      TunnelSocketConnectCb(fd, NULL);
   } else {
      TunnelProxy_AddPoll(TunnelSocketSslHandshakeRecvCb, userData, fd);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketRecvCb --
 *
 *      AsyncSocket data received callback.  Reads available data from the
 *      tunnel socket, and pushes into the TunnelProxy using
 *      TunnelProxy_HTTPRecv.  Ignores response headers.
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
TunnelSocketRecvCb(void *userData)      // IN
{
   int fd = GPOINTER_TO_INT(userData);

   if (TunnelSocketRead(fd, gSsl, gInBio, gRecvBuf) < 0) {
      return;
   }

   if (!gRecvHeaderDone) {
      gRecvHeaderDone = TunnelSocketParseHeader(gRecvBuf);
   }

   if (gRecvHeaderDone && gRecvBuf->len > 0) {
      TunnelProxy_HTTPRecv(gTunnelProxy, (char*)gRecvBuf->data,
                           gRecvBuf->len, TRUE);

      /* Reset recvBuf for next read */
      g_byte_array_set_size(gRecvBuf, 0);
   }

   TunnelProxy_AddPoll(TunnelSocketRecvCb, userData, fd);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketProxyRecvCb --
 *
 *      AsyncSocket data received callback, used during initial proxy server
 *      CONNECT setup.  Reads available data from the tunnel socket, and looks
 *      for the end of the HTTP header.  If found, starts tunnel endpoint POST
 *      request, otherwise, calls AsyncSocket_Recv to reinvoke this callback.
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
TunnelSocketProxyRecvCb(void *userData)      // IN
{
   int fd = GPOINTER_TO_INT(userData);

   if (TunnelSocketRead(fd, gSsl, gInBio, gRecvBuf) < 0) {
      return;
   }

   if (!TunnelSocketParseHeader(gRecvBuf)) {
      TunnelProxy_AddPoll(TunnelSocketProxyRecvCb, userData, fd);
      return;
   }

   g_debug("Connected to proxy server; initiating proxied connection...");

   /* Proxy portion of connect is done.  Connect using normal path. */
   g_byte_array_free(gRecvBuf, TRUE);
   gRecvBuf = NULL;
   TunnelSocketConnectCb(fd, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSendNeededCb --
 *
 *      TunnelProxy send needed callback.  Fetches the available HTTP chunk data
 *      and performs a blocking sends over the AsyncSocket.
 *
 *      XXX: Use async send, and heap-allocated send buffer?
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
TunnelSendNeededCb(TunnelProxy *tp, // IN
                   void *userData)  // IN: not used
{
   char sendBuf[TMPBUFSIZE];
   int sendSize = TMPBUFSIZE;

   do {
      sendSize = TMPBUFSIZE;
      TunnelProxy_HTTPSend(gTunnelProxy, sendBuf, &sendSize, TRUE);
      if (sendSize == 0) {
         break;
      }
   } while (0 < TunnelSocketWrite(gFd, gSsl, gOutBio, sendBuf, sendSize));
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketSslHandshake --
 *
 *      Attempt to complete an SSL handshake.  If it's not ready, do
 *      some IO.
 *
 * Results:
 *      TRUE if the handshake was completed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TunnelSocketSslHandshake(int fd,   /* IN */
                         SSL *ssl) /* IN */
{
   int rv;

   g_assert(ssl);

  doHandshake:
   rv = SSL_do_handshake(ssl);
   switch (rv) {
   case 1:
      // Success!
      break;
   case 0:
      // XXX: handle error.
      ERR_print_errors_fp(stderr);
      g_assert_not_reached();
      break;
   default:
      g_assert(rv < 0);
      switch (SSL_get_error(ssl, rv)) {
      case SSL_ERROR_WANT_WRITE:
         TunnelSocketWrite(fd, NULL, gOutBio, NULL, 0);
         goto doHandshake;
      case SSL_ERROR_WANT_READ:
         /*
          * The SSL_do_handshake call above may have written data to
          * our output bio, so we should send that along to the
          * server.
          */
         TunnelSocketWrite(fd, NULL, gOutBio, NULL, 0);
         DEBUG_IO(("Waiting for input..."));
         TunnelProxy_AddPoll(TunnelSocketSslHandshakeRecvCb, GINT_TO_POINTER(fd),
                        fd);
         break;
      default:
         g_printerr("Unhandled SSL handshake error: %d\n", rv);
         ERR_print_errors_fp(stderr);
         exit(1);
      }
   }
   return rv == 1;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketConnectCb --
 *
 *      AsyncSocket connection callback.  Converts the socket to SSL, if the URL
 *      passed on the command line is HTTPS, posts a simple HTTP1.1 request
 *      header, sets up socket read IO handler, and tells the TunnelProxy it is
 *      now connected.
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
TunnelSocketConnectCb(int fd,              // IN
                      void *userData)      // IN: not used
{
   TunnelProxyErr err = TP_ERR_OK;
   char *request = NULL;
   char *serverUrl = NULL;
   char *host = NULL;
   unsigned short port = 0;
   char *path = NULL;
   gboolean secure = FALSE;
   const char *hostIp = NULL;
   char hostName[1024];
   gsize bytes_written = 0;

   serverUrl = TunnelProxy_GetConnectUrl(gTunnelProxy, gServerArg);
   if (!CdkUrl_Parse(serverUrl, NULL, &host, &port, &path, &secure)) {
      g_assert_not_reached();
   }

   /* Establish SSL, but don't enforce the cert */
   if (secure) {
      if (!gSslCtx) {
         SSL_load_error_strings();
         ERR_load_BIO_strings();
         SSL_library_init();
         gSslCtx = SSL_CTX_new(TLSv1_client_method());
         if (!gSslCtx) {
            ERR_print_errors_fp(stderr);
            exit(1);
         }
      }

      if (!gSsl) {
         g_assert(!gInBio);
         g_assert(!gOutBio);

         gInBio = BIO_new(BIO_s_mem());
         gOutBio = BIO_new(BIO_s_mem());

         gSsl = SSL_new(gSslCtx);
         SSL_set_mode(gSsl, SSL_MODE_AUTO_RETRY);
         SSL_set_bio(gSsl, gInBio, gOutBio);
         SSL_set_connect_state(gSsl);
      }

      if (!TunnelSocketSslHandshake(fd, gSsl)) {
         DEBUG_IO(("%s: deferring request as handshake is still pending.", __FUNCTION__));
         goto out;
      }
   } else {
      g_assert(!gSslCtx);
      g_assert(!gSsl);
      g_assert(!gInBio);
      g_assert(!gOutBio);
   }

   request = g_strdup_printf(
      "POST %s HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Accept: text/*, application/octet-stream\r\n"
      "User-agent: Mozilla/4.0 (compatible; MSIE 6.0)\r\n"
      "Pragma: no-cache\r\n"
      "Connection: Keep-Alive\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Cache-Control: no-cache, no-store, must-revalidate\r\n"
      "\r\n", path, host, port);

   bytes_written = TunnelSocketWrite(fd, gSsl, gOutBio, request, -1);
   if (bytes_written <= 0) {
      exit(1);
   }

   /* Kick off channel reading */
   gRecvBuf = g_byte_array_new();
   TunnelProxy_AddPoll(TunnelSocketRecvCb, GINT_TO_POINTER(fd), fd);

   {
      /* Find the local address */
      struct sockaddr addr = { 0 };
      socklen_t addrLen = sizeof(addr);
      int gaiErr;

      if (getsockname(fd, &addr, &addrLen) < 0) {
         g_assert_not_reached();
      }

      hostIp = inet_ntoa(((struct sockaddr_in*) &addr)->sin_addr);
      if (!hostIp) {
         g_assert_not_reached();
      }

      gaiErr = getnameinfo(&addr, addrLen, hostName, sizeof(hostName), NULL, 0,
                           0);
      if (gaiErr < 0) {
         g_printerr("Unable to lookup name for localhost address '%s': %s.\n",
                    hostIp, gai_strerror(gaiErr));
         strcpy(hostName, hostIp);
      }
   }

   err = TunnelProxy_Connect(gTunnelProxy, hostIp, hostName,
                             TunnelSendNeededCb, NULL,
                             TunnelDisconnectCb, NULL);
   g_assert(err == TP_ERR_OK);

  out:
   g_free(serverUrl);
   g_free(host);
   g_free(path);
   g_free(request);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelSocketProxyConnectCb --
 *
 *      AsyncSocket connection callback for the proxy server.  Sends a simple
 *      HTTP1.1 CONNECT request header, calls TunnelSocketProxyRecvCb to read
 *      the proxy response header.
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
TunnelSocketProxyConnectCb(int fd,         /* IN */
                           void *userData) /* IN */
{
   char *request;
   char *serverUrl;
   char *host = NULL;
   unsigned short port = 0;
   gsize bytes_written = 0;

   serverUrl = TunnelProxy_GetConnectUrl(gTunnelProxy, gServerArg);
   if (!CdkUrl_Parse(serverUrl, NULL, &host, &port, NULL, NULL)) {
      g_assert_not_reached();
   }

   request = g_strdup_printf(
      "CONNECT %s:%d HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "User-agent: Mozilla/4.0 (compatible; MSIE 6.0)\r\n"
      "Proxy-Connection: Keep-Alive\r\n"
      "Content-Length: 0\r\n"
      "\r\n", host, port, host, port);

   bytes_written = TunnelSocketWrite(fd, gSsl, gOutBio, request, -1);
   if (bytes_written <= 0) {
      exit(1);
   }

   /* Kick off channel reading */
   gRecvBuf = g_byte_array_new();
   TunnelProxy_AddPoll(TunnelSocketProxyRecvCb, GINT_TO_POINTER(fd), fd);

   g_free(serverUrl);
   g_free(host);
   g_free(request);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelConnectSocket --
 *
 *      Create a GIOChannel connected to a given hostname and port.
 *
 * Results:
 *      A new GIOChannel or NULL if there was an error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static int
TunnelConnectSocket(const char *hostname, // IN
                    int port)             // IN
{
   struct addrinfo *addrs = NULL;
   struct addrinfo *addr = NULL;
   struct addrinfo hints = { 0, 0, SOCK_STREAM };
   int err = 0;
   int fd = -1;
   char *portStr = NULL;

   portStr = g_strdup_printf("%d", port);
   err = getaddrinfo(hostname, portStr, &hints, &addrs);
   g_free(portStr);

   if (err) {
      g_printerr("Could not resolve %s:%d: %s\n", hostname, port,
                 gai_strerror(err));
      return -1;
   }

   for (addr = addrs; addr; addr = addr->ai_next) {
      if (addr->ai_canonname) {
         g_debug("Connecting to %s:%d...", addr->ai_canonname, port);
      }

      fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
      if (fd < 0) {
         g_printerr("Could not create socket: %s\n", strerror(errno));
         continue;
      }

      if (connect(fd, addr->ai_addr, addr->ai_addrlen)) {
         g_printerr("Could not connect socket: %s\n", strerror(errno));
         close(fd);
         fd = -1;
         continue;
      }

      break;
   }

   freeaddrinfo(addrs);
   if (fd >= 0) {
      int nodelay = 1;
      long flags;
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
   }
   return fd;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelConnect --
 *
 *      Create an AsyncSocket and start the connection process for the server
 *      URL passed on the command line.  Will connect to the environment's
 *      http_proxy or https_proxy if set (depending on the protocol of the
 *      server URL).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Created socket is stored in the gAsock global.
 *
 *-----------------------------------------------------------------------------
 */

static void
TunnelConnect(void)
{
   char *http_proxy = NULL;
   CdkProxyType proxyType;
   const char *host;
   unsigned short port;
   char *serverUrl = NULL;
   char *serverProto = NULL;
   char *serverHost = NULL;
   unsigned short serverPort = 0;
   gboolean serverSecure = FALSE;
   char *proxyHost = NULL;
   unsigned short proxyPort = 0;

   g_assert(gFd == -1);
   g_assert(!gRecvHeaderDone);

   serverUrl = TunnelProxy_GetConnectUrl(gTunnelProxy, gServerArg);
   if (!CdkUrl_Parse(serverUrl, &serverProto, &serverHost, &serverPort, NULL,
                     &serverSecure)) {
      g_printerr("Invalid <server-url> argument: %s\n", serverUrl);
      exit(1);
   }

   http_proxy = CdkProxy_GetProxyForUrl(serverUrl, &proxyType);
   if (http_proxy && !CdkUrl_Parse(http_proxy, NULL, &proxyHost, &proxyPort,
                                   NULL, NULL)) {
      g_printerr("Invalid proxy URL '%s'.  Attempting direct connection.\n",
                 http_proxy);
      g_free(http_proxy);
      http_proxy = NULL;
   }

   if (http_proxy) {
      g_debug("Connecting to tunnel server '%s:%d' over %s, via proxy server '%s:%d'.",
              serverHost, serverPort, serverSecure ? "HTTPS" : "HTTP",
              proxyHost, proxyPort);
      host = proxyHost;
      port = proxyPort;
   } else {
      g_debug("Connecting to tunnel server '%s:%d' over %s.", serverHost,
              serverPort, serverSecure ? "HTTPS" : "HTTP");
      host = serverHost;
      port = serverPort;
   }
   g_assert(host && port > 0);

   gFd = TunnelConnectSocket(host, port);
   if (gFd < 0) {
      return;
   }

   if (http_proxy) {
      TunnelSocketProxyConnectCb(gFd, NULL);
   } else {
      TunnelSocketConnectCb(gFd, NULL);
   }

   g_free(http_proxy);
   g_free(serverUrl);
   g_free(serverProto);
   g_free(serverHost);
   g_free(proxyHost);
}


/*
 *-----------------------------------------------------------------------------
 *
 * TunnelPrintUsage --
 *
 *      Print binary usage information, in the UNIX tradition.
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
TunnelPrintUsage(const char *binName) // IN
{
   g_printerr("Usage: %s <server-url>\n", binName);
   exit(1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Tunnel_Main --
 *
 *      Main tunnel entrypoint.  Create a TunnelProxy object, start the async
 *      connect process and start the main poll loop.
 *
 * Results:
 *      0.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
Tunnel_Main(int argc,    // IN
            char **argv) // IN
{
#ifdef VIEW_GTK
   GMainLoop *loop = NULL;
#endif
   char buf[128];

#ifdef VMX86_DEBUG
   g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
#endif

   if (!setlocale(LC_ALL, "")) {
      g_printerr("Locale not supported by C library.\n"
                 "\tUsing the fallback 'C' locale.\n");
   }

   if (argc < 2 || !argv[1]) {
      TunnelPrintUsage(argv[0]);
   }

   gServerArg = argv[1];
   gConnectionIdArg = fgets(buf, sizeof(buf), stdin);
   if (!gConnectionIdArg || strlen(gConnectionIdArg) < 2) {
      fprintf(stderr, "Could not read connection id.\n");
       return 1;
   }
   /* remove '\n' */
   gConnectionIdArg[strlen(gConnectionIdArg) - 1] = '\0';

#ifdef VIEW_GTK
   Poll_InitGtk();
   loop = g_main_loop_new(NULL, FALSE);
#elif defined(VIEW_COCOA)
   Poll_InitCF();
#endif

#ifdef __MINGW32__
{
   // Initialize winsock API.
   WORD versionRequested;
   WSADATA wsaData;

   versionRequested = MAKEWORD(2, 0);
   if (WSAStartup(versionRequested, &wsaData) != 0) {
      g_printerr("WSAStartup failed; unable to continue.\n");
      return 2;
   }
}
#endif

   gTunnelProxy = TunnelProxy_Create(gConnectionIdArg, NULL, NULL, NULL, NULL,
                                     NULL, NULL);

   TunnelConnect();

#ifdef VIEW_GTK
   g_main_loop_run(loop);
   g_main_loop_unref(loop);
#elif defined(VIEW_COCOA)
   CFRunLoopRun();
#endif

   return 0;
}
