/*********************************************************
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
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
 * cdkProxy.c --
 *
 *      Implementation of CdkProxy based on environment variables.
 */


#include "cdkProxy.h"


#ifndef _WIN32
#include "str.h"
#include "util.h"
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * CdkProxy_GetProxyForUrl --
 *
 *      Retrieve proxy settings for a given URL, using the http_proxy,
 *      https_proxy, or HTTPS_PROXY environment variables.
 *
 *      PAC and SOCKS are not supported.
 *
 * Results:
 *      If no proxy is needed, or no proxy could be found, NULL is
 *      returned, and proxyType is set to CDK_PROXY_NONE.  Otherwise,
 *      a string containing the proxy host and port is returned,
 *      likely formatted as http://<host>:<port>.
 *
 * Side effects:
 *      The returned string should be freed by the caller.
 *
 *-----------------------------------------------------------------------------
 */

char *
CdkProxy_GetProxyForUrl(const char *aUrl,        // IN
                        CdkProxyType *proxyType) // OUT
{
#ifdef _WIN32
   // We need this stub here so ovditest builds on Windows.
   *proxyType = CDK_PROXY_NONE;
   return (char *)0;
#else
   const char *proxy = NULL;
   const unsigned char *c;

   ASSERT(aUrl);
   ASSERT(proxyType);

   *proxyType = CDK_PROXY_NONE;

   if (Str_Strncmp(aUrl, "http://", 7) == 0) {
      proxy = getenv("http_proxy");
   } else if (Str_Strncmp(aUrl, "https://", 8) == 0) {
      proxy = getenv("https_proxy");
      if (!proxy || !*proxy) {
         proxy = getenv("HTTPS_PROXY");
      }
   }

   if (!proxy || !*proxy) {
      return NULL;
   }

   /* Ensure that url is ASCII. */
   for (c = (const unsigned char *)proxy; *c; c++) {
      if (*c > 127) {
         static int warned = 0;
         if (!warned) {
            warned = 1;
            Warning("Non-ASCII character found in proxy environment variable.\n");
         }
         return NULL;
      }
   }

   *proxyType = CDK_PROXY_HTTP;
   return Util_SafeStrdup(proxy);
#endif
}
