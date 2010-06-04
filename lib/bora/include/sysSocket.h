//depot/vdi/vdm30/framework/cdk/lib/bora/include/sysSocket.h#1 - add change 23245 (text)
/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
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

#include <sys/socket.h>
#ifdef __APPLE__
/*
 * See bsd/kern/uipc_usrreq.c::unp_internalize() in the XNU source code, the
 * kernel code expects the array of file descriptor 'int' values to start right
 * after the 'struct cmsghdr' in the socket message.
 *
 * 'sizeof (struct cmsghdr)' is 12 and 'sizeof (int)' is 4 in both 32-bit and
 * 64-bit builds, so the socket message ABI looks well defined and compatible
 * with any combination of user/kernel, 32/64-bit code.
 *
 * Unfortunately, the following macros are defined based on
 * '__DARWIN_ALIGN(sizeof (struct cmsghdr))' in <sys/socket.h>. This expression
 * yields the proper 12 in 32-bit builds, but it becomes an incorrect 16 in
 * 64-bit builds.
 *
 * So workaround this Apple bug by correctly defining these macros.
 */


/*
 * If this function fails to compile, Apple changed something. Check if our
 * workaround is still needed.
 */
static inline void
SysSocketAssertOnCompile(void)
{
#   ifdef __LP64__
   enum { AssertOnCompileMisused = CMSG_LEN(0) == 16 ? 1 : -1 };
#   else
   enum { AssertOnCompileMisused = CMSG_LEN(0) == 12 ? 1 : -1 };
#   endif
   typedef char AssertOnCompileFailed[AssertOnCompileMisused];
}


#   undef CMSG_DATA
#   define CMSG_DATA(_c) ((unsigned char *)(_c + 1))

#   undef CMSG_NXTHDR
#   define CMSG_NXTHDR(_m, _c) ((struct cmsghdr *)( \
     (unsigned char *)(_c) + (_c)->cmsg_len + sizeof (struct cmsghdr) \
   > (unsigned char *)(_m)->msg_control + (_m)->msg_controllen \
      ? 0 \
      : (unsigned char *)(_c) + (_c)->cmsg_len))

#   undef CMSG_SPACE
#   define CMSG_SPACE(_l) (sizeof (struct cmsghdr) + (_l))

#   undef CMSG_LEN
#   define CMSG_LEN(_l) (sizeof (struct cmsghdr) + (_l))
#endif // ifdef __APPLE__
