/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
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
 * cdkUrl.c --
 *
 *      Implementation of CdkUrl.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UIDNA_IDNTOASCII
#include "vm_basic_types.h"
#include <unicode/uidna.h>
#endif // HAVE_UIDNA_IDNTOASCII


#include "cdkUrl.h"


/*
 *-----------------------------------------------------------------------------
 *
 * IDNToASCII --
 *
 *      Convert a string from UTF-8/IDN to Punycode/ASCII.
 *
 *      For explanations of IDN, see:
 *      https://developer.mozilla.org/En/Internationalized_Domain_Names_(IDN)_Support_in_Mozilla_Browsers
 *      http://www.ietf.org/rfc/rfc3490.txt
 *      http://www.ietf.org/rfc/rfc3491.txt
 *      http://www.ietf.org/rfc/rfc3492.txt
 *
 * Results:
 *      An ASCII representation of the UTF-8 hostname, or an empty
 *      string if the conversion failed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static char *
IDNToASCII(const char *text) // IN
{
#ifdef HAVE_UIDNA_IDNTOASCII
   /*
    * To convert UTF-8 to ASCII, first we need to convert it to UTF-16
    * for ICU, then convert it to 16-bit "ASCII", and then back to
    * 8-bit ASCII (which is ASCII) for xmlParse().
    */
   long utf16Len;
   GError *error = NULL;
   gunichar2 *utf16Text = g_utf8_to_utf16(text, -1, NULL, &utf16Len,
                                          &error);
   if (error) {
      g_warning("Could not convert text \"%s\" to UTF-16: %s\n", text,
                error->message);
      g_error_free(error);
      return NULL;
   }

   int32_t idnLen = 2 * utf16Len;
   UChar *idnText;
   UErrorCode status;
   int32_t len;
   /*
    * If we don't allocate enough characters on the first attempt,
    * IDNToASCII() will return how many characters we need.  If the
    * second attempt fails, just give up rather than looping forever.
    */
tryConversion:
   idnText = g_new(UChar, idnLen + 1);
   status = U_ZERO_ERROR;
   len = uidna_IDNToASCII((const UChar *)utf16Text, utf16Len,
                          idnText, idnLen, UIDNA_DEFAULT, NULL,
                          &status);
   if (len > idnLen) {
      g_free(idnText);
      // Guard against multiple loops.
      if (idnLen != 2 * utf16Len) {
         g_warning("The ASCII length was greater than we allocated after two "
                   "attempts; giving up on \"%s\".", text);
         g_free(idnText);
         g_free(utf16Text);
         return NULL;
      }
      idnLen = len;
      goto tryConversion;
   } else if (U_FAILURE(status)) {
      g_warning("Could not convert text \"%s\" to IDN: %s\n", text,
                u_errorName(status));
      g_free(idnText);
      g_free(utf16Text);
      return NULL;
   }

   // Convert it back to UTF-8/ASCII.
   char *ret = g_utf16_to_utf8((gunichar2 *)idnText, len, NULL, NULL,
                               NULL);
   g_free(idnText);
   g_free(utf16Text);

   return ret;
#else
   g_assert_not_reached();
   return NULL;
#endif // HAVE_UIDNA_IDNTOASCII
}


/*
 *-----------------------------------------------------------------------------
 *
 * CdkUrl_Parse --
 *
 *      Parse a URL.  Components are saved into their respective
 *      arguments.  If secure is non-NULL and *secure is non-zero, the
 *      protocol will default to https if not specified; otherwise, it
 *      defaults to http.
 *
 * Results:
 *      TRUE if url parsed successfully.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
CdkUrl_Parse(const char *url,      // IN
             char **proto,         // OUT
             char **host,          // OUT
             unsigned short *port, // OUT
             char **path,          // OUT
             gboolean *secure)     // IN/OUT
{
   char *myUrl = NULL;
   const char *start = NULL;
   const char *end = NULL;
   char *myProto = NULL;
   char *myHost = NULL;
   char *myPath = NULL;
   unsigned int myPort;

   g_assert(url);

   /*
    * If there are multibyte characters, we need to convert from IDN
    * to ASCII (punycode).
    */
   if (g_utf8_strlen(url, -1) != strlen(url)) {
      url = myUrl = IDNToASCII(url);
      if (!url) {
         return FALSE;
      }
   }

   start = url;

   end = strstr(start, "://");
   if (end) {
      myProto = g_strndup(start, end - start);
      start = end + 3;
   } else {
      /* Implicit protocol */
      myProto = g_strdup(secure && *secure ? "https" : "http");
   }

   end = strpbrk(start, ":/");
   if (!end) {
      end = start + strlen(start);
   }
   myHost = g_strndup(start, end - start);

   if (*end == ':') {
      /* Explicit port */
      start = end + 1;
      end = strchr(start, '/');
      if (!end) {
         end = start + strlen(start);
      }
      errno = 0;
      myPort = strtoul(start, NULL, 10);
      if (errno != 0 || myPort > 0xffff) {
         goto error;
      }
   } else {
      /* Implicit port */
      if (strcmp(myProto, "http") == 0) {
         myPort = 80;
      } else if (strcmp(myProto, "https") == 0) {
         myPort = 443;
      } else {
         /* Not implemented */
         goto error;
      }
   }

   start = end;
   if (*start == '/') {
      /* Explicit path */
      myPath = g_strdup(start);
   } else {
      /* Implicit path */
      g_assert(*start == '\0');
      myPath = g_strdup("/");
   }

   if (secure) {
      *secure = strcmp(myProto, "https") == 0;
   }
   if (proto) {
      *proto = myProto;
   } else {
      g_free(myProto);
   }
   if (host) {
      *host = myHost;
   } else {
      g_free(myHost);
   }
   if (port) {
      *port = myPort;
   }
   if (path) {
      *path = myPath;
   } else {
      g_free(myPath);
   }
   g_free(myUrl);
   return TRUE;

error:
   g_free(myUrl);
   g_free(myProto);
   g_free(myHost);
   g_free(myPath);
   return FALSE;
}
