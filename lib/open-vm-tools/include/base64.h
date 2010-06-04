/*********************************************************
 * Copyright (C) 2004 VMware, Inc. All rights reserved.
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
 * base64.h --
 *
 *      Functions to base64 encode/decode buffers. Implemented in
 *      lib/misc/base64.c.
 */

#ifndef _BASE64_H
#define _BASE64_H


#include <glib.h>


gboolean Base64_Encode(guint8 const *src, size_t srcLength,
                       char *target, size_t targSize,
                       size_t *dataLength);
gboolean Base64_Decode(char const *src,
                       guint8 *target, size_t targSize,
                       size_t *dataLength);
gboolean Base64_ValidEncoding(char const *src, size_t srcLength);
size_t Base64_EncodedLength(guint8 const *src, size_t srcLength);
size_t Base64_DecodedLength(char const *src, size_t srcLength);
gboolean Base64_EasyEncode(const guint8 *src, size_t srcLength,
                           char **target);
gboolean Base64_EasyDecode(const char *src,
                           guint8 **target, size_t *targSize);

#endif
