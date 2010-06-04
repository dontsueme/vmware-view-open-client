/*********************************************************
 * Copyright (C) 1998-2010 VMware, Inc. All rights reserved.
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
 * win32util_misc.c --
 *
 *    Various Win32 utility functions, with minimal
 *    dependencies. (Those with problematic dependencies live in
 *    bora/lib/user/win32util.c).
 */

#if (!defined(WINVER) || WINVER < 0x0501) && \
   (!defined(NTDDI_VERSION) || NTDDI_VERSION < NTDDI_WINXP)
#undef WINVER
#define WINVER 0x0501
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <shlobj.h>

#include "vmware.h"
#include "vm_product.h"
#include "win32u.h"
#include "win32util.h"
#include "unicode.h"

#include <shellapi.h>
#include <lmcons.h>

typedef BOOL (WINAPI *GetModuleHandleExW_t)(DWORD, LPCWSTR, HMODULE*);

/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_RobustGetLongPath --
 *
 *      Convert path name to long path name. The file does not have to
 *      exist (which is why we can't use the Win32 API); a "best
 *      effort" is made. Also relative paths are converted to
 *      absolute.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Unicode
W32Util_RobustGetLongPath(ConstUnicode path) // IN
{
   wchar_t *absPath = NULL;
   wchar_t *buf = NULL;
   const wchar_t *cp;
   Unicode absPathU = NULL;
   Unicode longPath = NULL;
   size_t bufLen;
   size_t maxLen;
   size_t cpLen;

   ASSERT(path);

   /*
    * First, turn into absolute path with GetFullPathName, which
    * exists on all versions of Windows. (GetFullPathName appears to
    * be fine with forward slashes).
    */

   absPathU = Win32U_GetFullPathName(path, NULL);
   if (!absPathU) {
      Log("%s: Win32U_GetFullPathName failed\n", __FUNCTION__);
      goto exit;
   }

   absPath = Unicode_GetAllocUTF16(absPathU);

   /*
    * Copy absPath to buf, building it up one component at a time,
    * and using FindFirstFile() to expand each stage.
    *
    * At each point, buf has the current expanded partial path,
    * bufLen is its length, and cp points to the rest of the original path.
    */
   maxLen = MAX_PATH;
   buf = Util_SafeMalloc(maxLen * sizeof(wchar_t));

   bufLen = wcscspn(absPath, L":");
   if (absPath[bufLen] == L'\0') {
      // If no drive letter specified, it must be a UNC or absolute path
      if (!absPath[0] || !wcschr(VALID_DIRSEPS_W, absPath[0])) {
         goto exit;
      }

      bufLen = 0;
   } else {
      /* 
       * If we find a ':', that means that we have a drive specification 
       * (otherwise the character is not legal). Copy the drive letter off
       * the front first since FindFirstFile doesn't do the right thing 
       * with drive letters (it returns the name of the current directory).
       *
       * XXX
       * We only handle paths of the form '<drive>:\<path>', or 
       * '<drive>:/<path>' not 'c:filename'.
       */
      if (!wcschr(VALID_DIRSEPS_W, absPath[bufLen + 1])) {
         goto exit;
      }

      bufLen += 1; 
      if (bufLen > maxLen - 1) {
         maxLen *= 2;
         buf = Util_SafeRealloc(buf, maxLen * sizeof(wchar_t));
      }
      memcpy(buf, absPath, bufLen * sizeof(wchar_t));
      
      /*
       * Single drive letter followed by a ':'. (Can there be anything
       * else?)
       */
      if (bufLen == 2) {
         ASSERT(buf[1] == L':');
         buf[0] = towupper(buf[0]);
      }
   }
   buf[bufLen] = 0;
   for (cp = absPath + bufLen; *cp; cp += cpLen) {
      WIN32_FIND_DATAW fd;
      HANDLE h;

      /*
       * Append next component to buf.
       *
       * cpLen is the next component length.
       */
      cpLen = wcscspn(cp + 1, VALID_DIRSEPS_W) + 1;
      if (bufLen + cpLen > maxLen - 1) {
         maxLen = MAX(maxLen * 2, maxLen + cpLen);
         buf = Util_SafeRealloc(buf, maxLen * sizeof(wchar_t));
      }
      memcpy(buf + bufLen, cp, cpLen * sizeof(wchar_t));
      buf[bufLen += cpLen] = 0;

      /*
       * Expand the partial path.
       *
       * We don't consider FindFirstFile() failure a reason
       * to terminate, because it doesn't matter a whole lot.
       *
       * The expanded component is copied to buf, over the current
       * unexpanded component.
       */
      h = FindFirstFileW(buf, &fd);
      if (h != INVALID_HANDLE_VALUE) {
         size_t expandedLen;

         FindClose(h);
         bufLen -= cpLen - wcsspn(cp, VALID_DIRSEPS_W);
         expandedLen = wcslen(fd.cFileName);
         if (bufLen + expandedLen > maxLen - 1) {
            maxLen = MAX(maxLen * 2, maxLen + expandedLen);
            buf = Util_SafeRealloc(buf, maxLen * sizeof(wchar_t));
         }
         memcpy(buf + bufLen, fd.cFileName,
                (expandedLen + 1) * sizeof(wchar_t));
         bufLen += expandedLen;
      }
   }

   longPath = Unicode_AllocWithUTF16(buf);

  exit:      
   free(absPath);
   free(buf);
   Unicode_Free(absPathU);

   return longPath;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetAppDataPath --
 *
 *      Return the path to our directory within the AppData directory.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      Directory created if doesn't exist.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetAppDataPath(void)
{
   Unicode basePath;
   Unicode vmwarePath;
   const utf16_t *vmwarePathW = NULL;

   if (FAILED(Win32U_SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                                      NULL, 0, &basePath))) {
      return NULL;
   }

   vmwarePath = Unicode_Join(basePath, DIRSEPS, PRODUCT_GENERIC_NAME,
                             NULL);
   Unicode_Free(basePath);

   vmwarePathW = UNICODE_GET_UTF16(vmwarePath);
   CreateDirectoryW(vmwarePathW, NULL); // Just to make sure it's there.
   UNICODE_RELEASE_UTF16(vmwarePathW);

   return vmwarePath;
}


/*
 *----------------------------------------------------------------------------
 *
 * W32Util_GetModuleByAddress --
 *
 *      Returns an HMODULE for the module containing the given address.
 *
 * Returns:
 *      HMODULE or NULL.
 *
 * Side effects:
 *      None (refcount of module is NOT increased).
 *
 *----------------------------------------------------------------------------
 */

HMODULE
W32Util_GetModuleByAddress(const void *addr) // IN
{
   HMODULE kernel32 = NULL;
   HMODULE hModule;
   GetModuleHandleExW_t pGetModuleHandleExW = NULL;

   /*
    * If GetModuleHandleEx is available (newer versions of Windows),
    * use it, otherwise fallback onto an older technique.
    */
   kernel32 = GetModuleHandleA("kernel32.dll");
   if (!kernel32) {
      goto exit;
   }

   pGetModuleHandleExW = (GetModuleHandleExW_t)
      GetProcAddress(kernel32, "GetModuleHandleExW");

   if (pGetModuleHandleExW) {
      if (!pGetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR) addr,
                               &hModule)) {
         hModule = NULL;
         goto exit;
      }
   } else {
      MEMORY_BASIC_INFORMATION mbi;

      /*
       * Get base address of module containing the specified address.
       */
      if (!VirtualQuery(addr, &mbi, sizeof mbi)) {
         goto exit;
      }

      /*
       * This is always the case for older versions of Windows.
       */
      hModule = (HMODULE) mbi.AllocationBase;
   }

  exit:
   return hModule;
}
