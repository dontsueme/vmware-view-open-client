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
 * cdkProxyDarwin.c --
 *
 *      Implementaiton of CdkProxy for Mac OS X.
 */

#include <CoreServices/CoreServices.h>
#include <glib.h>
#include <SystemConfiguration/SystemConfiguration.h>


#include "cdkProxy.h"


/* TODO: Move this to libMisc instead of libUser? */
/*
 *-----------------------------------------------------------------------------
 *
 * CFStringToUTF8CString --
 *
 *      Copied from bora/lib/user/utilMacos.c as of CLN 825297.
 *
 *      Convert a CFString into a UTF-8 encoded C string.
 *
 *      Amazingly, CFString does not provide this functionality, so everybody
 *      (including Apple, see smb-217.18/lib/smb/charsets.c in darwinsource)
 *      ends up re-implementing it this way...
 *
 * Results:
 *      On success: Allocated, UTF8-encoded, NUL-terminated string.
 *      On failure: NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
CdkProxy_CFStringToUTF8CString(CFStringRef s) /* IN */
{
   static CFStringEncoding const encoding = kCFStringEncodingUTF8;
   char const *fast;
   char *result;

   g_assert(s);

   fast = CFStringGetCStringPtr(s, encoding);
   if (fast) {
      result = strdup(fast);
   } else {
      size_t maxSize;

      maxSize =
         CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), encoding) + 1;
      result = malloc(maxSize);
      if (result) {
         if (CFStringGetCString(s, result, maxSize, encoding)) {
            /*
             * It is likely that less than 'maxSize' bytes have actually been
             * written into 'result'. If that becomes a problem in the future,
             * we can always trim the buffer here.
             */
         } else {
            free(result);
            result = NULL;
         }
      }
   }

   if (!result) {
      g_debug("Failed to get C string from CFString.");
   }
   return result;
}


/*
 * -----------------------------------------------------------------------------
 * GetDictionaryStringValue --
 *
 *    Helper function to retrieve a C string from a CFString value inside a
 *    CFDictionaryRef.
 *
 * Results:
 *    An allocated char*. Caller must free. Returns NULL on error or if
 *    the key does not exist in the dictionary.
 * -----------------------------------------------------------------------------
 */

static char *
GetDictionaryStringValue (CFDictionaryRef dict, /* IN */
                          const void *key)      /* IN */
{
   CFTypeRef value;
   CFStringRef valueStr;
   char *ret = NULL;

   g_assert(dict);
   g_assert(key);

   if (CFDictionaryContainsKey(dict, key)) {
      value = CFDictionaryGetValue(dict, key);
      g_assert(CFStringGetTypeID() == CFGetTypeID(value));

      valueStr = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@"), value);
      ret = CdkProxy_CFStringToUTF8CString(valueStr);
   }

   return ret;
}


/*
 * -----------------------------------------------------------------------------
 * GetDictionaryNumberValue --
 *
 *    Helper function to retrieve an integer from a CFNumber value inside a
 *    CFDictionaryRef.
 *
 * Results:
 *    The number from the dictionary, or -1 on error of if the key does not
 *    exist in the dictionary.
 * -----------------------------------------------------------------------------
 */

static int
GetDictionaryNumberValue (CFDictionaryRef dict, /* IN */
                          const void *key)      /* IN */
{
   CFTypeRef value;
   Boolean ok;
   int ret = -1;

   g_assert(dict);
   g_assert(key);

   if (CFDictionaryContainsKey(dict, key)) {
      value = CFDictionaryGetValue(dict, key);
      g_assert(CFNumberGetTypeID() == CFGetTypeID(value));

      ok = CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &ret);
      g_assert(ok);
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * CdkProxy_GetProxyForUrl --
 *
 *      Retrieve proxy settings for a given URL, using the
 *      SystemConfiguration framework.
 *
 *      PAC is not supported.
 *
 * Results:
 *      If no proxy is needed, or no proxy could be found, NULL is
 *      returned, and proxyType is set to CDK_PROXY_NONE.  Otherwise,
 *      a string containing the proxy host and port is returned,
 *      formatted as [http://]<host>:<port> - for SOCKS proxies the
 *      schema is omitted.
 *
 * Side effects:
 *      The returned string should be freed by the caller.
 *
 *-----------------------------------------------------------------------------
 */

char *
CdkProxy_GetProxyForUrl(const char *aUrl,        /* IN */
                        CdkProxyType *proxyType) /* OUT */
{
   CFURLRef url = NULL;
   CFDictionaryRef proxySettings = NULL;
   CFArrayRef proxies = NULL;
   CFIndex numProxies;
   int i;
   char *ret = NULL;

   g_assert(aUrl);
   g_assert(proxyType);

   *proxyType = CDK_PROXY_NONE;

   /* Turn the URL string into a CFURL. */
   url = CFURLCreateWithBytes(NULL, (const UInt8 *)aUrl, strlen(aUrl),
                              kCFStringEncodingUTF8, NULL);
   if (url == NULL) {
      goto out;
   }

   /* Retrieve proxies from System Configuration database. */
   proxySettings = SCDynamicStoreCopyProxies(NULL);
   if (proxySettings == NULL) {
      goto out;
   }

   /* Check if a proxy is required to access this particular URL. */
   proxies = CFNetworkCopyProxiesForURL(url, proxySettings);
   if (proxies == NULL) {
      goto out;
   }

   /* Iterate through the proxies until we find a suitable one. */
   numProxies = CFArrayGetCount(proxies);

   for (i = 0; i < numProxies; i++) {
      CFDictionaryRef proxy = CFArrayGetValueAtIndex(proxies, i);
      CFStringRef osProxyType = CFDictionaryGetValue(proxy, kCFProxyTypeKey);
      Boolean isHTTP = CFEqual(osProxyType, kCFProxyTypeHTTP) ||
                       CFEqual(osProxyType, kCFProxyTypeHTTPS);
      Boolean isSOCKS = CFEqual(osProxyType, kCFProxyTypeSOCKS);

      /* TODO: Handle PAC */
      if (isHTTP || isSOCKS) {
         int port = GetDictionaryNumberValue(proxy, kCFProxyPortNumberKey);
         char *host = GetDictionaryStringValue(proxy, kCFProxyHostNameKey);

         g_assert(!(isHTTP && isSOCKS));

         if (port != -1 && host != NULL) {
            /* XXX: we should support more proxy types in general */
            /* Note that we still use http to connect to https proxies. */
            ret = g_strdup_printf("%s%s:%d", isHTTP ? "http://" : "", host,
                                  port);
            if (ret) {
               free(host);
               *proxyType = isHTTP ? CDK_PROXY_HTTP : CDK_PROXY_SOCKS4;
               break;
            }
         }
         free(host);
      }
   }

out:
   if (proxies) {
      CFRelease(proxies);
   }
   if (proxySettings) {
      CFRelease(proxySettings);
   }
   if (url) {
      CFRelease(url);
   }

   return ret;
}
