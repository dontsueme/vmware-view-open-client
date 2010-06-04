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
 * win32u.c --
 *
 *    UTF-8 Win32 API wrappers
 */

#include "safetime.h"
#include <winsock2.h> // also includes windows.h
#include <io.h>
#include <process.h>

#include "vm_ctype.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <shlobj.h>
#include <shellapi.h>
#include <lmcons.h>
#include <shlwapi.h>

#include "vmware.h"
#include "win32u.h"
#include "msg.h"
#include "util.h"
#include "str.h"
#include "vm_version.h"
#include "dynbuf.h"


/*
 * Module types
 */
#ifdef __MINGW32__
typedef long LSTATUS;
#endif

typedef HRESULT (WINAPI *SHGetFolderPathWFnType)(HWND, int, HANDLE, DWORD,
                                                 LPWSTR);
typedef HRESULT (WINAPI *SHSetFolderPathWFnType)(int, HANDLE, DWORD, LPWSTR);
typedef BOOL (WINAPI *PathUnExpandEnvStringsWFnType)(LPWSTR, LPWSTR, UINT);
typedef BOOL (WINAPI *LookupAccountNameWFnType)(
   LPCWSTR lpSystemName,
   LPCWSTR lpAccountName,
   PSID Sid,
   LPDWORD cbSid,
   LPWSTR ReferencedDomainName,
   LPDWORD cchReferencedDomainName,
   PSID_NAME_USE peUse);
typedef LSTATUS (WINAPI *SHCopyKeyWFnType)(HKEY, LPCWSTR, HKEY, DWORD);
typedef LSTATUS (WINAPI *SHDeleteKeyWFnType)(HKEY, LPCWSTR);
static HMODULE g_hShlwapi = NULL;
static SHCopyKeyWFnType g_SHCopyKeyWFn = NULL;
static SHDeleteKeyWFnType g_SHDeleteKeyWFn = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Win32U_LookupSidForAccount --
 *
 *      Returns the SID for a given name.  Caller is expected 
 *      to free any non-NULL pointer returned by this function.
 *
 * Results:
 *      Returns matching SID, otherwise NULL on error.
 *
 * Side effects:
 *      DACL of the file is replaced.
 *
 *----------------------------------------------------------------------
 */

PSID
Win32U_LookupSidForAccount(ConstUnicode name) // IN
{
   PSID psid = NULL;
   wchar_t *domain = NULL;
   DWORD psidSize = 0;
   DWORD domainSize = 0;
   SID_NAME_USE sidType;
   const utf16_t *nameW = NULL;
   BOOL succeeded = FALSE;
   static HMODULE hAdvapi = NULL;
   static LookupAccountNameWFnType LookupAccountNameWFn = NULL;

   ASSERT(name);

   if (!hAdvapi) {
      hAdvapi = LoadLibraryA("advapi32.dll");
   }

   if (hAdvapi && !LookupAccountNameWFn) {
      LookupAccountNameWFn = (LookupAccountNameWFnType)
         GetProcAddress(hAdvapi, "LookupAccountNameW");
   }

   if (!LookupAccountNameWFn) {
      goto exit;
   }

   nameW = UNICODE_GET_UTF16(name);

   if (LookupAccountNameWFn(NULL,                 // local machine
                            nameW, psid, &psidSize, domain, &domainSize,
                            &sidType) == 0) {
      psid = Util_SafeMalloc(psidSize);
      domain = Util_SafeMalloc(domainSize * sizeof *domain);

      if (LookupAccountNameWFn(NULL,              // local machine
                               nameW, psid, &psidSize, domain, &domainSize,
                               &sidType) == 0) {
         goto exit;
      }
   }

   succeeded = TRUE;

  exit:
   if (!succeeded) {
      free(psid);
      psid = NULL;
   }

   free(domain);
   UNICODE_RELEASE_UTF16(nameW);

   return psid;
}


/*
 *----------------------------------------------------------------------
 *
 * Win32U_SHGetFolderPath --
 *
 *      Wrapper around SHGetFolderPath. See MSDN for details.
 *
 * Results:
 *      An HRESULT, and a Unicode string in 'path' (free with
 *      Unicode_Free).
 * 
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HRESULT
Win32U_SHGetFolderPath(HWND hwndOwner, // IN
                       int nFolder,    // IN
                       HANDLE hToken,  // IN
                       DWORD dwFlags,  // IN
                       Unicode *path)  // OUT
{
   static HMODULE hShfolder = NULL;
   static SHGetFolderPathWFnType SHGetFolderPathWFn = NULL;
   wchar_t pathW[MAX_PATH]; // MAX_PATH enforced by the API
   HRESULT res;

   ASSERT(path);
   *path = NULL;

   if (!hShfolder) {
      hShfolder = LoadLibraryA("shfolder.dll");
   }

   if (hShfolder && !SHGetFolderPathWFn) {
      SHGetFolderPathWFn = (SHGetFolderPathWFnType)
         GetProcAddress(hShfolder, "SHGetFolderPathW");
   }

   if (!SHGetFolderPathWFn) {
      return E_UNEXPECTED;
   }

   res = SHGetFolderPathWFn(hwndOwner, nFolder, hToken, dwFlags, pathW);

   if (SUCCEEDED(res)) {
      *path = Unicode_AllocWithUTF16(pathW);
   }

   return res;
}


/*
 *----------------------------------------------------------------------
 *
 * Win32U_SHSetFolderPath --
 *
 *      Wrapper around SHSetFolderPath. The changes don't take effect
 *      until the user logs out and logs back in.
 *      MSDN documents this function even though it is not exported by name.
 *      See MSDN for more details:
 *      http://msdn2.microsoft.com/en-us/library/bb762247(VS.85).aspx
 *
 * Results:
 *      An HRESULT.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

HRESULT
Win32U_SHSetFolderPath(int csidl,     // IN
                       HANDLE hToken, // IN
                       DWORD dwFlags, // IN
                       Unicode path)  // IN
{
   static HMODULE hShell32 = NULL;
   static SHSetFolderPathWFnType SHSetFolderPathWFn = NULL;
   utf16_t *pathW;
   HRESULT res;

   if (!hShell32) {
      hShell32 = LoadLibraryA("shell32.dll");
   }

   if (hShell32 && !SHSetFolderPathWFn) {
      SHSetFolderPathWFn = (SHSetFolderPathWFnType)
         GetProcAddress(hShell32, (char *)232L);
   }

   if (!SHSetFolderPathWFn) {
      return E_UNEXPECTED;
   }

   pathW = UNICODE_GET_UTF16(path);
   res = SHSetFolderPathWFn(csidl, hToken, dwFlags, pathW);
   UNICODE_RELEASE_UTF16(pathW);

   return res;
}


/*
 *----------------------------------------------------------------------
 *
 * Win32U_PathUnExpandEnvStrings --
 *
 *      Wrapper around PathUnExpandEnvStrings.
 *      Note: %USERPROFILE% will not be unexpanded if the user is being
 *      impersonated from a service.
 *
 *      See MSDN for more details:
 *      http://msdn.microsoft.com/en-us/library/bb773760(VS.85).aspx
 *
 * Results:
 *      NULL or a Unicode string (free with Unicode_Free)
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
Win32U_PathUnExpandEnvStrings(Unicode path) // IN
{
   static PathUnExpandEnvStringsWFnType PathUnExpandEnvStringsWFn = NULL;
   utf16_t *pathW;
   utf16_t unexpandedPathW[MAX_PATH] = {0};
   size_t unexpandedPathWChars = ARRAYSIZE(unexpandedPathW);
   Bool res = FALSE;

   if (!g_hShlwapi) {
      g_hShlwapi = LoadLibraryA("shlwapi.dll");
   }

   if (g_hShlwapi && !PathUnExpandEnvStringsWFn) {
      PathUnExpandEnvStringsWFn = (PathUnExpandEnvStringsWFnType)
         GetProcAddress(g_hShlwapi, "PathUnExpandEnvStringsW");
   }

   if (!PathUnExpandEnvStringsWFn) {
      return NULL;
   }

   pathW = UNICODE_GET_UTF16(path);
   res = PathUnExpandEnvStringsWFn(pathW, unexpandedPathW, unexpandedPathWChars);
   UNICODE_RELEASE_UTF16(pathW);

   if (res) {
      return Unicode_AllocWithUTF16(unexpandedPathW);
   } else {
      return NULL;
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetModuleFileName --
 *
 *      Dyanmic-size wrapper around GetModuleFileName. See
 *      MSDN. Returns a Unicode string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetModuleFileName(HANDLE hModule) // IN
{
   Unicode path = NULL;
   utf16_t *pathW = NULL;
   DWORD size = MAX_PATH;

   while (TRUE) {
      DWORD res;

      pathW = Util_SafeRealloc(pathW, size * sizeof *pathW);
      res = GetModuleFileNameW(hModule, pathW, size);

      if (res == 0) {
         /* fatal error */
         goto exit;
      } else if (res == size) {
         /*
          * Buffer too small.  Don't depend on GetLastError() returning
          * ERROR_INSUFFICIENT_BUFFER for Windows 2000/XP systems.
          */
         size *= 2;
      } else {
         /* success */
         path = Unicode_AllocWithUTF16(pathW);
         break;
      }
   }

  exit:
   free(pathW);
   return path;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetFullPathName --
 *
 *      Wrapper around GetFullPathName. See MSDN. Returns a Unicode
 *      string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetFullPathName(ConstUnicode path,         // IN
                       UnicodeIndex *iComponent)  // OUT (OPT)
{
   Unicode fullPath = NULL;
   wchar_t *fullPathW = NULL;
   wchar_t *component;
   DWORD size = MAX_PATH;
   DWORD ret;
   const utf16_t *pathW;
   static HMODULE hk32;

   ASSERT(path);

   pathW = UNICODE_GET_UTF16(path);

   while (TRUE) {
      fullPathW = Util_SafeRealloc(fullPathW, size * sizeof(wchar_t));
      ret = GetFullPathNameW(pathW, size, fullPathW, &component);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
      }
   }

   fullPath = Unicode_AllocWithUTF16(fullPathW);

   if (iComponent) {
      *iComponent = component - pathW;
   }

exit:
   free(fullPathW);
   UNICODE_RELEASE_UTF16(pathW);

   return fullPath;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetClassName --
 *
 *      Wrapper around GetClassName. See MSDN. Returns a
 *      Unicode string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetClassName(HWND hwnd) // IN: HANDLE of window
{
   Unicode className = NULL;
   WCHAR data[MAX_PATH];
   int size;

   size = GetClassNameW(hwnd, data, ARRAYSIZE(data));
   if (0 == size) {
      goto exit;
   }

   className = Unicode_AllocWithUTF16(data);

exit:
   return className;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetCurrentDirectory --
 *
 *      Wrapper around GetCurrentDirectory. See MSDN. Returns a
 *      Unicode string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetCurrentDirectory(void) // IN
{
   Unicode curDir = NULL;
   wchar_t *curDirW = NULL;
   DWORD size;

   size = GetCurrentDirectoryW(0, NULL);
   if (0 == size) {
      goto exit;
   }

   curDirW = Util_SafeMalloc(size * sizeof(wchar_t));
   size = GetCurrentDirectoryW(size, curDirW);
   if (0 == size) {
      goto exit;
   }

   curDir = Unicode_AllocWithUTF16(curDirW);

exit:
   free(curDirW);

   return curDir;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetLogicalDriveStrings --
 *
 *      Wrapper around GetLogicalDriveStrings.
 *
 * Returns:
 *      Returns the number drive strings returned or -1 on failure.
 *
 * Side effects:
 *      Each unicode (one for each drive) and the array itself must be freed.
 *
 *----------------------------------------------------------------------------
 */

int
Win32U_GetLogicalDriveStrings(Unicode **driveList)  // OUT:
{
   int numStrings;

   DWORD tchars = 0;
   utf16_t *strings = NULL;

   ASSERT(driveList);

   /*
    * Attempt to get the drive strings
    *
    * Loop so we can catch hot plugged drives arriving.
    */

   while (TRUE) {
      DWORD ret;

      ret = GetLogicalDriveStringsW(tchars, strings);

      if (ret == 0) {
         free(strings);
         strings = NULL;
         break;
      }

      if (ret <= tchars) {
         break;
      }

      tchars = ret + 1;
      strings = Util_SafeRealloc(strings, tchars * sizeof(utf16_t));
   }

   /* Did it work? */

   if (strings == NULL) {
      /* Didn't get them */
      numStrings = -1;
   } else {
      /* Got them! Parse them and make a nice list out of them */
      DynBuf b;

      int i = 0;

      numStrings = 0;

      DynBuf_Init(&b);

      while (strings[i] != 0) {
         int start = i;
         Unicode drive;

         while (strings[i] != 0) {
            i++;
         }

         drive = Unicode_AllocWithUTF16(&strings[start]);
         DynBuf_Append(&b, &drive, sizeof drive);

         numStrings++;

         i++;
      }

      *driveList = DynBuf_Detach(&b);
      free(strings);
   }

   return numStrings;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_FindFirstFileW --
 *
 *      Wrapper around FindFirstFile. See MSDN. Returns a search handle.
 *
 * Returns:
 *      Handle
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_FindFirstFileW(ConstUnicode searchPath,     // IN:
                      WIN32_FIND_DATAW *findData)  // IN:
{
   HANDLE handle;
   utf16_t *path;

   path = UNICODE_GET_UTF16(searchPath);
   handle = FindFirstFileW(path, findData);
   UNICODE_RELEASE_UTF16(path);

   return handle;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_FindFirstChangeNotification --
 *
 *      Wrapper around FindFirstChangeNotification. See MSDN.
 *
 * Returns:
 *      Handle
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_FindFirstChangeNotification(ConstUnicode path,     // IN:
                                   BOOL watchSubtree,     // IN:
                                   DWORD notifyFilter)    // IN:
{
   HANDLE handle;
   Unicode fullPath;
   utf16_t *wpath;

   fullPath = Unicode_Join("\\\\?\\", path, NULL);
   wpath = UNICODE_GET_UTF16(fullPath);
   handle = FindFirstChangeNotificationW(wpath, watchSubtree, notifyFilter);
   UNICODE_RELEASE_UTF16(wpath);
   Unicode_Free(fullPath);
   return handle;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetComputerNameEx --
 *
 *      Wrapper around GetComputerNameEx. See MSDN. Returns a Unicode string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      On failure GetComputerNameEx will set an error that can be retreived
 *      by GetLastError().
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetComputerNameEx(int nameType)  // IN:
{
   utf16_t *name = NULL;
   DWORD nameSize  = 0;
   Unicode uName = NULL;

   /* First get the size needed. */
   if (   !GetComputerNameExW((COMPUTER_NAME_FORMAT) nameType, NULL, &nameSize)
       && GetLastError() != ERROR_MORE_DATA) {
      goto exit;
   }

   /* 
    * Allocate exactly that much. The size returned by GetComputerNameExW()
    * includes space for NUL.
    */
   name = malloc(nameSize * sizeof *name);
   if (NULL == name) {
      goto exit;
   }

   if (!GetComputerNameExW((COMPUTER_NAME_FORMAT) nameType, name, &nameSize)) {
      goto exit;
   }

   uName = Unicode_AllocWithUTF16(name);

exit:
   free(name);

   return uName;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetDriveType --
 *
 *      Wrapper around GetDriveType. See MSDN.
 *
 * Returns:
 *      Drive type
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

UINT
Win32U_GetDriveType(ConstUnicode driveString)  // IN:
{
   utf16_t *path = UNICODE_GET_UTF16(driveString);
   UINT driveType = GetDriveTypeW(path);

   UNICODE_RELEASE_UTF16(path);
   return driveType;
}


/*
 *----------------------------------------------------------------------------
 * Win32U_GetClipboardFormatName --
 *
 *      Wrapper around GetClipboardFormatName. See MSDN. Returns a
 *      Unicode string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetClipboardFormatName(UINT format) // IN
{
   Unicode formatName = NULL;
   wchar_t *formatNameW = NULL;
   int size = 64;

   while (TRUE) {
      int ret;

      formatNameW = Util_SafeRealloc(formatNameW, size * sizeof(wchar_t));
      ret = GetClipboardFormatNameW(format, formatNameW, size);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
      }
   }

   formatName = Unicode_AllocWithUTF16(formatNameW);

exit:
   free(formatNameW);
   return formatName;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetClipboardFormatName --
 *
 *      Wrapper around DragQueryFile. See MSDN. Returns a Unicode
 *      string.
 *
 * Returns:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_DragQueryFile(HANDLE hDrop, // IN
                     UINT iFile)   // IN
{
   Unicode dqfile = NULL;
   wchar_t *dqfileW = NULL;
   size_t size = 0;
   UINT ret = 0;

   /* we don't handle this bizarre documented corner case */
   ASSERT_NOT_IMPLEMENTED(0xffffffff != iFile);

   // Query the buffer size.
   size = DragQueryFileW(hDrop, iFile, NULL, 0);
   if (size == 0) {
      goto exit;
   }

   size += 1 /* NULL-terminator */;
   dqfileW = Util_SafeMalloc(size * sizeof *dqfileW);
   ret = DragQueryFileW(hDrop, iFile, dqfileW, size);
   if (ret > 0) {
      dqfile = Unicode_AllocWithUTF16(dqfileW);
   }

exit:
   free(dqfileW);
   return dqfile;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_LoadLibrary --
 *
 *      Wrapper around LoadLibrary. See MSDN.
 *
 * Returns:
 *      Handle to the DLL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

HMODULE
Win32U_LoadLibrary(ConstUnicode pathName)  // IN:
{
   return Win32U_LoadLibraryEx(pathName, NULL, 0);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_LoadLibraryEx --
 *
 *      Wrapper around LoadLibraryEx. See MSDN.
 *
 * Results:
 *      Handle to the DLL.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

HMODULE
Win32U_LoadLibraryEx(ConstUnicode pathName,   // IN
                     HANDLE file,             // IN
                     DWORD flags)             // IN
{
   utf16_t *path = UNICODE_GET_UTF16(pathName);
   HMODULE handle = LoadLibraryExW(path, file, flags);

   UNICODE_RELEASE_UTF16(path);
   return handle;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateFile --
 *
 *      Wrapper around CreateFile. See MSDN.
 *
 * Returns:
 *      Handle
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_CreateFile(ConstUnicode pathName,            // IN:
                  DWORD access,                     // IN:
                  DWORD share,                      // IN:
                  SECURITY_ATTRIBUTES *attributes,  // IN:
                  DWORD disposition,                // IN:
                  DWORD flags,                      // IN:
                  HANDLE templateFile)              // IN:
{
   utf16_t *path = UNICODE_GET_UTF16(pathName);
   HANDLE handle = CreateFileW(path, access, share, attributes, disposition,
                               flags, templateFile);

   UNICODE_RELEASE_UTF16(path);
   return handle;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetVolumeInformation --
 *
 *      Wrapper around GetVolumeInformation. See MSDN.
 *
 * Returns:
 *      TRUE on success, FALSE on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetVolumeInformation(ConstUnicode pathName,              // IN/OPT:
                            Unicode *volumeName,                // OUT/OPT:
                            DWORD *volumeSerialNumber,          // OUT/OPT:
                            DWORD *volumeMaximumComponentPath,  // OUT:
                            DWORD *fileSystemFlags,             // OUT:
                            Unicode *fileSystemName)            // OUT/OPT:
{
   utf16_t *path = UNICODE_GET_UTF16(pathName);
   BOOL result;
   utf16_t name[MAX_PATH + 1];
   utf16_t volume[MAX_PATH + 1];

   result = GetVolumeInformationW(path,
                                  volume, MAX_PATH + 1,
                                  volumeSerialNumber,
                                  volumeMaximumComponentPath,
                                  fileSystemFlags,
                                  name, MAX_PATH + 1);

   UNICODE_RELEASE_UTF16(path);

   if (volumeName) {
      *volumeName = (result) ? Unicode_AllocWithUTF16(volume) : NULL;
   }

   if (fileSystemName) {
      *fileSystemName = (result) ? Unicode_AllocWithUTF16(name) : NULL;
   }

   return result;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_DeleteFile --
 *
 *    Wrapper around DeleteFile. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_DeleteFile(ConstUnicode lpFileName)  // IN
{
   BOOL retval;
   utf16_t *lpFileNameW;

   ASSERT(lpFileName);

   lpFileNameW = Unicode_GetAllocUTF16(lpFileName);

   retval = DeleteFileW(lpFileNameW);

   free(lpFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetModuleHandle --
 *
 *    Wrapper around GetModuleHandle. See MSDN.
 *
 * Results:
 *    Returns handle to specified module, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HMODULE
Win32U_GetModuleHandle(ConstUnicode lpModuleName)  // IN
{
   HMODULE retval;
   utf16_t *lpModuleNameW;

   lpModuleNameW = Unicode_GetAllocUTF16(lpModuleName);

   retval = GetModuleHandleW(lpModuleNameW);

   free(lpModuleNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_OutputDebugString --
 *
 *    Wrapper around OutputDebugString. See MSDN.
 *
 *    OutputDebugString is odd in that the W version converts to
 *    local encoding then calls the A version.  We will shortcut
 *    this process - UTF8->local is safer than UTF8->UTF16->local,
 *    and in the common case (ASCII) no conversion is required at all.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

void
Win32U_OutputDebugString(ConstUnicode lpOutputString)  // IN
{
   char *escapedA;
   char *messageA;

   ASSERT(lpOutputString);

   if (Unicode_IsBufferValid(lpOutputString, -1, STRING_ENCODING_US_ASCII)) {
      /* 
       * Assume Win32 console pages are a strict superset of US-ASCII.
       * Thus, such strings can be output as-is.  Emit directly, which
       * bypasses potential out-of-memory failures.
       */
      OutputDebugStringA(lpOutputString);
      return;
   }

#if 0
   /*
    * We actually want to try the user's console output encoding, which our 
    * Unicode library does not (yet) support.
    *
    * Quote BenG:
    *    STRING_ENCODING_DEFAULT boils down to GetACP() (the encoding used by  
    *    CreateFileA() and friends):
    *   http://msdn.microsoft.com/en-us/library/ms776259(VS.85).aspx
    *
    *    whereas the console output encoding is based on the result of  
    *    GetConsoleOutputCP():
    *   http://msdn.microsoft.com/en-us/library/ms683169(VS.85).aspx
    *
    * Until we have a means of representing console encoding, the
    * replacement character implementation is a better choice.
    */
   if (Unicode_IsBufferValid(lpOutputString, -1, STRING_ENCODING_DEFAULT)) {
      char *lpOutputStringA;
      /* UTF8 string can be converted to current encoding losslessly. */

      lpOutputStringA = Unicode_GetAllocBytes(lpOutputString, STRING_ENCODING_DEFAULT);
      if (lpOutputStringA != NULL) {
         OutputDebugStringA(lpOutputStringA);
         free(lpOutputStringA);
         return;
      }
   }
#endif

   /* 
    * UTF8->local failed.  Fallback to replacement characters.  Do NOT
    * try UTF8->UTF16, as OutputDebugStringW will internally downsample 
    * to OutputDebugStringA and probably lose information.
    */
   escapedA = Unicode_EscapeBuffer(lpOutputString, -1, STRING_ENCODING_UTF8);
   if (escapedA) {
      messageA = Str_Asprintf(NULL, "%s: String with invalid encoding: %s",
                              __FUNCTION__, escapedA);
      if (messageA) {
         /* Escaped with prefix. */
         OutputDebugStringA(messageA);
      } else {
         /* Escaped without prefix. */
         OutputDebugStringA(escapedA);
      }
   } else {
      /* Escape failed.  Raw bytes, hope for the best. */
      OutputDebugStringA(lpOutputString);
   }
   free(messageA);
   free(escapedA);
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_MessageBox --
 *
 *    Wrapper around MessageBox. See MSDN.
 *
 * Results:
 *    On success, return a non-zero value corresponding to the button selected.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

int
Win32U_MessageBox(HWND hWnd,               // IN
                  ConstUnicode lpText,     // IN
                  ConstUnicode lpCaption,  // IN
                  UINT uType)              // IN
{
   int retval;
   utf16_t *lpTextW;
   utf16_t *lpCaptionW;

   ASSERT(lpText);

   lpTextW = Unicode_GetAllocUTF16(lpText);
   lpCaptionW = Unicode_GetAllocUTF16(lpCaption);

   retval = MessageBoxW(hWnd, lpTextW, lpCaptionW, uType);

   free(lpTextW);
   free(lpCaptionW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SetWindowText --
 *
 *    Wrapper around SetWindowText. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_SetWindowText(HWND hWnd,              // IN
                     ConstUnicode lpString)  // IN
{
   BOOL retval;
   utf16_t *lpStringW;

   ASSERT(lpString);

   lpStringW = Unicode_GetAllocUTF16(lpString);

   retval = SetWindowTextW(hWnd, lpStringW);

   free(lpStringW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateDirectory --
 *
 *    Wrapper around CreateDirectory. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_CreateDirectory(ConstUnicode lpPathName,                     // IN
                       LPSECURITY_ATTRIBUTES lpSecurityAttributes)  // IN
{
   BOOL retval;
   utf16_t *lpPathNameW;

   ASSERT(lpPathName);

   lpPathNameW = Unicode_GetAllocUTF16(lpPathName);

   retval = CreateDirectoryW(lpPathNameW, lpSecurityAttributes);


   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_RemoveDirectory --
 *
 *    Wrapper around RemoveDirectory. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_RemoveDirectory(ConstUnicode lpPathName)  // IN
{
   BOOL retval;
   utf16_t *lpPathNameW;

   ASSERT(lpPathName);

   lpPathNameW = Unicode_GetAllocUTF16(lpPathName);

   retval = RemoveDirectoryW(lpPathNameW);

   free(lpPathNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CopyFile --
 *
 *    Wrapper around CopyFile. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_CopyFile(ConstUnicode lpExistingFileName,  // IN
                ConstUnicode lpNewFileName,       // IN
                BOOL bFailIfExists)               // IN
{
   BOOL retval;
   utf16_t *lpExistingFileNameW;
   utf16_t *lpNewFileNameW;

   ASSERT(lpExistingFileName);
   ASSERT(lpNewFileName);

   lpExistingFileNameW = Unicode_GetAllocUTF16(lpExistingFileName);
   lpNewFileNameW = Unicode_GetAllocUTF16(lpNewFileName);

   retval = CopyFileW(lpExistingFileNameW, lpNewFileNameW, bFailIfExists);

   free(lpExistingFileNameW);
   free(lpNewFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CopyFileEx --
 *
 *    Wrapper around CopyFileEx. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_CopyFileEx(ConstUnicode lpExistingFileName,       // IN
                  ConstUnicode lpNewFileName,            // IN
                  LPPROGRESS_ROUTINE lpProgressRoutine,  // IN
                  LPVOID lpData,                         // IN
                  LPBOOL phCancel,                       // IN
                  DWORD dwCopyFlags)                     // IN
{
   BOOL retval;
   utf16_t *lpExistingFileNameW;
   utf16_t *lpNewFileNameW;

   ASSERT(lpExistingFileName);
   ASSERT(lpNewFileName);

   lpExistingFileNameW = Unicode_GetAllocUTF16(lpExistingFileName);
   lpNewFileNameW = Unicode_GetAllocUTF16(lpNewFileName);

   retval = CopyFileExW(lpExistingFileNameW, lpNewFileNameW, lpProgressRoutine,
                        lpData, phCancel, dwCopyFlags);

   free(lpExistingFileNameW);
   free(lpNewFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_MoveFileEx --
 *
 *    Wrapper around MoveFileEx. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_MoveFileEx(ConstUnicode lpExistingFileName,  // IN
                  ConstUnicode lpNewFileName,       // IN
                  DWORD dwFlags)                    // IN
{
   BOOL retval;
   utf16_t *lpExistingFileNameW;
   utf16_t *lpNewFileNameW;

   ASSERT(lpExistingFileName);

   lpExistingFileNameW = Unicode_GetAllocUTF16(lpExistingFileName);
   lpNewFileNameW = Unicode_GetAllocUTF16(lpNewFileName);

   retval = MoveFileExW(lpExistingFileNameW, lpNewFileNameW, dwFlags);

   free(lpExistingFileNameW);
   free(lpNewFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetFileAttributes --
 *
 *    Wrapper around GetFileAttributes. See MSDN.
 *
 * Results:
 *    Returns the file attributes, or INVALID_FILE_ATTRIBUTES on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

DWORD
Win32U_GetFileAttributes(ConstUnicode lpFileName)  // IN
{
   DWORD retval;
   utf16_t *lpFileNameW;

   ASSERT(lpFileName);

   lpFileNameW = Unicode_GetAllocUTF16(lpFileName);

   retval = GetFileAttributesW(lpFileNameW);

   free(lpFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_OpenSCManager --
 *
 *    Wrapper around OpenSCManager. See MSDN.
 *
 * Results:
 *    Returns a handle to the SCM database, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

SC_HANDLE
Win32U_OpenSCManager(ConstUnicode lpMachineName,   // IN
                     ConstUnicode lpDatabaseName,  // IN
                     DWORD dwDesiredAccess)        // IN
{
   SC_HANDLE retval;
   utf16_t *lpMachineNameW;
   utf16_t *lpDatabaseNameW;

   lpMachineNameW = Unicode_GetAllocUTF16(lpMachineName);
   lpDatabaseNameW = Unicode_GetAllocUTF16(lpDatabaseName);

   retval = OpenSCManagerW(lpMachineNameW, lpDatabaseNameW, dwDesiredAccess);

   free(lpMachineNameW);
   free(lpDatabaseNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateFileMapping --
 *
 *    Wrapper around CreateFileMapping. See MSDN.
 *
 * Results:
 *    Returns a handle to the new/existing file mapping, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_CreateFileMapping(HANDLE hFile,                        // IN
                         LPSECURITY_ATTRIBUTES lpAttributes,  // IN
                         DWORD flProtect,                     // IN
                         DWORD dwMaximumSizeHigh,             // IN
                         DWORD dwMaximumSizeLow,              // IN
                         ConstUnicode lpName)                 // IN
{
   HANDLE retval;
   utf16_t *lpNameW;

   lpNameW = Unicode_GetAllocUTF16(lpName);

   retval = CreateFileMappingW(hFile, lpAttributes, flProtect,
                               dwMaximumSizeHigh, dwMaximumSizeLow, lpNameW);

   free(lpNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SetFileAttributes --
 *
 *    Wrapper around SetFileAttributes. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_SetFileAttributes(ConstUnicode lpFileName,  // IN
                         DWORD dwFileAttributes)   // IN
{
   BOOL retval;
   utf16_t *lpFileNameW;

   ASSERT(lpFileName);

   lpFileNameW = Unicode_GetAllocUTF16(lpFileName);

   retval = SetFileAttributesW(lpFileNameW, dwFileAttributes);

   free(lpFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_OpenService --
 *
 *    Wrapper around OpenService. See MSDN.
 *
 * Results:
 *    Returns a handle to the service, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

SC_HANDLE
Win32U_OpenService(SC_HANDLE hSCManager,        // IN
                   ConstUnicode lpServiceName,  // IN
                   DWORD dwDesiredAccess)       // IN
{
   SC_HANDLE retval;
   utf16_t *lpServiceNameW;

   ASSERT(lpServiceName);

   lpServiceNameW = Unicode_GetAllocUTF16(lpServiceName);

   retval = OpenServiceW(hSCManager, lpServiceNameW, dwDesiredAccess);

   free(lpServiceNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CryptAcquireContext --
 *
 *    Wrapper around CryptAcquireContext. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_CryptAcquireContext(HCRYPTPROV* phProv,         // OUT
                           ConstUnicode pszContainer,  // IN
                           ConstUnicode pszProvider,   // IN
                           DWORD dwProvType,           // IN
                           DWORD dwFlags)              // IN
{
   BOOL retval;
   utf16_t *pszContainerW;
   utf16_t *pszProviderW;

   pszContainerW = Unicode_GetAllocUTF16(pszContainer);
   pszProviderW = Unicode_GetAllocUTF16(pszProvider);

   retval = CryptAcquireContextW(phProv, pszContainerW, pszProviderW,
                                 dwProvType, dwFlags);

   free(pszContainerW);
   free(pszProviderW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetDiskFreeSpace --
 *
 *    Wrapper around GetDiskFreeSpace. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetDiskFreeSpace(ConstUnicode lpRootPathName,      // IN
                        LPDWORD lpSectorsPerCluster,      // OUT
                        LPDWORD lpBytesPerSector,         // OUT
                        LPDWORD lpNumberOfFreeClusters,   // OUT
                        LPDWORD lpTotalNumberOfClusters)  // OUT
{
   BOOL retval;
   utf16_t *lpRootPathNameW;

   lpRootPathNameW = Unicode_GetAllocUTF16(lpRootPathName);

   retval = GetDiskFreeSpaceW(lpRootPathNameW, lpSectorsPerCluster,
                              lpBytesPerSector, lpNumberOfFreeClusters,
                              lpTotalNumberOfClusters);

   free(lpRootPathNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetDiskFreeSpaceEx --
 *
 *    Wrapper around GetDiskFreeSpaceEx. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetDiskFreeSpaceEx(ConstUnicode lpDirectoryName,             // IN
                          PULARGE_INTEGER lpFreeBytesAvailable,     // OUT
                          PULARGE_INTEGER lpTotalNumberoOfBytes,    // OUT
                          PULARGE_INTEGER lpTotalNumberofFreeBytes) // OUT
{
   BOOL retval;
   utf16_t *lpDirectoryNameW;

   lpDirectoryNameW = Unicode_GetAllocUTF16(lpDirectoryName);

   retval = GetDiskFreeSpaceExW(lpDirectoryNameW, lpFreeBytesAvailable,
                                lpTotalNumberoOfBytes, lpTotalNumberofFreeBytes);

   free(lpDirectoryNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SetCurrentDirectory --
 *
 *    Wrapper around SetCurrentDirectory. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_SetCurrentDirectory(ConstUnicode lpPathName)  // IN
{
   BOOL retval;
   utf16_t *lpPathNameW;

   ASSERT(lpPathName);

   lpPathNameW = Unicode_GetAllocUTF16(lpPathName);

   retval = SetCurrentDirectoryW(lpPathNameW);

   free(lpPathNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_LoadCursor --
 *
 *    Wrapper around LoadCursor. See MSDN.
 *
 * Results:
 *    Returns a handle to the cursor, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HCURSOR
Win32U_LoadCursor(HINSTANCE hInstance,        // IN
                  ConstUnicode lpCursorName)  // IN
{
   HCURSOR retval;
   utf16_t *lpCursorNameW;

   if (!IS_INTRESOURCE(lpCursorName)) {
      lpCursorNameW = Unicode_GetAllocUTF16(lpCursorName);
   } else {
      lpCursorNameW = (utf16_t *)lpCursorName;
   }

   retval = LoadCursorW(hInstance, lpCursorNameW);

   if (!IS_INTRESOURCE(lpCursorName)) {
      free(lpCursorNameW);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_FindResource --
 *
 *    Wrapper around FindResource. See MSDN.
 *
 * Results:
 *    Returns a handle to the resource's information block, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HRSRC
Win32U_FindResource(HMODULE hModule,      // IN
                    ConstUnicode lpName,  // IN
                    ConstUnicode lpType)  // IN
{
   HRSRC retval;
   utf16_t *lpNameW;
   utf16_t *lpTypeW;

   if (!IS_INTRESOURCE(lpName)) {
      lpNameW = Unicode_GetAllocUTF16(lpName);
   } else {
      lpNameW = (utf16_t *)lpName;
   }
   if (!IS_INTRESOURCE(lpType)) {
      lpTypeW = Unicode_GetAllocUTF16(lpType);
   } else {
      lpTypeW = (utf16_t *)lpType;
   }

   retval = FindResourceW(hModule, lpNameW, lpTypeW);

   if (!IS_INTRESOURCE(lpName)) {
      free(lpNameW);
   }
   if (!IS_INTRESOURCE(lpType)) {
      free(lpTypeW);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetFileSecurity --
 *
 *    Wrapper around GetFileSecurity. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetFileSecurity(ConstUnicode lpFileName,                    // IN
                       SECURITY_INFORMATION RequestedInformation,  // IN
                       PSECURITY_DESCRIPTOR pSecurityDescriptor,   // OUT
                       DWORD nLength,                              // IN
                       LPDWORD lpnLengthNeeded)                    // OUT
{
   BOOL retval;
   utf16_t *lpFileNameW;

   ASSERT(lpFileName);

   lpFileNameW = Unicode_GetAllocUTF16(lpFileName);

   retval = GetFileSecurityW(lpFileNameW, RequestedInformation,
                             pSecurityDescriptor, nLength, lpnLengthNeeded);

   free(lpFileNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateWindowEx --
 *
 *    Wrapper around CreateWindowEx. See MSDN.
 *
 * Results:
 *    Returns a handle to the new window, or NULL on error.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HWND
Win32U_CreateWindowEx(DWORD dwExStyle,            // IN
                      ConstUnicode lpClassName,   // IN
                      ConstUnicode lpWindowName,  // IN
                      DWORD dwStyle,              // IN
                      int x,                      // IN
                      int y,                      // IN
                      int nWidth,                 // IN
                      int nHeight,                // IN
                      HWND hWndParent,            // IN
                      HMENU hMenu,                // IN
                      HINSTANCE hInstance,        // IN
                      LPVOID lpParam)             // IN
{
   HWND retval;
   utf16_t *lpClassNameW;
   utf16_t *lpWindowNameW;

   ASSERT(lpWindowName);

   if (!IS_INTRESOURCE(lpClassName)) {
      lpClassNameW = Unicode_GetAllocUTF16(lpClassName);
   } else {
      lpClassNameW = (utf16_t *)lpClassName;
   }
   lpWindowNameW = Unicode_GetAllocUTF16(lpWindowName);

   retval = CreateWindowExW(dwExStyle, lpClassNameW, lpWindowNameW, dwStyle,
                            x, y, nWidth, nHeight, hWndParent, hMenu,
                            hInstance, lpParam);

   if (!IS_INTRESOURCE(lpClassName)) {
      free(lpClassNameW);
   }
   free(lpWindowNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_BeginUpdateResource --
 *
 *    Wrapper around BeginUpdateResource. See MSDN.
 *
 * Results:
 *    Returns update resource handle on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_BeginUpdateResource(ConstUnicode lpFileName,        // IN
                           BOOL bDeleteExistingResources)  // IN
{
   HANDLE retval;
   utf16_t *lpFileNameW;

   ASSERT(lpFileName);

   lpFileNameW = Unicode_GetAllocUTF16(lpFileName);

   retval = BeginUpdateResourceW(lpFileNameW, bDeleteExistingResources);

   free(lpFileNameW);
   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_UpdateResource --
 *
 *    Wrapper around UpdateResource. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_UpdateResource(HANDLE hUpdate,       // IN
                      ConstUnicode lpType,  // IN
                      ConstUnicode lpName,  // IN
                      WORD wLanguage,       // IN
                      LPVOID lpData,        // IN
                      DWORD cbData)         // IN
{
   BOOL retval;
   utf16_t *lpTypeW;
   utf16_t *lpNameW;

   if (!IS_INTRESOURCE(lpType)) {
      lpTypeW = Unicode_GetAllocUTF16(lpType);
   } else {
      lpTypeW = (utf16_t *)lpType;
   }
   if (!IS_INTRESOURCE(lpName)) {
      lpNameW = Unicode_GetAllocUTF16(lpName);
   } else {
      lpNameW = (utf16_t *)lpName;
   }

   retval = UpdateResourceW(hUpdate, lpTypeW, lpNameW, wLanguage, lpData,
                            cbData);

   if (!IS_INTRESOURCE(lpType)) {
      free(lpTypeW);
   }
   if (!IS_INTRESOURCE(lpName)) {
      free(lpNameW);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_QueryDosDevice --
 *
 *    Wrapper around QueryDosDevice. See MSDN. Returns a list of paths
 *    in "pathsList", and the number of paths in that list as the
 *    return value proper. Free with Unicode_FreeList().
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static int
Win32UQueryDosDeviceInt(ConstUnicode lpDeviceName, // IN (OPT)
                        Unicode **pathsList)       // IN/OUT
{
   int numPaths = -1;
   size_t size = MAX_PATH;
   utf16_t *pathsW = NULL;
   const utf16_t *deviceNameW;
   const utf16_t *start;
   DynBuf b;
   const utf16_t *ptr;
   const utf16_t *end;
   DWORD ret;

   ASSERT(pathsList);
   deviceNameW = UNICODE_GET_UTF16(lpDeviceName);
   DynBuf_Init(&b);

   /*
    * Attempt to get the drive strings
    */

   while (TRUE) {
      pathsW = Util_SafeRealloc(pathsW, size * sizeof(utf16_t));
      ret = QueryDosDeviceW(deviceNameW, pathsW, size);

      if (0 == ret) {
         if (ERROR_INSUFFICIENT_BUFFER != GetLastError()) {
            goto exit;
         }
      } else if (ret < size) {
         break;
      }
      size *= 2;
   }

   /*
    * We have a list of NUL-terminated strings, parse them and make a
    * nice list out of them.
    */

   numPaths = 0;
   start = pathsW;
   end = start + ret;
   for (ptr = start; ptr != end; ptr++) {
      if (!*ptr) {
        Unicode path;

        if (ptr == start) { /* Empty string => end of list. */
           break;
        }
        path = Unicode_AllocWithUTF16(start);
        if (!DynBuf_Append(&b, &path, sizeof path)) {
           Unicode_FreeList(DynBuf_Detach(&b), numPaths);
           numPaths = -1;
           goto exit;
        }
        numPaths++;
        start = ptr + 1;
     }
   }

   *pathsList = DynBuf_Detach(&b);

  exit:
   DynBuf_Destroy(&b);
   UNICODE_RELEASE_UTF16(deviceNameW);
   free(pathsW);

   return numPaths;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_QueryDosDevice --
 *
 *    Fixed-size wrapper around QueryDosDevice. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

DWORD
Win32U_QueryDosDevice(ConstUnicode lpDeviceName, // IN (OPT)
                      LPSTR lpTargetPath,        // IN/OUT
                      DWORD ucchMax)             // IN
{
   Unicode *paths = NULL;
   int numPaths;
   size_t retLen = 0;
   int ipath;
   DynBuf b;

   ASSERT(lpTargetPath);
   DynBuf_Init(&b);

   numPaths = Win32UQueryDosDeviceInt(lpDeviceName, &paths);

   if (-1 == numPaths) {
      goto exit;
   }

   /*
    * We have to form a doubly-NUL terminated list of
    * NUL-terminated strings.
    */
   for (ipath = 0; ipath < numPaths; ipath++) {
      if (!DynBuf_Append(&b, paths[ipath], strlen(UTF8(paths[ipath])) + 1)) {
         goto exit;
      }
   }

   /*
    * Add the last NUL.
    */
   if (!DynBuf_Append(&b, "\0", 1)) {
      goto exit;
   }

   /*
    * Copy to user buffer.
    */
   retLen = DynBuf_GetSize(&b);

   if (retLen > ucchMax) {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      retLen = 0;
      goto exit;
   } else {
      memcpy(lpTargetPath, DynBuf_Get(&b), retLen);
   }

  exit:
   DynBuf_Destroy(&b);
   Unicode_FreeList(paths, numPaths);

   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetTempPathInt --
 *
 *    Dynamic-size wrapper around GetTempPath. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetTempPathInt(void)
{
   Unicode tempPath = NULL;
   utf16_t *tempPathW = NULL;
   size_t size = MAX_PATH;
   DWORD ret;

   while (TRUE) {
      tempPathW = Util_SafeRealloc(tempPathW, size * sizeof(utf16_t));
      ret = GetTempPathW(size, tempPathW);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
      }
   }

   tempPath = Unicode_AllocWithUTF16(tempPathW);

exit:
   free(tempPathW);

   return tempPath;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetTempPath --
 *
 *    Fixed-size wrapper around GetTempPath. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

DWORD
Win32U_GetTempPath(DWORD nBufferLength, // IN
                   LPSTR lpBuffer)      // IN/OUT
{
   Unicode tempPath = Win32UGetTempPathInt();
   size_t retLen;

   if (NULL == tempPath) {
      return 0;
   } else if ((NULL == lpBuffer) || (0 == nBufferLength)) {
      retLen = strlen(UTF8(tempPath)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpBuffer, tempPath, nBufferLength,
                                       &retLen, STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
   }

   Unicode_Free(tempPath);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetEnvironmentVariableInt --
 *
 *    Dynamic-size wrapper around GetEnvironmentVariable. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetEnvironmentVariableInt(ConstUnicode lpName) // IN
{
   Unicode value = NULL;
   utf16_t *valueW = NULL;
   size_t size = 1024;
   DWORD ret;
   const utf16_t *nameW;

   ASSERT(lpName);
   nameW = UNICODE_GET_UTF16(lpName);

   while (TRUE) {
      valueW = Util_SafeRealloc(valueW, size * sizeof(utf16_t));
      ret = GetEnvironmentVariableW(nameW, valueW, size);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
      }
   }

   value = Unicode_AllocWithUTF16(valueW);

exit:
   UNICODE_RELEASE_UTF16(nameW);
   free(valueW);

   return value;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetEnvironmentVariable --
 *
 *    Fixed-size wrapper around GetEnvironmentVariable. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

DWORD
Win32U_GetEnvironmentVariable(ConstUnicode lpName,  // IN
                              LPSTR lpBuffer,       // IN/OUT
                              DWORD nSize)          // IN
{
   Unicode envVar = Win32UGetEnvironmentVariableInt(lpName);
   size_t retLen;

   if (NULL == envVar) {
      return 0;
   } else if ((NULL == lpBuffer) || (0 == nSize)) {
      retLen = strlen(UTF8(envVar)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpBuffer, envVar, nSize, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
   }

   Unicode_Free(envVar);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UExpandEnvironmentStringsInt --
 *
 *    Dynamic-size wrapper around ExpandEnvironmentStrings. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UExpandEnvironmentStringsInt(ConstUnicode lpSrc)
{
   Unicode envVar = NULL;
   wchar_t *envVarW = NULL;
   DWORD size = MAX_PATH;
   DWORD ret;
   const utf16_t *srcW = UNICODE_GET_UTF16(lpSrc);

   while (TRUE) {
      envVarW = Util_SafeRealloc(envVarW, size * sizeof(wchar_t));
      ret = ExpandEnvironmentStringsW(srcW, envVarW, size);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
      }
   }

   envVar = Unicode_AllocWithUTF16(envVarW);

exit:
   UNICODE_RELEASE_UTF16(srcW);
   free(envVarW);

   return envVar;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_ExpandEnvironmentStrings --
 *
 *    Fixed-size wrapper around ExpandEnvironmentStrings. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

DWORD
Win32U_ExpandEnvironmentStrings(ConstUnicode lpSrc,   // IN
                                LPSTR lpDst,          // OUT
                                DWORD nSize)          // IN
{
   Unicode expandedVar = Win32UExpandEnvironmentStringsInt(lpSrc);
   size_t retLen;

   if (NULL == expandedVar) {
      return 0;
   } else if ((lpDst == NULL) || (nSize == 0)) {
      retLen = strlen(UTF8(expandedVar)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpDst, expandedVar, nSize, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
   }

   Unicode_Free(expandedVar);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SetEnvironmentVariable --
 *
 *    Fixed-size wrapper around SetEnvironmentVariable. See MSDN.
 *
 * Results:
 *    Returns TRUE on success; FALSE if the function fails.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

#ifndef __MINGW32__
BOOL
#else
DWORD
#endif
Win32U_SetEnvironmentVariable(ConstUnicode lpName,  // IN
                              ConstUnicode lpValue) // IN
{
   BOOL retval;
   utf16_t *lpNameW;
   utf16_t *lpValueW;

   ASSERT(lpName);

   lpNameW = Unicode_GetAllocUTF16(lpName);
   lpValueW = Unicode_GetAllocUTF16(lpValue);

   retval = SetEnvironmentVariableW(lpNameW, lpValueW);

   free(lpNameW);
   free(lpValueW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetSystemDirectoryInt --
 *
 *    Dynamic-size wrapper around GetSystemDirectory. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetSystemDirectoryInt(void)
{
   Unicode systemDir = NULL;
   wchar_t *systemDirW = NULL;
   DWORD size = MAX_PATH;
   DWORD ret;

   while (TRUE) {
      systemDirW = Util_SafeRealloc(systemDirW, size * sizeof(utf16_t));
      ret = GetSystemDirectoryW(systemDirW, size);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;

      } else {
         size = ret;
      }
   }

   systemDir = Unicode_AllocWithUTF16(systemDirW);

exit:
   free(systemDirW);

   return systemDir;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetSystemDirectory --
 *
 *    Fixed-size wrapper around GetSystemDirectory. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

UINT
Win32U_GetSystemDirectory(LPSTR lpBuffer, // IN/OUT
                          UINT uSize)     // IN
{
   Unicode systemDir = Win32UGetSystemDirectoryInt();
   size_t retLen;

   if (NULL == systemDir) {
      return 0;
   } else if ((NULL == lpBuffer) || (0 == uSize)) {
      retLen = strlen(UTF8(systemDir)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpBuffer, systemDir, uSize, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
   }

   Unicode_Free(systemDir);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetUserNameInt --
 *
 *    Dynamic-size wrapper around GetUserName. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetUserNameInt(void)
{
   Unicode userName = NULL;
   utf16_t *userNameW = NULL;
   DWORD size = UNLEN + 1;
   BOOL ret;

   while (TRUE) {
      userNameW = Util_SafeRealloc(userNameW, size * sizeof(utf16_t));
      ret = GetUserNameW(userNameW, &size);

      if (!ret) {
         if (ERROR_INSUFFICIENT_BUFFER != GetLastError()) {
            goto exit;
         }
      } else {
         break;
      }
   }

   userName = Unicode_AllocWithUTF16(userNameW);

exit:
   free(userNameW);

   return userName;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetUserName --
 *
 *    Fixed-size wrapper around GetUserName. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

#ifndef __MINGW32__
UINT
#else
BOOL
#endif
Win32U_GetUserName(LPSTR lpBuffer,  // IN/OUT
                   LPDWORD lpnSize) // IN
{
   Unicode userName = Win32UGetUserNameInt();
   BOOL ret;

   if (NULL == userName) {
      return FALSE;
   } else if ((NULL == lpBuffer) || (0 == *lpnSize)) {
      *lpnSize = strlen(UTF8(userName)) + 1;
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      ret = FALSE;
   } else {
      size_t retLen;
      Bool noTrunc = Unicode_CopyBytes(lpBuffer, userName, *lpnSize, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
      *lpnSize = retLen + 1;
      ret = TRUE;
   }

   Unicode_Free(userName);
   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetTimeFormatInt --
 *
 *    Dyanmic-size wrapper around GetTimeFormat. See MSDN.
 *
 *    Note: The locale LOCALE_USER_DEFAULT is required, because we
 *    only know how to convert from the default locale to UTF-8.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetTimeFormatInt(LCID Locale,              // IN
                       DWORD dwFlags,            // IN
                       const SYSTEMTIME *lpTime, // IN
                       ConstUnicode lpFormat)    // IN
{
   Unicode timeStr = NULL;
   utf16_t *timeStrW = NULL;
   const utf16_t *formatStrW = NULL;
   size_t size = 0;

   if ((LOCALE_USER_DEFAULT != Locale) ||
       (dwFlags & LOCALE_USE_CP_ACP)) {
      /*
       * The LOCALE_USE_CP_ACP flag seems potentially dangerous,
       * disallow it.
       */
      NOT_IMPLEMENTED();
   }

   formatStrW = UNICODE_GET_UTF16(lpFormat);
   size = GetTimeFormatW(Locale, dwFlags, lpTime, formatStrW, NULL, 0);

   if ((0 == size) && (ERROR_INSUFFICIENT_BUFFER != GetLastError())) {
      goto exit;
   }

   timeStrW = Util_SafeMalloc(size * sizeof(utf16_t));
   size = GetTimeFormatW(Locale, dwFlags, lpTime, formatStrW, timeStrW, size);

   if (0 == size) {
      goto exit;
   }

   timeStr = Unicode_AllocWithUTF16(timeStrW);

exit:
   free(timeStrW);
   UNICODE_RELEASE_UTF16(formatStrW);

   return timeStr;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetTimeFormat --
 *
 *    Fixed-size wrapper around GetTimeFormat. See MSDN.
 *
 *    Note: The locale LOCALE_USER_DEFAULT is required, because we
 *    only know how to convert from the default locale to UTF-8.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

int
Win32U_GetTimeFormat(LCID Locale,              // IN
                     DWORD dwFlags,            // IN
                     const SYSTEMTIME *lpTime, // IN
                     ConstUnicode lpFormat,    // IN
                     LPSTR lpTimeStr,          // IN/OUT
                     int cchData)              // IN
{
   Unicode timeStr = Win32UGetTimeFormatInt(Locale, dwFlags, lpTime,
      lpFormat);
   size_t retLen;

   if (NULL == timeStr) {
      return 0;
   } else if ((NULL == lpTimeStr) || (0 == cchData)) {
      retLen = strlen(UTF8(timeStr)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpTimeStr, timeStr, cchData, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
      retLen++;
   }

   Unicode_Free(timeStr);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetDateFormatInt --
 *
 *    Dyanmic-size wrapper around GetDateFormat. See MSDN.
 *
 *    Note: The locale LOCALE_USER_DEFAULT is required, because we
 *    only know how to convert from the default locale to UTF-8.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetDateFormatInt(LCID Locale,              // IN
                       DWORD dwFlags,            // IN
                       const SYSTEMTIME *lpTime, // IN
                       ConstUnicode lpFormat)    // IN
{
   Unicode dateStr = NULL;
   utf16_t *dateStrW = NULL;
   const utf16_t *formatStrW = NULL;
   size_t size = 0;

   if ((LOCALE_USER_DEFAULT != Locale) ||
       (dwFlags & LOCALE_USE_CP_ACP)) {
      /*
       * The LOCALE_USE_CP_ACP flag seems potentially dangerous,
       * disallow it.
       */
      NOT_IMPLEMENTED();
   }

   formatStrW = UNICODE_GET_UTF16(lpFormat);
   size = GetDateFormatW(Locale, dwFlags, lpTime, formatStrW, NULL, 0);

   if ((0 == size) && (ERROR_INSUFFICIENT_BUFFER != GetLastError())) {
      goto exit;
   }

   dateStrW = Util_SafeMalloc(size * sizeof(utf16_t));
   size = GetDateFormatW(Locale, dwFlags, lpTime, formatStrW, dateStrW, size);

   if (0 == size) {
      goto exit;
   }

   dateStr = Unicode_AllocWithUTF16(dateStrW);

exit:
   free(dateStrW);
   UNICODE_RELEASE_UTF16(formatStrW);

   return dateStr;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetDateFormat --
 *
 *    Fixed-size wrapper around GetDateFormat. See MSDN.
 *
 *    Note: The locale LOCALE_USER_DEFAULT is required, because we
 *    only know how to convert from the default locale to UTF-8.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

int
Win32U_GetDateFormat(LCID Locale,              // IN
                     DWORD dwFlags,            // IN
                     const SYSTEMTIME *lpTime, // IN
                     ConstUnicode lpFormat,    // IN
                     LPSTR lpDateStr,          // IN/OUT
                     int cchData)              // IN
{
   Unicode dateStr = Win32UGetDateFormatInt(Locale, dwFlags, lpTime,
      lpFormat);
   size_t retLen;

   if (NULL == dateStr) {
      return 0;
   } else if ((NULL == lpDateStr) || (0 == cchData)) {
      retLen = strlen(UTF8(dateStr)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpDateStr, dateStr, cchData, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
      retLen++;
   }

   Unicode_Free(dateStr);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetComputerName --
 *
 *      Doesn't actually call GetComputerName, it uses 
 *      Win32U_GetComputerNameEx which wraps around GetComputerNameEx,
 *      but with a flag that gets the same information.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free). See 
 *      Win32U_GetComputerNameEx.
 *
 * Side effects:
 *      On failure GetComputerNameEx will set an error that can be retreived
 *      by GetLastError().
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_GetComputerName(void) // IN
{
   return Win32U_GetComputerNameEx(ComputerNameNetBIOS);
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_FormatMessage --
 *
 *      Dynamic-size wrapper around FormatMessage. See MSDN.
 *
 *      Note: This wrapper does NOT support the variant of this API that
 *      allows for a format string in lpSource, nor does it support an
 *      argument list. Also, only MAKELANGID(LANG_NEUTRAL,
 *      SUBLANG_DEFAULT) is supported for dwLanguageId, since we want to
 *      return UTF-8.
 *
 *      In other words, if you attempt to use this for anything more
 *      complicated than getting error strings from the system,
 *      in the default language, you fail at life.
 *
 *      Also, since this wrapper always returns an allocated string, this does
 *      not support FORMAT_MESSAGE_ALLOCATE_BUFFER.
 *
 * Results:
 *      Returns an allocated Unicode string.  The caller must free it with
 *      Unicode_Free.
 *
 *      Returns NULL on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_FormatMessage(DWORD dwFlags,      // IN
                     LPCVOID lpSource,   // IN
                     DWORD dwMessageId,  // IN
                     DWORD dwLanguageId, // IN
                     va_list *Arguments) // IN
{
   utf16_t *apiBufW = NULL;
   Unicode apiBuf = NULL;

   // We always allocate memory.  Don't confuse things.
   ASSERT_NOT_IMPLEMENTED((dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0);
   ASSERT_NOT_IMPLEMENTED((dwFlags & FORMAT_MESSAGE_FROM_STRING) == 0);
   ASSERT_NOT_IMPLEMENTED(Arguments == NULL);
   ASSERT_NOT_IMPLEMENTED(dwLanguageId == 0 ||
                          dwLanguageId == MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT) ||
                          dwLanguageId == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));

   if (0 == FormatMessageW(dwFlags | FORMAT_MESSAGE_ALLOCATE_BUFFER, lpSource,
                           dwMessageId, dwLanguageId, (LPWSTR) &apiBufW, 0,
                           NULL)) {
      goto exit;
   }

   apiBuf = Unicode_AllocWithUTF16(apiBufW);
   LocalFree(apiBufW);

  exit:
   return apiBuf;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_LoadString --
 *
 *      Deprecated.  Use Win32U_AllocString instead.
 *
 *      Fixed-size wrapper around LoadString. See MSDN.
 *
 *      Note: MSDN documents a feature whereby if "nBufferMax" is given
 *      as 0, a read-only pointer (that does not have to be free'd) to
 *      the resource will be returned in lpBuffer.  That feature is NOT
 *      supported by this wrapper since we must convert to UTF-8.
 *
 * Results:
 *      Intentionally returns nothing to discourage callers from incorrectly
 *      using the return value to determine if the string was truncated.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

void
Win32U_LoadString(HINSTANCE hInstance, // IN
                  UINT uID,            // IN
                  LPSTR lpBuffer,      // IN/OUT
                  int nBufferMax)      // IN
{
   Unicode str = NULL;

   if (nBufferMax <= 0) {
      /*
       * See comment above, this is NOT supported.
       */
      NOT_IMPLEMENTED();
   }

   str = Win32U_AllocString(hInstance, uID);
   if (str != NULL) {
      /*
       * Don't report truncation to user because the API doesn't either.
       */
      Unicode_CopyBytes(lpBuffer, str, nBufferMax, NULL, STRING_ENCODING_UTF8);
   }

   Unicode_Free(str);
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_AllocString --
 *
 *      Dynamic-size wrapper around LoadString. See MSDN.
 *
 *      XXX Does not follow the dynamic-size wrapper naming convention
 *      in order to accomodate existing callers. Will be fixed later.
 *
 * Results:
 *      Returns an allocated Unicode string.  The caller must free it with
 *      Unicode_Free.
 *
 *      Returns NULL on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Unicode
Win32U_AllocString(HINSTANCE hInstance, // IN
                   UINT uID)            // IN
{
   size_t size = 256;
   utf16_t *strW = NULL;
   Unicode str = NULL;

   while (TRUE) {
      int ret;
      strW = Util_SafeRealloc(strW, size * sizeof *strW);
      ret = LoadStringW(hInstance, uID, strW, size);
      if (ret <= 0) {
         // Resource doesn't exist.
         goto exit;
      } else if (ret == size - 1) {
         /*
          * We can't distinguish exact fit from truncation.  Assume
          * truncation occurred.
          */
         size *= 2;
      } else {
         // Success.
         break;
      }
   }

   str = Unicode_AllocWithUTF16(strW);

  exit:
   free(strW);

   return str;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetVolumePathName --
 *
 *    Wrapper around GetVolumePathName. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetVolumePathName(ConstUnicode lpszFileName, // IN
                         LPSTR lpszVolumePathName,  // IN/OUT
                         DWORD cchBufferLength)     // IN
{
   BOOL ret;
   const utf16_t *fileNameW;
   utf16_t *volumePathNameW;
   Unicode volumePathName = NULL;

   ASSERT(lpszFileName);
   fileNameW = UNICODE_GET_UTF16(lpszFileName);

   volumePathNameW = Util_SafeMalloc(cchBufferLength * sizeof(utf16_t));

   ret = GetVolumePathNameW(fileNameW, volumePathNameW, cchBufferLength);

   if (!ret) {
      goto exit;
   }

   volumePathName = Unicode_AllocWithUTF16(volumePathNameW);

   /*
    * Don't report truncation to user because the API doesn't either.
    */
   Unicode_CopyBytes(lpszVolumePathName, volumePathName, cchBufferLength,
                     NULL, STRING_ENCODING_UTF8);

  exit:
   UNICODE_RELEASE_UTF16(fileNameW);
   free(volumePathNameW);
   Unicode_Free(volumePathName);

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetVolumeNameForVolumeMountPoint --
 *
 *    Wrapper around GetVolumeNameForVolumeMountPoint. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetVolumeNameForVolumeMountPoint(
   ConstUnicode lpszVolumeMountPoint, // IN
   LPSTR lpszVolumeName,              // IN/OUT
   DWORD cchBufferLength)             // IN
{
   BOOL ret;
   const utf16_t *volumeMountPointW;
   utf16_t *volumeNameW;
   Unicode volumeName = NULL;

   ASSERT(lpszVolumeMountPoint);
   volumeMountPointW = UNICODE_GET_UTF16(lpszVolumeMountPoint);

   volumeNameW = Util_SafeMalloc(cchBufferLength * sizeof(utf16_t));

   ret = GetVolumeNameForVolumeMountPointW(volumeMountPointW, volumeNameW,
                                           cchBufferLength);

   if (!ret) {
      goto exit;
   }

   volumeName = Unicode_AllocWithUTF16(volumeNameW);

   /*
    * Don't report truncation to user because the API doesn't either.
    */
   Unicode_CopyBytes(lpszVolumeName, volumeName, cchBufferLength,
                     NULL, STRING_ENCODING_UTF8);

  exit:
   UNICODE_RELEASE_UTF16(volumeMountPointW);
   free(volumeNameW);
   Unicode_Free(volumeName);

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32UGetLongPathNameInt --
 *
 *    Dynamic-size wrapper around GetLongPathName. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static Unicode
Win32UGetLongPathNameInt(ConstUnicode lpszShortPath) // IN
{
   Unicode longPath = NULL;
   utf16_t *longPathW = NULL;
   const utf16_t *shortPathW;
   size_t size = MAX_PATH;
   DWORD ret;

   shortPathW = UNICODE_GET_UTF16(lpszShortPath);

   while (TRUE) {
      longPathW = Util_SafeRealloc(longPathW, size * sizeof(utf16_t));
      ret = GetLongPathNameW(shortPathW, longPathW, size);

      if (0 == ret) {
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
      }
   }

   longPath = Unicode_AllocWithUTF16(longPathW);

exit:
   UNICODE_RELEASE_UTF16(shortPathW);
   free(longPathW);

   return longPath;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetLongPathName --
 *
 *    Fixed-size wrapper around GetLongPathName. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

DWORD
Win32U_GetLongPathName(ConstUnicode lpszShortPath, // IN
                       LPSTR lpszLongPath,         // IN/OUT
                       DWORD cchBuffer)            // IN
{
   Unicode longPath = Win32UGetLongPathNameInt(lpszShortPath);
   size_t retLen;

   if (NULL == longPath) {
      return 0;
   } else if ((NULL == lpszLongPath) || (0 == cchBuffer)) {
      retLen = strlen(UTF8(longPath)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(lpszLongPath, longPath, cchBuffer,
                                       &retLen, STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
   }

   Unicode_Free(longPath);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_GetVersionEx --
 *
 *    Wrapper around GetVersionEx. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_GetVersionEx(LPOSVERSIONINFOA lpVersionInfo)  // IN/OUT
{
   BOOL retval;
   OSVERSIONINFOEXW versionInfoExW = { 0 };
   Bool noTrunc;
   Unicode buf;

   ASSERT(lpVersionInfo);
   ASSERT_NOT_IMPLEMENTED(
      lpVersionInfo->dwOSVersionInfoSize == sizeof(OSVERSIONINFOA) ||
      lpVersionInfo->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA));

   versionInfoExW.dwOSVersionInfoSize = sizeof versionInfoExW;
   retval = GetVersionExW((LPOSVERSIONINFOW)&versionInfoExW);

   if (retval) {
      buf = Unicode_AllocWithUTF16(versionInfoExW.szCSDVersion);
      noTrunc = Unicode_CopyBytes(lpVersionInfo->szCSDVersion, buf,
                                  sizeof lpVersionInfo->szCSDVersion,
                                  NULL, STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
      Unicode_Free(buf);

      lpVersionInfo->dwMajorVersion = versionInfoExW.dwMajorVersion;
      lpVersionInfo->dwMinorVersion = versionInfoExW.dwMinorVersion;
      lpVersionInfo->dwBuildNumber = versionInfoExW.dwBuildNumber;
      lpVersionInfo->dwPlatformId = versionInfoExW.dwPlatformId;

      if (lpVersionInfo->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA)) {
         LPOSVERSIONINFOEXA lpVersionInfoEx = (LPOSVERSIONINFOEXA)lpVersionInfo;

         lpVersionInfoEx->wServicePackMajor = versionInfoExW.wServicePackMajor;
         lpVersionInfoEx->wServicePackMinor = versionInfoExW.wServicePackMinor;
         lpVersionInfoEx->wSuiteMask = versionInfoExW.wSuiteMask;
         lpVersionInfoEx->wProductType = versionInfoExW.wProductType;
         lpVersionInfoEx->wReserved = versionInfoExW.wReserved;
      }
   }

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateProcess --
 *
 *    Wrapper around CreateProcess. See MSDN.
 *
 *    Note 1: Even though CreateProcessW accepts either Unicode or
 *    ANSI strings in the environment block at lpEnvironment, this wrapper
 *    only supports Unicode strings to avoid confusion over the encoding.  If
 *    lpEnvironment is NULL, CREATE_UNICODE_ENVIRONMENT is added to
 *    dwCreationFlags.  Otherwise, this wrapper asserts that
 *    CREATE_UNICODE_ENVIRONMENT is set by the caller.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_CreateProcess(ConstUnicode lpApplicationName,              // IN
                     ConstUnicode lpCommandLine,                  // IN
                     LPSECURITY_ATTRIBUTES lpProcessAttributes,   // IN
                     LPSECURITY_ATTRIBUTES lpThreadAttributes,    // IN
                     BOOL bInheritHandles,                        // IN
                     DWORD dwCreationFlags,                       // IN
                     LPVOID lpEnvironment,                        // IN
                     ConstUnicode lpCurrentDirectory,             // IN
                     LPSTARTUPINFOA lpStartupInfo,                // IN
                     LPPROCESS_INFORMATION lpProcessInformation)  // OUT
{
#ifndef __MINGW32__
   BOOL retval;
   utf16_t *lpApplicationNameW;
   utf16_t *lpCommandLineW;
   utf16_t *lpCurrentDirectoryW;
   STARTUPINFOEXW startupInfoW = { 0 };

   ASSERT(lpStartupInfo);

   lpApplicationNameW = Unicode_GetAllocUTF16(lpApplicationName);
   lpCommandLineW = Unicode_GetAllocUTF16(lpCommandLine);
   lpCurrentDirectoryW = Unicode_GetAllocUTF16(lpCurrentDirectory);

   if (lpEnvironment == NULL) {
      dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;
   }
   ASSERT_NOT_IMPLEMENTED((dwCreationFlags & CREATE_UNICODE_ENVIRONMENT) != 0);

   ASSERT_NOT_IMPLEMENTED(lpStartupInfo->cb <= sizeof startupInfoW);
   memcpy(&startupInfoW, lpStartupInfo, lpStartupInfo->cb);
   startupInfoW.StartupInfo.lpDesktop = Unicode_GetAllocUTF16(lpStartupInfo->lpDesktop);
   startupInfoW.StartupInfo.lpTitle = Unicode_GetAllocUTF16(lpStartupInfo->lpTitle);

   retval = CreateProcessW(lpApplicationNameW, lpCommandLineW,
                           lpProcessAttributes, lpThreadAttributes,
                           bInheritHandles, dwCreationFlags, lpEnvironment,
                           lpCurrentDirectoryW, &startupInfoW.StartupInfo,
                           lpProcessInformation);

   free(lpApplicationNameW);
   free(lpCommandLineW);
   free(lpCurrentDirectoryW);
   free(startupInfoW.StartupInfo.lpDesktop);
   free(startupInfoW.StartupInfo.lpTitle);

   return retval;
#else
   return FALSE;
#endif
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32ULookupAccountSidInt --
 *
 *    Dynamic-size wrapper around LookupAccountSid. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
BOOL
Win32ULookupAccountSidInt(ConstUnicode lpSystemName,          // IN
                          PSID lpSid,                         // IN
                          Unicode *lplpName,                  // OUT
                          Unicode *lplpReferencedDomainName,  // OUT
                          PSID_NAME_USE peUse)                // OUT
{
   BOOL retval;
   utf16_t *lpSystemNameW;
   utf16_t *lpNameW = NULL;
   utf16_t *lpRefDomNameW = NULL;
   DWORD nameSize = 0;
   DWORD domNameSize = 0;

   ASSERT(lplpName);
   ASSERT(lplpReferencedDomainName);

   lpSystemNameW = Unicode_GetAllocUTF16(lpSystemName);

   retval = LookupAccountSidW(lpSystemNameW, lpSid, NULL, &nameSize,
                              NULL, &domNameSize, peUse);
   ASSERT_NOT_IMPLEMENTED(retval == 0);
   if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      goto exit;
   }

   lpNameW = Util_SafeMalloc(nameSize * sizeof(utf16_t));
   lpRefDomNameW = Util_SafeMalloc(domNameSize * sizeof(utf16_t));
   retval = LookupAccountSidW(lpSystemNameW, lpSid, lpNameW, &nameSize,
                              lpRefDomNameW, &domNameSize, peUse);

   if (retval == 0) {
      goto exit;
   }

   *lplpName = Unicode_AllocWithUTF16(lpNameW);
   *lplpReferencedDomainName = Unicode_AllocWithUTF16(lpRefDomNameW);

exit:
   free(lpSystemNameW);
   free(lpNameW);
   free(lpRefDomNameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_LookupAccountSid --
 *
 *    Fixed-size wrapper around LookupAccountSid. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_LookupAccountSid(ConstUnicode lpSystemName,        // IN
                        PSID lpSid,                       // IN
                        Unicode lpName,                   // OUT
                        LPDWORD cchName,                  // IN/OUT
                        Unicode lpReferencedDomainName,   // OUT
                        LPDWORD cchReferencedDomainName,  // IN/OUT
                        PSID_NAME_USE peUse)              // OUT
{
   BOOL retval;
   Unicode name;
   Unicode refDomName;

   retval = Win32ULookupAccountSidInt(lpSystemName, lpSid, &name, &refDomName,
                                      peUse);

   if (retval) {
      if (*cchName == 0 || !Unicode_CopyBytes(lpName, name, (size_t)*cchName,
                                              NULL, STRING_ENCODING_UTF8)) {
         *cchName = strlen(UTF8(name)) + 1;
         SetLastError(ERROR_INSUFFICIENT_BUFFER);
         retval = FALSE;
      }
      if (*cchReferencedDomainName == 0 ||
          !Unicode_CopyBytes(lpReferencedDomainName, refDomName,
                             (size_t)*cchReferencedDomainName, NULL,
                             STRING_ENCODING_UTF8)) {
         *cchReferencedDomainName = strlen(UTF8(refDomName)) + 1;
         SetLastError(ERROR_INSUFFICIENT_BUFFER);
         retval = FALSE;
      }

      Unicode_Free(name);
      Unicode_Free(refDomName);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateMutex --
 *
 *    Wrapper around CreateMutex. See MSDN.
 *
 * Results:
 *    Returns a handle to the newly created mutex on success.
 *    Returns NULL if the function fails.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_CreateMutex(LPSECURITY_ATTRIBUTES lpSecurityAttributes,  // IN
                   BOOL bInitialOwner,                          // IN
                   ConstUnicode lpName)                         // IN
{
   HANDLE h;
   utf16_t *lpNameW = Unicode_GetAllocUTF16(lpName);

   h = CreateMutexW(lpSecurityAttributes,
                    bInitialOwner,
                    (const utf16_t *)lpNameW);

   free(lpNameW);
   return h;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateNamedPipe --
 *
 *    Wrapper around CreateNamedPipe. See MSDN.
 *
 * Results:
 *    Returns a handle to the server end of a named pipe instance on success.
 *    Returns INVALID_HANDLE_VALUE if the function fails.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_CreateNamedPipe(ConstUnicode lpName,                          // IN
                       DWORD dwOpenMode,                             // IN
                       DWORD dwPipeMode,                             // IN
                       DWORD nMaxInstances,                          // IN
                       DWORD nOutBufferSize,                         // IN
                       DWORD nInBufferSize,                          // IN
                       DWORD nDefaultTimeOut,                        // IN
                       LPSECURITY_ATTRIBUTES lpSecurityAttributes)   // IN
{
   HANDLE h;
   utf16_t *lpNameW = Unicode_GetAllocUTF16(lpName);

   h = CreateNamedPipeW(lpNameW,
                        dwOpenMode,
                        dwPipeMode,
                        nMaxInstances,
                        nOutBufferSize,
                        nInBufferSize,
                        nDefaultTimeOut,
                        lpSecurityAttributes);

   free(lpNameW);

   return h;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_WaitNamedPipe --
 *
 *    Wrapper around WaitNamedPipe. See MSDN.
 *
 * Results:
 *    Returns non-zero if an instance of the pipe is available
 *    before the time-out interval elapses.
 *    Returns 0 otherwise.
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_WaitNamedPipe(ConstUnicode lpName,                        // IN
                     DWORD nTimeOut)                             // IN
{
   utf16_t *lpNameW = Unicode_GetAllocUTF16(lpName);
   BOOL ret;

   ret = WaitNamedPipeW(lpNameW, nTimeOut);

   free(lpNameW);

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateSemaphore --
 *
 *    Wrapper around CreateSemaphore. See MSDN.
 *
 * Results:
 *    Returns a handle to the newly created mutex on success.
 *    Returns NULL if the function fails.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_CreateSemaphore(LPSECURITY_ATTRIBUTES lpSecurityAttributes,  // IN
                       LONG lInitialCount,                          // IN
                       LONG lMaximumCount,                          // IN
                       ConstUnicode lpName)                         // IN
{
   HANDLE h;
   utf16_t *lpNameW = Unicode_GetAllocUTF16(lpName);

   h = CreateSemaphoreW(lpSecurityAttributes,
                        lInitialCount,
                        lMaximumCount,
                        (const utf16_t *)lpNameW);

   free(lpNameW);
   return h;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_OpenSemaphore --
 *
 *    Wrapper around OpenSemaphore. See MSDN.
 *
 * Results:
 *    Returns a handle to the semaphore object.
 *    Returns NULL if the function fails.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_OpenSemaphore(DWORD dwDesiredAccess,    // IN
                     BOOL bInheritHandle,      // IN
                     ConstUnicode lpName)      // IN
{
   HANDLE h;
   utf16_t *lpNameW = Unicode_GetAllocUTF16(lpName);

   h = OpenSemaphoreW(dwDesiredAccess,
                      bInheritHandle,
                      lpNameW);

   free(lpNameW);
   return h;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_OpenEvent --
 *
 *    Wrapper around OpenEvent. See MSDN.
 *
 * Results:
 *    Returns a handle to the event object.
 *    Returns NULL if the function fails.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

HANDLE
Win32U_OpenEvent(DWORD dwDesiredAccess,    // IN
                 BOOL bInheritHandle,      // IN
                 ConstUnicode lpName)      // IN
{
   HANDLE h;
   utf16_t *lpNameW = Unicode_GetAllocUTF16(lpName);

   h = OpenEventW(dwDesiredAccess,
                  bInheritHandle,
                  lpNameW);

   free(lpNameW);
   return h;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_ReportEvent --
 *
 *    Wrapper around Report. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
Win32U_ReportEvent(HANDLE hEventLog,
                   WORD wType,
                   WORD wCategory,
                   DWORD dwEventID,
                   PSID lpUserSid,
                   WORD wNumStrings,
                   DWORD dwDataSize,
                   ConstUnicode *lpStrings,
                   LPVOID lpRawData)
{
   utf16_t **stringsW;
   BOOL ret;
   uint32 i;

   /*
    * make an array of utf16_t strings
    */

   if (wNumStrings) {

      stringsW = Util_SafeMalloc(wNumStrings * sizeof (utf16_t*));

      for (i = 0; i < wNumStrings; i++) {
         stringsW[i] = Unicode_GetAllocUTF16(lpStrings[i]);
      }
   } else {

      stringsW = NULL;
   }

   ret = ReportEventW(hEventLog, wType, wCategory, dwEventID,
                      lpUserSid, wNumStrings, dwDataSize,
                      stringsW, lpRawData);

   for (i = 0; i < wNumStrings; i++) {
      free(stringsW[i]);
   }

   free(stringsW);

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CreateService --
 *
 *      Wrapper around CreateService. See MSDN.
 *
 * Returns:
 *      Returns a handle to the service, or NULL on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

SC_HANDLE
Win32U_CreateService(SC_HANDLE hSCManager,             // IN
                     ConstUnicode lpServiceName,       // IN
                     ConstUnicode lpDisplayName,       // IN
                     DWORD dwDesiredAccess,            // IN
                     DWORD dwServiceType,              // IN
                     DWORD dwStartType,                // IN
                     DWORD dwErrorControl,             // IN
                     ConstUnicode lpBinaryPathName,    // IN
                     ConstUnicode lpLoadOrderGroup,    // IN
                     LPDWORD lpdwTagId,                // OUT
                     ConstUnicode lpDependencies,      // IN
                     ConstUnicode lpServiceStartName,  // IN
                     ConstUnicode lpPassword)          // IN
{
   SC_HANDLE retval;
   utf16_t *lpServiceNameW;
   utf16_t *lpDisplayNameW;
   utf16_t *lpBinaryPathNameW;
   utf16_t *lpLoadOrderGroupW;
   utf16_t *lpDependenciesW;
   utf16_t *lpServiceStartNameW;
   utf16_t *lpPasswordW;

   ASSERT(hSCManager);
   ASSERT(lpServiceName);
   ASSERT(lpBinaryPathName);

   lpServiceNameW = Unicode_GetAllocUTF16(lpServiceName);
   lpDisplayNameW = Unicode_GetAllocUTF16(lpDisplayName);
   lpBinaryPathNameW = Unicode_GetAllocUTF16(lpBinaryPathName);
   lpLoadOrderGroupW = Unicode_GetAllocUTF16(lpLoadOrderGroup);
   lpServiceStartNameW = Unicode_GetAllocUTF16(lpServiceStartName);
   lpPasswordW = Unicode_GetAllocUTF16(lpPassword);
   if (lpDependencies != NULL) {
      ssize_t length = 0;

      while (lpDependencies[length] != '\0') {
         /* Move the index to the byte after the delimiting NUL */
         length += strlen(&lpDependencies[length]) + 1;
      }

      lpDependenciesW = Unicode_GetAllocBytesWithLength(lpDependencies,
                                                        STRING_ENCODING_UTF16,
                                                        length);
   } else {
      lpDependenciesW = NULL;
   }

   retval = CreateServiceW(hSCManager, lpServiceNameW, lpDisplayNameW,
                           dwDesiredAccess, dwServiceType, dwStartType,
                           dwErrorControl, lpBinaryPathNameW,
                           lpLoadOrderGroupW, lpdwTagId, lpDependenciesW,
                           lpServiceStartNameW, lpPasswordW);

   free(lpServiceNameW);
   free(lpDisplayNameW);
   free(lpBinaryPathNameW);
   free(lpLoadOrderGroupW);
   free(lpDependenciesW);
   free(lpServiceStartNameW);
   Util_ZeroFreeStringW(lpPasswordW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SHCopyKey --
 *
 *      Trivial wrapper for SHCopyKey function; see MSDN.
 *
 * Returns:
 *      Returns ERROR_SUCCESS on success, or an error code from winerror.h
 *      on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

LSTATUS
Win32U_SHCopyKey(HKEY srcKey,         // IN
                 ConstUnicode subKey, // IN
                 HKEY dstKey,         // IN
                 DWORD reserved)      // IN: reserved, must be NULL.
{
   utf16_t *subKeyW;
   LSTATUS res;

   if (!g_hShlwapi) {
      g_hShlwapi = LoadLibraryA("shlwapi.dll");
   }

   if (g_hShlwapi && !g_SHCopyKeyWFn) {
      g_SHCopyKeyWFn = (SHCopyKeyWFnType) GetProcAddress(g_hShlwapi,
                                                         "SHCopyKeyW");
   }

   if (!g_SHCopyKeyWFn) {
      return E_UNEXPECTED;
   }

   subKeyW = UNICODE_GET_UTF16(subKey);
   res = (*g_SHCopyKeyWFn)(srcKey, subKeyW, dstKey, reserved);
   UNICODE_RELEASE_UTF16(subKeyW);

   return res;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SHDeleteKey --
 *
 *      Trivial wrapper for SHDeleteKey function; see MSDN.
 *
 * Returns:
 *      Returns ERROR_SUCCESS on success, or an error code from winerror.h
 *      on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */
LSTATUS
Win32U_SHDeleteKey(HKEY key,            // IN
                   ConstUnicode subKey) // IN
{
   utf16_t *subKeyW;
   LSTATUS res;

   if (!g_hShlwapi) {
      g_hShlwapi = LoadLibraryA("shlwapi.dll");
   }

   if (g_hShlwapi && !g_SHDeleteKeyWFn) {
      g_SHDeleteKeyWFn = (SHDeleteKeyWFnType) GetProcAddress(g_hShlwapi,
                                                             "SHDeleteKeyW");
   }

   if (!g_SHDeleteKeyWFn) {
      return E_UNEXPECTED;
   }

   subKeyW = UNICODE_GET_UTF16(subKey);
   res = (*g_SHDeleteKeyWFn)(key, subKeyW);
   UNICODE_RELEASE_UTF16(subKeyW);

   return res;
}
