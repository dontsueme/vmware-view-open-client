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
 * Win32Util.c --
 *
 *    misc Windows util functions
 */

#include <string.h>
#include <stdio.h>
#include <direct.h>
#include <windows.h>
#ifndef __MINGW32__
#include <dbghelp.h>
#endif
#include <shlobj.h>
#include <io.h>
#include <fcntl.h>
#include <winsock.h>

#include "vmware.h"
#include "productState.h"
#include "vm_version.h"
#include "vm_group.h"
#include "util.h"
#include "file.h"
#include "win32u.h"
#include "msg.h"
#include "win32util.h"
#include "str.h"
#include "log.h"
#include "panic.h"
#include "unicode.h"
#include "dynbuf.h"

#include <aclapi.h>
#include <accctrl.h>

#define LOGLEVEL_MODULE win32util
#include "loglevel_user.h"

#define LGPFX "Win32Util: "

#ifdef __MINGW32__
// XXX - can we get this from a more reliable source, i.e., an include file?
#define SECURITY_MAX_SID_SIZE 68
#endif

/*
 * Warning 4748: /GS (protect against local overruns) interacts poorly
 * with __asm.  It emits a warning that it cannot protect the function,
 * which causes the compile to error out.  Suppress the warning.
 */

#pragma warning( disable : 4748 )

/*
 * Module types
 */

typedef BOOL (WINAPI *CreateWellKnownSidFnType)(WELL_KNOWN_SID_TYPE, PSID,
                                                PSID, DWORD *);
typedef BOOL (WINAPI *SetSecurityDescriptorControlFnType)
   (PSECURITY_DESCRIPTOR pSecurityDescriptor,
    SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest,
    SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet);
typedef BOOL (WINAPI *ChangeServiceConfig2WFnType)(SC_HANDLE hService,
                                                   DWORD dwInfoLevel,
                                                   LPVOID lpInfo);

/*
 * Module globals
 */

static const DWORD AUTORUN_VALUE_ON = 0x95;
static const DWORD AUTORUN_VALUE_OFF = 0xFF;
static const int VERSION_VISTA = 6;

/*
 * Local enums
 */

typedef enum RegistryError {
   REGISTRY_SUCCESS,
   REGISTRY_UNKNOWN_ERROR,
   REGISTRY_ACCESS_DENIED,
   REGISTRY_KEY_DOES_NOT_EXIST,
   REGISTRY_TYPE_MISMATCH
} RegistryError;

/*
 * Local function prototypes
 */

static Unicode W32UtilGetVmwareCommonAppDataPath(void);
static Unicode W32UtilGetLocalAppDataPath(void);

static RegistryError W32UtilGetRegDWORD(HKEY base, const char *subPath,
                                        const char *var, DWORD *value);
static RegistryError W32UtilSetRegDWORD(HKEY base, const char *subPath,
                                        const char *var, DWORD value);
static RegistryError W32UtilRegDelete(HKEY base, const char *subPath,
                                      const char *var);


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetInstallPath --
 *
 *      Return the path (ending in a slash) to the directory in which
 *      the current product was installed. (On 64-bit Windows, this
 *      will be either the install directory chosen at install time,
 *      or the default 32-bit install directory under "Program Files
 *      (x86)").
 *
 * Results:
 *      A Unicode string, empty or a path (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetInstallPath(void)
{
   DWORD result, type;
   char *regPath = NULL;
   HKEY key = NULL;
   DWORD size = MAX_PATH * sizeof(wchar_t);
   Unicode path = NULL;
   wchar_t *pathW = NULL;

   result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, ProductState_GetRegistryPath(),
                          0, KEY_READ | VMW_KEY_WOW64_32KEY, &key);

   if (result != ERROR_SUCCESS) {
      /*
       * If an application supplied its own application name to ProductState
       * and there isn't a registry entry for it, default to the registry entry
       * for the product that the application is bundled with.
       * For example: "VMware Workstation".
       */

      regPath = ProductState_GetRegistryPathForProduct(PRODUCT_SHORT_NAME);
      result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0,
                             KEY_READ | VMW_KEY_WOW64_32KEY, &key);
      free(regPath);
      if (result != ERROR_SUCCESS) {
         goto exit;
      }
   }

   pathW = Util_SafeMalloc(size);
   result = RegQueryValueExW(key, L"InstallPath", NULL, &type,
                             (LPBYTE) pathW, &size);
   if (ERROR_MORE_DATA == result) {
      pathW = Util_SafeRealloc(pathW, size);
      result = RegQueryValueExW(key, L"InstallPath", NULL, &type,
                                (LPBYTE) pathW, &size);
      if (ERROR_SUCCESS != result) {
         goto exit;
      }
   } else if (ERROR_SUCCESS != result) {
      goto exit;
   }

   // don't assume NUL-termination
   path = Unicode_AllocWithLength(pathW, size, STRING_ENCODING_UTF16);

   // always end in slash
   if (!Unicode_EndsWith(path, "/") &&
       !Unicode_EndsWith(path, "\\")) {
      Unicode path2 = Unicode_Append(path, "\\");

      Unicode_Free(path);
      path = path2;
   }

  exit:
   if (key) {
      RegCloseKey(key);
   }

   if (!path) {
      path = Unicode_Duplicate("");
   }

   free(pathW);

   return path;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetInstallPath64 --
 *
 *      On 64-bit Windows, return a path (ending in a slash) to the
 *      special install directory for 64-bit binaries (should be under
 *      "Program Files" always). Otherwise return the empty string.
 *
 * Results:
 *      A Unicode string, empty or a path (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetInstallPath64(void)
{
   DWORD result, type;
   char *regPath = NULL;
   HKEY key = NULL;
   DWORD size = MAX_PATH * sizeof(wchar_t);
   Unicode path = NULL;
   wchar_t *pathW = NULL;

   result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, ProductState_GetRegistryPath(),
                          0, KEY_READ | VMW_KEY_WOW64_32KEY, &key);

   if (result != ERROR_SUCCESS) {
      /*
       * If an application supplied its own application name to ProductState
       * and there isn't a registry entry for it, default to the registry entry
       * for the product that the application is bundled with.
       * For example: "VMware Workstation".
       */

      regPath = ProductState_GetRegistryPathForProduct(PRODUCT_SHORT_NAME);
      result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0,
                             KEY_READ | VMW_KEY_WOW64_32KEY, &key);

      free(regPath);
      if (result != ERROR_SUCCESS) {
         goto exit;
      }
   }

   pathW = Util_SafeMalloc(size);
   result = RegQueryValueExW(key, L"InstallPath64", NULL, &type,
                             (LPBYTE) pathW, &size);
   if (ERROR_MORE_DATA == result) {
      pathW = Util_SafeRealloc(pathW, size);
      result = RegQueryValueExW(key, L"InstallPath64", NULL, &type,
                                (LPBYTE) pathW, &size);
      if (ERROR_SUCCESS != result) {
         goto exit;
      }
   } else if (ERROR_SUCCESS != result) {
      goto exit;
   }

   // don't assume NUL-termination
   path = Unicode_AllocWithLength(pathW, size, STRING_ENCODING_UTF16);

   // always end in slash
   if (!Unicode_EndsWith(path, "/") &&
       !Unicode_EndsWith(path, "\\")) {
      Unicode path2 = Unicode_Append(path, "\\");

      Unicode_Free(path);
      path = path2;
   }

  exit:
   if (key) {
      RegCloseKey(key);
   }

   if (!path) {
      path = Unicode_Duplicate("");
   }

   free(pathW);

   return path;
}


/*
 *----------------------------------------------------------------------
 *
 * W32UtilGetLocalAppDataPath --
 *
 *      Return the path to the Local AppData directory.
 *
 * Results:
 *      A Unicode string, empty or a path (free with Unicode_Free).
 *
 * Side effects:
 *      Directory created if doesn't exist.
 *
 *----------------------------------------------------------------------
 */

static Unicode
W32UtilGetLocalAppDataPath(void)
{
   Unicode path, path2;

   if (FAILED(Win32U_SHGetFolderPath(NULL,
                                     CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                     NULL, 0, &path))) {
      return NULL;
   }

   path2 = Unicode_Join(path, DIRSEPS, PRODUCT_GENERIC_NAME, NULL);
   Unicode_Free(path);

   return path2;
}


/*
 *----------------------------------------------------------------------
 *
 * W32UtilGetCommonAppDataPath --
 *
 *      Return the path to the Common AppData folder for this application.
 *      It is typically
 *      \Documents and Settings\All Users\Application Data\VMware\<product>
 *
 * Results:
 *      NULL or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      Directory created if doesn't exist.
 *
 *----------------------------------------------------------------------
 */

static Unicode
W32UtilGetCommonAppDataPath(void)
{
   Unicode basePath;
   Unicode path = NULL;

   if (!(basePath = W32UtilGetVmwareCommonAppDataPath())) {
      return NULL;
   }

   path = Unicode_Join(basePath, DIRSEPS, ProductState_GetName(), NULL);
   Unicode_Free(basePath);

   if (!File_EnsureDirectory(path)) {
      Unicode_Free(path);

      return NULL;
   }

   return path;
}


/*
 *----------------------------------------------------------------------
 *
 * W32UtilGetVmwareCommonAppDataPath --
 *
 *      Return the path to VMware Common AppData folder for this application.
 *      It is typically
 *      \Documents and Settings\All Users\Application Data\VMware
 *
 * Results:
 *      NULL or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      Directory created if doesn't exist.
 *
 *----------------------------------------------------------------------
 */

static Unicode
W32UtilGetVmwareCommonAppDataPath(void)
{
   DWORD result, type;
   HKEY key = NULL;
   Unicode path = NULL;
   Unicode path2 = NULL;
   wchar_t *pathW = NULL;

   /*
    * See if the Windows Registry has that path defined.
    * Note that we don't create this registry key as part of installation
    * or via any of our applications/components. The user has to manually
    * edit the Windows registry and add the key there.
    */

   result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, ProductState_GetRegistryPath(),
                          0, KEY_READ | VMW_KEY_WOW64_32KEY, &key);

   if (result == ERROR_SUCCESS) {
      DWORD size = MAX_PATH * sizeof(wchar_t);
      pathW = Util_SafeMalloc(size);

      result = RegQueryValueExW(key, L"AppDataPath", NULL, &type,
                                (LPBYTE) pathW, &size);
      if (result == ERROR_MORE_DATA) {
         pathW = Util_SafeRealloc(pathW, size);
         result = RegQueryValueExW(key, L"AppDataPath", NULL, &type,
                                   (LPBYTE) pathW, &size);

         if (result == ERROR_SUCCESS) {
            // don't assume NUL-termination
            path = Unicode_AllocWithLength(pathW, size, STRING_ENCODING_UTF16);
         }
      }
   }

   if (!path) {
      /*
       * Could not get it from the registry. Default to the Common Application
       * Data folder.
       */

      // Get the Common Application data folder - create if it doesn't exist.
      if (FAILED(Win32U_SHGetFolderPath(NULL,
                CSIDL_COMMON_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, &path))) {
         goto exit;
      }
   }

   ASSERT(path);

   // Make sure the <product> subdirectory exists.
   path2 = Unicode_Join(path, DIRSEPS, PRODUCT_GENERIC_NAME, NULL);
   if (!File_EnsureDirectory(path2)) {
      Unicode_Free(path2);
      path2 = NULL;
      goto exit;
   }

  exit:
   Unicode_Free(path);
   free(pathW);
   if (key) {
      RegCloseKey(key);
   }

   return path2;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetMyDocumentPath --
 *
 *      Return the path to the AppData directory.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetMyDocumentPath(void)
{
   HRESULT hr = S_OK;
   Unicode path = NULL;

   if (FAILED(hr = Win32U_SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0,
                                          &path))) {
      Log("UTIL: Failed to get 'My Documents' folder. hr = 0x%lx. "
          "Trying 'App Data'.\n", hr);

      if (FAILED(hr = Win32U_SHGetFolderPath(NULL, CSIDL_APPDATA , NULL, 0,
                                             &path))) {
         // This is a workaround for the uncommon case in bug #12198  - ticho
         Log("UTIL: Failed to get 'My Documents' and 'App Data' folders. "
             "hr = 0x%lx\n", hr);

         return W32Util_GetInstallPath();
      }
   }

   return path;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetMyVideoPath --
 *
 *      Return the path to the "My Video" directory. Gives the caller
 *      the choice of returning the "My Documents" on failure.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetMyVideoPath(BOOL myDocumentsOnFail) // IN: whether to return
                                               // the "My Documents" path
                                               // on failure
{
   HRESULT hr = S_OK;
   Unicode path = NULL;

   if (FAILED(hr = Win32U_SHGetFolderPath(NULL, CSIDL_MYVIDEO, NULL, 0,
                                          &path))) {
      if (myDocumentsOnFail) {
         return W32Util_GetMyDocumentPath();
      } else {
         return W32Util_GetInstallPath();
      }
   }

   return path;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetDefaultVMPath --
 *
 *      Return the path to the default VM location
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetDefaultVMPath(ConstUnicode pref) // IN
{
   Unicode appDataDir = NULL;
   Unicode vmPath = NULL;
   OSVERSIONINFOA osvi = { 0 };

   if (pref != NULL && !Unicode_IsEmpty(pref)) {
      vmPath = Unicode_Duplicate(pref); /* use this value */
   } else {
      appDataDir = W32Util_GetMyDocumentPath();

      osvi.dwOSVersionInfoSize = sizeof osvi;
      Win32U_GetVersionEx(&osvi);
      if (osvi.dwMajorVersion < VERSION_VISTA) {
         vmPath = Unicode_Append(appDataDir, DIRSEPS "My Virtual Machines");
      } else {
         vmPath = Unicode_Append(appDataDir, DIRSEPS "Virtual Machines");
      }
   }

   Unicode_Free(appDataDir);

   return vmPath;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetInstalledFilePath --
 *
 *      Return the full path of a file in the 32-bit install
 *      directory. This means taking the file name given and
 *      prepending the install path.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetInstalledFilePath(ConstUnicode fileName) // IN
{
   Unicode base = W32Util_GetInstallPath();

   ASSERT_NOT_IMPLEMENTED(base != NULL);
   if (fileName != NULL && base[0] != '\0') {
      Unicode fullPath = Unicode_Append(base, fileName);

      Unicode_Free(base);

      return fullPath;
   } else {
      return base;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetInstalledFilePath64 --
 *
 *      Return the full path of a file in the 64-bit install
 *      directory. This means taking the file name given and
 *      prepending the install path.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetInstalledFilePath64(ConstUnicode fileName) // IN
{
   Unicode base = W32Util_GetInstallPath64();

   ASSERT_NOT_IMPLEMENTED(base != NULL);
   if (fileName != NULL && base[0] != '\0') {
      Unicode fullPath = Unicode_Append(base, fileName);

      Unicode_Free(base);

      return fullPath;
   } else {
      return base;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetAppDataFilePath --
 *
 *      Return the full path of a file in the AppData directory for the
 *      user.
 *
 * Results:
 *      Full path to the given file in the user's AppData directory, or
 *      full path to the AppData directory itself if no fileName is
 *      specified. Returns NULL if user's AppData directory does not
 *      exist. Free with Unicode_Free().
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetAppDataFilePath(ConstUnicode fileName) // IN
{
   Unicode base = W32Util_GetAppDataPath();

   if (!base) {
      return NULL;
   }

   if (fileName) {
      Unicode fullPath = Unicode_Join(base, DIRSEPS, fileName, NULL);

      Unicode_Free(base);

      return fullPath;
   } else {
      return base;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetLocalAppDataFilePath --
 *
 *      Return the full path of a file in the LocalAppData directory for the
 *      user.
 *
 * Results:
 *      Full path to the given file in the user's LocalAppData directory, or
 *      full path to the LocalAppData directory itself if no fileName is
 *      specified. Returns NULL if user's AppData directory does not
 *      exist. Free with Unicode_Free();
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetLocalAppDataFilePath(ConstUnicode fileName) // IN
{
   Unicode base = W32UtilGetLocalAppDataPath();

   if (!base) {
      return NULL;
   }

   if (fileName) {
      Unicode fullPath = Unicode_Append(base, fileName);

      Unicode_Free(base);

      return fullPath;
   } else {
      return base;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetCommonAppDataFilePath --
 *
 *      Return the full path of a file in the Common AppData directory
 *      of an installed VMware product. This means taking the file
 *      name given and prepending the Common AppData path.
 *
 * Results:
 *      Unicode string giving the path to the file (free with
 *      Unicode_Free()).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetCommonAppDataFilePath(ConstUnicode fileName) // IN
{
   Unicode base = W32UtilGetCommonAppDataPath();

   if (!base) {
      return NULL;
   }

   if (fileName) {
      Unicode fullPath = Unicode_Join(base, DIRSEPS, fileName, NULL);

      Unicode_Free(base);

      return fullPath;
   } else {
      return base;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetVmwareCommonAppDataFilePath --
 *
 *      Return the full path of a file in VMware Common AppData directory.
 *      This means taking the file name given and prepending the Common
 *      AppData path.
 *
 * Results:
 *      Unicode string giving the path to the file (free with
 *      Unicode_Free()).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_GetVmwareCommonAppDataFilePath(ConstUnicode fileName) // IN
{
   Unicode base = W32UtilGetVmwareCommonAppDataPath();

   if (!base) {
      return NULL;
   }

   if (fileName) {
      Unicode fullPath = Unicode_Join(base, DIRSEPS, fileName, NULL);

      Unicode_Free(base);

      return fullPath;
   } else {
      return base;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_OpenProductRegKey --
 *
 *    Open the product registry key and return a handle to it.
 *
 * Returns:
 *    A handle to the product registry key if the open was successful.
 *    NULL otherwise.
 *
 * Side effects:
 *    Opens the product registry key for writing. This must be closed
 *    by the calling program.
 *
 *----------------------------------------------------------------------
 */

HKEY
W32Util_OpenProductRegKey(REGSAM access) // Define access given to opened key
{
   HKEY key;
   LONG result = Win32U_RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                                     ProductState_GetRegistryPath(),
                                     0, access | VMW_KEY_WOW64_32KEY, &key);

   return (result == ERROR_SUCCESS) ? key : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_OpenUserRegKey --
 *
 *    Open the product registry key for the current user and return a
 *    handle to it.
 *
 * Returns:
 *    A handle to the product registry key if the open was successful.
 *    NULL otherwise.
 *
 * Side effects:
 *    Opens the product registry key for writing. This must be closed
 *    by the calling program.
 *
 *----------------------------------------------------------------------
 */

HKEY
W32Util_OpenUserRegKey(REGSAM access) // Define access given to the opened key
{
   HKEY key;
   LONG result = Win32U_RegOpenKeyEx(HKEY_CURRENT_USER,
                                     ProductState_GetRegistryPath(),
                                     0, access, &key);

   return (result == ERROR_SUCCESS) ? key : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_ArgvToCmdLine --
 *
 *      Takes an array of strings to be passed as argv to a program
 *      and concatenates them into a Unicode with the proper quote and
 *      escape characters.  Passing this string to CreateProcess
 *      should yield the same argv back in the new process.
 *      See: http://msdn.microsoft.com/en-us/library/a1y7w461.aspx
 *
 * Results:
 *      The command line string.
 *
 * Side effects:
 *      The result is allocated.
 *
 *----------------------------------------------------------------------
 */

static Unicode
W32Util_ArgvToCmdLine(ConstUnicode *argv)  // IN: NUL-terminated arg.
{
   ConstUnicode arg;
   DynBuf buf;

   DynBuf_Init(&buf); 
   while ((arg = *argv++) != NULL) {
      int backslash = 0;
      int c;
      size_t len = strlen(arg);

      /*
       * We don't truncate any strings and UTF-8 doesn't allow
       * bit 7 to be clear in any multi-byte characters, so we
       * should be fine with the regular strXXX functions.
       */

      if (len && strcspn(arg, " \t\"") == len) {
         /*
          * Non-Null strings with no whitespace or quotes are fine as-is.
          */

         DynBuf_Append(&buf, arg, len);
         DynBuf_Append(&buf, " ", 1);
         continue;
      }

      /* Opening quote */
      DynBuf_Append(&buf, "\"", 1);

      while ((c = *arg++) != 0) {
         switch (c) {
         case '\\':
            backslash++;
            break;
         case '"':
            /*
             * Before a quote, each backslash needs two backslashes.
             */

            while (backslash) {
               DynBuf_Append(&buf, "\\\\", 2);
               backslash--;
            }
            DynBuf_Append(&buf, "\\\"", 2);
            break;
         default:
            /*
             * Before a non-quote, each backslash needs just one backslash
             */

            while (backslash) {
               DynBuf_Append(&buf, "\\", 1);
               backslash--;
            }
            DynBuf_Append(&buf, &c, 1);
         }
      }

      /*
       * Before the end of the string, each backslash needs two backslashes
       * so the terminating quote is not treated literally.
       */

      while (backslash) {
         DynBuf_Append(&buf, "\\\\", 2);
         backslash--;
      }
      /* Ending quote and space before next arg */
      DynBuf_Append(&buf, "\" ", 2);
      
   }

   DynBuf_Append(&buf, "\0", 1);

   return DynBuf_Detach(&buf); 
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_CreateProcessArgv --
 *
 *      Like W32U_CreateProcess except that it takes a NULL-terminated
 *      Unicode * argv instead of a Unicode command line.  This
 *      function takes care of all conversion from raw argv strings
 *      to an escaped command line for CreateProcess internally.
 *
 * Results:
 *      Same as CreateProcess
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_CreateProcessArgv(ConstUnicode lpApplicationName,             // IN
                            ConstUnicode *argv,                         // IN
                            LPSECURITY_ATTRIBUTES lpProcessAttributes,  // IN
                            LPSECURITY_ATTRIBUTES lpThreadAttributes,   // IN
                            BOOL bInheritHandles,                       // IN
                            DWORD dwCreationFlags,                      // IN
                            LPVOID lpEnvironment,                       // IN
                            ConstUnicode lpCurrentDirectory,            // IN
                            LPSTARTUPINFOA lpStartupInfo,               // IN
                            LPPROCESS_INFORMATION lpProcessInformation) // OUT
{
   Unicode cmdLine;
   Bool ret;

   cmdLine = W32Util_ArgvToCmdLine(argv);

   ret = Win32U_CreateProcess(lpApplicationName, cmdLine,
                              lpProcessAttributes, lpThreadAttributes,
                              bInheritHandles, dwCreationFlags, 
                              lpEnvironment, lpCurrentDirectory,
                              lpStartupInfo, lpProcessInformation);
   Unicode_Free(cmdLine);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_SplitCommandLine --
 *
 *      Break the command line into tokens.
 *
 * Results:
 *      Returns the number of tokens found and an array of tokens.
 *
 * Side effects:
 *      The commandLine string is changed.
 *
 * Bugs:
 *      Doesn't handle nested quotes, quoted backslashes, or MBCS strings.
 *      XXX Also doesn't do Unicode - will do this later.
 *
 *----------------------------------------------------------------------
 */

void
W32Util_SplitCommandLine(char *commandLine, // IN: the original command line
                         int maxArgs,       // IN: the maximum number of arguments
                         char *progName,    // IN: first argument
                         int *argc,         // OUT: the actual number of arguments
                         char **argv)       // OUT: the array of arguments
{
#ifndef __MINGW32__
   size_t i;
   char *token;
   char *tempToken;
   char space[] = " ";
   char quote[] = "\"";
   char *stopChar;
   char *saveptr;

   argv[0] = progName;
   *argc = 1;
   stopChar = commandLine + strlen(commandLine);
   for (token = strtok_s(commandLine, space, &saveptr);
        token != NULL && *argc < maxArgs;
        token = strtok_s(NULL, space, &saveptr)) {

      /*
       * Check to see if the current token is a quoted string with
       * embedded spaces.
       */

      if (token[0] == '"' &&
          token[strlen(token) - 1] != '"' &&
          (tempToken = strtok_s(NULL, quote, &saveptr)) != NULL) {

         /*
          * This token starts with a double-quote. Remove
          * the double-quote from the front.
          */

         token++;

         /*
          * Now replace the NULL characters inserted by strtok_s
          * with the original spaces.
          */

         for (i = strlen(token);
              token + i < stopChar && token[i] == '\0';
              i++) {

            token[i] = ' ';
         }
      }

      /*
       * Strip off the first and last quote if this is a quoted string.
       */

      if (token[0] == '"' && token[strlen(token) - 1] == '"') {
         token[strlen(token) - 1] = '\0';
         token++;
      }
      argv[(*argc)++] = token;
   }
   argv[(*argc)] = NULL;
#else
   ASSERT(0);  // ***TODO-KwH: do we need to port this?
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * W32UtilReadWriteFileTimeout --
 *
 *      Reads from or writes to a file (since the timeout-handling code is
 *      the same except for the call to the underlying API function, this is
 *      used as a helper function for both W32UtilReadFileTimeout and
 *      W32UtilWriteFileTimeout).  Will fail if the operation couldn't be
 *      completed in a certain amount of time.
 *
 * Results:
 *      Same as the WIN32 ReadFile/WriteFile APIs.
 *
 * Side effects:
 *      Fills lpBuffer from the file, or fills the file from lpBuffer.
 *
 *----------------------------------------------------------------------
 */

static BOOL
W32UtilReadWriteFileTimeout(HANDLE hFile,      // handle of file to read/write
                            LPVOID lpBuffer,   // pointer to data buffer
                            DWORD cbBuffer,    // number of bytes to read
                            LPDWORD pcbResult, // pointer to num bytes read
                            DWORD msTimeout,   // timeout in milliseconds
                            BOOL bWrite)       // write or read?
{
   BOOL fSuccess;
   DWORD waitResult;
   OVERLAPPED ol = { 0 };

   /*
    * Create an event only we own, to tell us when overlapped IO completes
    */

   if ((ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
      return FALSE;
   }

   /*
    * Read or write the data; it's all the same to us
    */

   if (bWrite) {
      fSuccess = WriteFile(hFile, lpBuffer, cbBuffer, pcbResult, &ol);
   } else {
      fSuccess = ReadFile(hFile, lpBuffer, cbBuffer, pcbResult, &ol);
   }

   /*
    * If the data can't be read/written immediately and the API plans to
    * work on it asynchronously, it fails with ERROR_IO_PENDING, which we
    * expect and handle.  Any other failure is bad.
    */

   if (fSuccess == FALSE && GetLastError() == ERROR_IO_PENDING) {
      waitResult = WaitForSingleObject(ol.hEvent, msTimeout);
      switch (waitResult) {
      case WAIT_OBJECT_0:
         fSuccess = GetOverlappedResult(hFile, &ol, pcbResult, FALSE);
         break;
      case WAIT_FAILED: // last error already set
      case WAIT_ABANDONED: // can't really happen since we're the only thread
         break;
      case WAIT_TIMEOUT:
         // cancel the IO since the caller-owned lpBuffer might disappear
         CancelIo(hFile);
         // try one last time in case data was read/written in the meantime
         fSuccess = GetOverlappedResult(hFile, &ol, pcbResult, FALSE);
         if (!fSuccess) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) {
               // we'd rather this show as a timeout than aborted
               SetLastError(STATUS_TIMEOUT);
            }
         }
         break;
      }
   }

   CloseHandle(ol.hEvent);

   return fSuccess;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_ReadFileTimeout --
 *
 *      Reads from a file.  Will fail if the read couldn't be
 *      completed in a certain amount of time.
 *
 * Results:
 *      Same as the WIN32 ReadFile API.
 *
 * Side effects:
 *      Fills lpBuffer from the file.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_ReadFileTimeout(HANDLE hFile,                // handle of file to read
                        LPVOID lpBuffer,             // pointer to buffer that receives data
                        DWORD nNumberOfBytesToRead,  // number of bytes to read
                        LPDWORD lpNumberOfBytesRead, // pointer to number of bytes read
                        DWORD msTimeout)             // timeout in milliseconds
{
   return W32UtilReadWriteFileTimeout(hFile, lpBuffer, nNumberOfBytesToRead,
                                      lpNumberOfBytesRead, msTimeout, FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_WriteFileTimeout --
 *
 *      Writes to a file.  Will fail if the write couldn't be
 *      completed in a certain amount of time.
 *
 * Results:
 *      Same as the WIN32 WriteFile API.
 *
 * Side effects:
 *      Write's from lpBuffer to the file
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_WriteFileTimeout(HANDLE hFile,                    // IN
                         LPCVOID lpBuffer,                // IN
                         DWORD nNumberOfBytesToWrite,     // IN
                         LPDWORD lpNumberOfBytesWritten,  // IN
                         DWORD msTimeout)                 // IN
{
   return W32UtilReadWriteFileTimeout(hFile, (LPVOID)lpBuffer,
                                      nNumberOfBytesToWrite,
                                      lpNumberOfBytesWritten,
                                      msTimeout, TRUE);
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_RealPath --
 *
 *      Takes the path to a file in the 'path' variable, resolves
 *      from the current working directory, expands environment variables
 *      etc, to produce a full path.  Can be used to replace UNIX calls
 *      to realpath.
 *
 * Results:
 *      NULL, or a Unicode string (free with Unicode_Free).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Unicode
W32Util_RealPath(ConstUnicode path) // IN
{
   DWORD size, ret;
   Bool succeeded = FALSE;
   Unicode fullPath = NULL;
   Unicode expPath = NULL;
   const utf16_t *fullPathW = NULL;
   wchar_t *expPathW = NULL;

   ASSERT(path);

   /*
    * Create the input path.  If this isn't a UNC or
    * fully qualified path, we need to stick the
    * current working directory on the front.  Also,
    * assume that if the first character is a % (i.e.
    * and environment variable expansion), that it's
    * not a relative path (is this valid?).
    */

   if (!Unicode_StartsWith(path, DIRSEPS DIRSEPS) &&
       !Unicode_StartsWith(path, "%") &&
       (1 != Unicode_Find(path, ":"))) {
      Unicode curDir = Win32U_GetCurrentDirectory();

      if (!curDir) {
         goto exit;
      }

      fullPath = Unicode_Join(curDir, DIRSEPS, path, NULL);
      Unicode_Free(curDir);
   } else {
      fullPath = Unicode_Duplicate(path);
   }

   /*
    * Expand the path with ExpandEnvironmentStrings.
    */

   fullPathW = UNICODE_GET_UTF16(fullPath);
   size = MAX_PATH;

   while (TRUE) {
      expPathW = Util_SafeMalloc(size * sizeof(wchar_t));
      ret = ExpandEnvironmentStringsW(fullPathW, expPathW, size);

      if (0 == size) {
         Log("%s: ExpandEnvironmentStringsW failed: %d\n", __FUNCTION__,
             GetLastError());
         goto exit;
      } else if (ret < size) {
         break;
      } else {
         size = ret;
         free(expPathW);
      }
   }

   expPath = Unicode_AllocWithUTF16(expPathW);

   /*
    * Make sure the constructed path actually exists.
    */

   if (!File_Exists(expPath)) {
      goto exit;
   }

   succeeded = TRUE;

  exit:
   Unicode_Free(fullPath);
   UNICODE_RELEASE_UTF16(fullPathW);
   free(expPathW);

   if (!succeeded) {
      Unicode_Free(expPath);
      expPath = NULL;
   }

   return expPath;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_CheckForPrivilegeHeld --
 *
 *      Checks to see if the specified token
 *      has the privilege.
 *
 * Results:
 *      TRUE if the privilege is held, FALSE otherwise
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_CheckForPrivilegeHeld(HANDLE token, // IN
                              LPCTSTR priv) // IN
{
   TOKEN_PRIVILEGES *p = NULL;
   DWORD psize = 0;
   DWORD i;
   LUID priv_id;
   BOOL retval = FALSE;
   BOOL fProcessToken = FALSE;

   /*
    * Use the current process token if no handle is specified.
    */

   if (token == NULL) {
      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
         Warning("Error %d calling OpenProcessToken.", GetLastError());
         goto end;
      }
      fProcessToken = TRUE;
   }

   /*
    * Look up all the privileges held by the token.
    */

   GetTokenInformation(token, TokenPrivileges, p, 0, &psize);
   ASSERT(psize > 0);

   p = (TOKEN_PRIVILEGES *) LocalAlloc(0, psize);
   if (p == NULL) {
      Warning("Out of memory trying to allocate %d bytes in "
              "W32Util_CheckForPrivilegeHeld.\n", psize);
      ASSERT(FALSE);
      goto end;
   }

   if (!GetTokenInformation(token, TokenPrivileges, p, psize, &psize)) {
      Warning("Error %d during GetTokenInformation(\"%0x%x\").",
              GetLastError(), token);
      goto end;
   }
   if (!LookupPrivilegeValue(NULL, priv, &priv_id)) {
      Warning("Error %d during LookupPrivilegeValue(\"%s\").",
              GetLastError(), priv);
      goto end;
   }

   /*
    * Search for the one we're interested in.
    */

   for (i = 0; i < p->PrivilegeCount; i++) {
      if (p->Privileges[i].Luid.HighPart == priv_id.HighPart &&
          p->Privileges[i].Luid.LowPart == priv_id.LowPart) {
         retval = TRUE;
         goto end;
      }
   }

   /*
    * Intentional Fallthrough
    */

end:
   if (fProcessToken) {
      CloseHandle(token);
   }
   if (p != NULL) {
      LocalFree(p);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_WideStrToMultiByteStr --
 *
 *      Converts a unicode string to a Multi Byte string.
 *
 * Results:
 *      An allocated buffer to the new string. Caller is responsible
 *      for freeing the buffer. NULL on error.
 *
 * Side effects:
 *      Allocates buffer.
 *
 *----------------------------------------------------------------------
 */

LPSTR
W32Util_WideStrToMultiByteStr(LPCWSTR wideStr,  // IN:
                              UINT codePage)    // IN:
{
   int nBytes;
   LPSTR multiStr;

   // Get the length of the converted string.
   nBytes = WideCharToMultiByte(codePage, 0, wideStr, -1, NULL, 0, NULL, NULL);

   if (nBytes == 0) {
      return NULL;
   }

   // Allocate space to hold the converted string.
   multiStr = Util_SafeMalloc(nBytes);

   // Convert the string.
   nBytes = WideCharToMultiByte(codePage, 0, wideStr, -1, multiStr, nBytes,
                                NULL, NULL);

   if (nBytes == 0) {
      free(multiStr);

      return NULL;
   }

   return multiStr;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_WideStrToAsciiStr --
 *
 *      Converts a unicode string to an ASCII (UTF-8) string.
 *
 * Results:
 *      An allocated buffer to the new string. Caller is responsible
 *      for freeing the buffer. NULL on error.
 *
 * Side effects:
 *      Allocates buffer.
 *
 *----------------------------------------------------------------------
 */

LPSTR
W32Util_WideStrToAsciiStr(LPCWSTR wideStr) // IN
{
   return W32Util_WideStrToMultiByteStr(wideStr, CP_UTF8);
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_MultiByteStrToWideStr --
 *
 *      Converts multibyte string to an unicode string.
 *
 * Results:
 *      An allocated buffer to the new string. Caller is responsible
 *      for freeing the buffer. NULL on error.
 *
 * Side effects:
 *      Allocates buffer.
 *
 *----------------------------------------------------------------------
 */

LPWSTR
W32Util_MultiByteStrToWideStr(LPCSTR multiStr,
                              UINT codePage) // IN
{
    ULONG nBytes, nChars;
    LPWSTR wideStr;

    // Get the length of the converted string.
    nChars = MultiByteToWideChar(codePage, 0, multiStr, -1, NULL, 0);

    if (nChars == 0) {
       return NULL;
    }

    // Allocate space to hold the converted string.
    nBytes = sizeof *wideStr * nChars;
    wideStr = Util_SafeMalloc(nBytes);

    // Convert the string.
    nChars = MultiByteToWideChar(codePage, 0, multiStr, -1, wideStr, nChars);

    if (nChars == 0) {
       free(wideStr);

       return NULL;
    }

    return wideStr;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_AsciiStrToWideStr --
 *
 *      Converts ASCII (UTF-8) to unicode.
 *
 * Results:
 *      An allocated buffer to the new string. Caller is responsible
 *      for freeing the buffer. NULL on error.
 *
 * Side effects:
 *      Allocates buffer.
 *
 *----------------------------------------------------------------------
 */

LPWSTR
W32Util_AsciiStrToWideStr(LPCSTR multiStr) // IN
{
    return W32Util_MultiByteStrToWideStr(multiStr, CP_UTF8);
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_WinSockAddReference --
 *
 *      Initialize the WinSock library. The library contains
 *      an internal count of how many times it is initialized and
 *      shutdown.
 *
 * Results:
 *      TRUE on success.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_WinSockAddReference(void)
{
   WORD versionRequested;
   WSADATA wsaData;

   versionRequested = MAKEWORD(2, 0);

   return WSAStartup(versionRequested, &wsaData) == 0;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_WinSockDereference --
 *
 *      Decrement the winsock library's internal reference count,
 *      potentially causing the library to shut down.
 *
 * Results:
 *      TRUE on success.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_WinSockDereference(void)
{
   return WSACleanup() == 0;
}


/*
 *----------------------------------------------------------------------
 *
 * W32UtilSetServiceDescription --
 *
 *    Sets the description name of the service.
 *
 * Results:
 *    Returns TRUE on success.  FALSE on failure.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static Bool
W32UtilSetServiceDescription(SC_HANDLE hService,        // IN
                             ConstUnicode description)  // IN
{
   SERVICE_DESCRIPTIONW desc;
   Bool retval = FALSE;
   const utf16_t *descriptionW = NULL;
   static HMODULE hAdvapi = NULL;
   static ChangeServiceConfig2WFnType ChangeServiceConfig2WFn = NULL;

   ASSERT(hService);
   ASSERT(description);

   if (!hAdvapi) {
      hAdvapi = LoadLibraryW(L"advapi32.dll");
   }

   if (hAdvapi && !ChangeServiceConfig2WFn) {
      ChangeServiceConfig2WFn = (ChangeServiceConfig2WFnType)
                           GetProcAddress(hAdvapi, "ChangeServiceConfig2W");
   }

   if (!ChangeServiceConfig2WFn) {
      goto exit;
   }

   descriptionW = UNICODE_GET_UTF16(description);
   desc.lpDescription = (LPWSTR) descriptionW;

   retval = ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &desc);

  exit:
   UNICODE_RELEASE_UTF16(descriptionW);

   return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_RegisterService --
 *
 *   Register or unregister a service with the Service Control Manager
 *
 * Returns
 *   TRUE on success, FALSE on failure. Error messages are logged to the
 *   log file.
 *
 * Side Effects
 *   None
 *----------------------------------------------------------------------
 */

Bool
W32Util_RegisterService(Bool bRegister,           // IN: Reg/Unreg flag
                        ConstUnicode name,        // IN: Service name
                        ConstUnicode displayName, // IN: Display name
                        ConstUnicode description, // IN: Service description
                        ConstUnicode binaryPath,  // IN: Path to service exe
                        Unicode *errString)       // OUT: error string
{
   SC_HANDLE schSCManager = NULL;
   SC_HANDLE schService   = NULL;
   Bool retval = FALSE;
   const utf16_t *nameW;
   const utf16_t *displayNameW;
   const utf16_t *binaryPathW;

   ASSERT(name);
   ASSERT(displayName);
   ASSERT(description);
   ASSERT(binaryPath);

   if (errString) {
      *errString = NULL;
   }

   nameW = UNICODE_GET_UTF16(name);
   displayNameW = UNICODE_GET_UTF16(displayName);
   binaryPathW = UNICODE_GET_UTF16(binaryPath);

   schSCManager = Win32U_OpenSCManager(NULL,                    // local machine
                                       NULL,                    // ServicesActive db
                                       SC_MANAGER_ALL_ACCESS);  // full access rights

   if (schSCManager == NULL) {
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Could not open SCManager: %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto exit;
   }

   if (bRegister) {
      schService = CreateServiceW(schSCManager,              // SCManager db
                                  nameW,                     // service name
                                  displayNameW,              // display name
                                  SERVICE_ALL_ACCESS,        // desired access
                                  SERVICE_WIN32_OWN_PROCESS, // service type
                                  SERVICE_AUTO_START,        // start type
                                  SERVICE_ERROR_NORMAL,      // err ctrl type
                                  binaryPathW,               // service binary
                                  NULL,                      // no load group
                                  NULL,                      // no tag id
                                  NULL,                      // no deps
                                  NULL,                      // LocalSystem
                                  NULL);                     // no password

      if (schService == NULL) {
         switch (GetLastError()) {
         case ERROR_SERVICE_MARKED_FOR_DELETE:
            if (errString) {
               *errString = Unicode_Duplicate(
                  "Could not register service because it is currently "
                  "marked for deletion.");
            }
            goto exit;
         case ERROR_SERVICE_EXISTS:
            if (errString) {
               *errString =
                  Unicode_Duplicate("Service is already registered.");
            }
            goto exit;
         default:
            if (errString) {
               char *temp = Str_Asprintf(NULL,
                                         "Could not create service: %s\n",
                                         Msg_ErrString());

               *errString = Unicode_AllocWithUTF8(temp);
               free(temp);
            }
            goto exit;
         }
      }

      W32UtilSetServiceDescription(schService, description);
   } else {
      schService = OpenServiceW(schSCManager,      // SCManager database
                                nameW,             // name of service
                                DELETE);           // only need DELETE access

      if (schService == NULL) {
         if (errString) {
            if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
               *errString = Unicode_Duplicate("Service is not registered.");
            } else {
               char *temp = Str_Asprintf(NULL, "Could not open service: %s\n",
                                         Msg_ErrString());

               *errString = Unicode_AllocWithUTF8(temp);
               free(temp);
            }
         }
         goto exit;
      }

      if (!DeleteService(schService)) {
         if (errString) {
            char *temp = Str_Asprintf(NULL, "Could not delete service: %s\n",
                                      Msg_ErrString());

            *errString = Unicode_AllocWithUTF8(temp);
            free(temp);
         }
         goto exit;
      }
   }

   retval = TRUE;

  exit:
   UNICODE_RELEASE_UTF16(nameW);
   UNICODE_RELEASE_UTF16(displayNameW);
   UNICODE_RELEASE_UTF16(binaryPathW);

   if (schService) {
      CloseServiceHandle(schService);
   }

   if (schSCManager) {
      CloseServiceHandle(schSCManager);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_DebugService --
 *
 *   Check if the service needs to be debugged. Looks for the existence
 *   of the specified file in system root directory.
 *
 * Returns
 *   TRUE if file exists. FALSE otherwise.
 *
 * Side Effects
 *   None
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_DebugService(ConstUnicode dbgFile)
{
   Bool succeeded = FALSE;
   wchar_t *systemDirW = NULL;
   Unicode fullPath = NULL;
   Unicode fullPath2 = NULL;
   DWORD size = MAX_PATH;
   BOOL ret;

   if (!dbgFile) {
      goto exit;
   }

   while (TRUE) {
      systemDirW = Util_SafeMalloc(size * sizeof(wchar_t));
      ret = GetSystemDirectoryW(systemDirW, size);

      if (ret < size) {
         break;
      } else {
         size = ret;
         free(systemDirW);
      }
   }

   if ((0 != ret) &&
       (systemDirW[1] == L':') &&
       (systemDirW[2] == L'\\') &&
       (systemDirW[3] != L'\0')) {
      systemDirW[3] = L'\0';
   } else {
      free(systemDirW);
      systemDirW = Str_Aswprintf(NULL, L"C:\\");
   }

   fullPath = Unicode_AllocWithUTF16(systemDirW);
   fullPath2 = Unicode_Append(fullPath, dbgFile);

   if (!File_Exists(fullPath2)) {
      goto exit;
   }

   succeeded = TRUE;

  exit:
   free(systemDirW);
   Unicode_Free(fullPath);
   Unicode_Free(fullPath2);

   return succeeded;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_RegisterEventLog --
 *
 *      Registers the service or program as a possible source for the
 *      Event Log.  The serviceName and typesSupported parameters are
 *      required while the others are optional.  This function will
 *      overwrite any previous settings for service.
 *
 * Results:
 *      TRUE on success.
 *
 * Side effects:
 *      Modifies the registry.  Call W32Util_UnregisterEventLog() on
 *      uninstall.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_RegisterEventLog(ConstUnicode serviceName,     // IN: event logger name
                         DWORD typesSupported,         // IN: mask of events
                         ConstUnicode eventMsgFile,    // IN: msg file
                         ConstUnicode categoryMsgFile, // IN: cat msg file
                         DWORD categoryCount,          // IN: num of categories
                         ConstUnicode paramMsgFile)    // IN: param msg file
{
   HKEY hk = NULL;
   DWORD dwData;
   Bool retval = FALSE;
   Unicode keyPath;
   const utf16_t *keyPathW = NULL;
   const utf16_t *eventMsgFileW = NULL;
   const utf16_t *categoryMsgFileW = NULL;
   const utf16_t *paramMsgFileW = NULL;

   ASSERT(serviceName);

   keyPath = Unicode_Append(
      "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\",
      serviceName);
   keyPathW = UNICODE_GET_UTF16(keyPath);

   if (eventMsgFile) {
      eventMsgFileW = UNICODE_GET_UTF16(eventMsgFile);
   }
   if (categoryMsgFile) {
      categoryMsgFileW = UNICODE_GET_UTF16(categoryMsgFile);
   }
   if (paramMsgFile) {
      paramMsgFileW = UNICODE_GET_UTF16(paramMsgFile);
   }

   /*
    * Register ourselves as an event source to hook into the
    * Windows NT Event Viewer application log.
    */

   if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPathW, 0,
                       NULL, REG_OPTION_NON_VOLATILE,
                       KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS) {
      Warning("%s: Could not open registry key. Error %d.\n",
              __FUNCTION__, GetLastError());
      goto exit;
   }

   /*
    * Set the supported event types in the TypesSupported subkey.
    */

   dwData = typesSupported;

   if (RegSetValueExW(hk, L"TypesSupported", 0, REG_DWORD,
                      (LPBYTE) &dwData, sizeof(DWORD))) {
      Warning("%s: Could not set TypesSupported key. Error %d.\n",
              __FUNCTION__, GetLastError());
      goto exit;
   }

  /*
   * Set the names of the event, category, and parameter message files if they
   * are defined.
   */

   if (eventMsgFile) {
      if (RegSetValueExW(hk, L"EventMessageFile", 0, REG_EXPAND_SZ,
                         (LPBYTE) eventMsgFileW,
                         (wcslen(eventMsgFileW) + 1) * sizeof(wchar_t))) {
         Warning("%s: Could not set EventMessageFile key. Error %d.\n",
                 __FUNCTION__, GetLastError());
         goto exit;
      }
   }

   if (categoryMsgFile) {
      if (RegSetValueExW(hk, L"CategoryMessageFile", 0, REG_EXPAND_SZ,
                         (LPBYTE) categoryMsgFileW,
                         (wcslen(categoryMsgFileW) + 1) * sizeof(wchar_t))) {
         Warning("%s: Could not set CategoryMessageFile key. Error %d.\n",
                 __FUNCTION__, GetLastError());
         goto exit;
      }
   }

   if (paramMsgFile) {
      if (RegSetValueExW(hk, L"ParameterMessageFile", 0, REG_EXPAND_SZ,
                         (LPBYTE) paramMsgFileW,
                         (wcslen(paramMsgFileW) + 1) * sizeof(wchar_t))) {
         Warning("%s: Could not set ParameterMessageFile key. Error %d.\n",
                 __FUNCTION__, GetLastError());
         goto exit;
      }
   }

   /*
    * Set the category count if it is non-zero.
    */

   if (categoryCount > 0) {
      dwData = categoryCount;

      if (RegSetValueExW(hk, L"CategoryCount", 0, REG_DWORD,
                         (LPBYTE) &dwData, sizeof(DWORD))) {
         Warning("%s: Could not set CategoryCount key. Error %d.\n",
                 __FUNCTION__, GetLastError());
         goto exit;
      }
   }

   /* Success if we got here */
   retval = TRUE;

  exit:
   UNICODE_RELEASE_UTF16(keyPathW);
   UNICODE_RELEASE_UTF16(eventMsgFileW);
   UNICODE_RELEASE_UTF16(categoryMsgFileW);
   UNICODE_RELEASE_UTF16(paramMsgFileW);

   if (hk) {
      RegCloseKey(hk);
   }

   if (!retval) {
      /* On failure, undo all registry changes. */
      W32Util_UnregisterEventLog(serviceName);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_UnregisterEventLog --
 *
 *      Unregisters the service or program as a possible source for the
 *      Event Log.
 *
 * Results:
 *      TRUE on success.
 *
 * Side effects:
 *      Modifies the registry.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_UnregisterEventLog(ConstUnicode serviceName)  // IN: name to be used
{
   Unicode keyPath;
   const utf16_t *keyPathW;
   Bool res = FALSE;

   ASSERT(serviceName);

   keyPath = Unicode_Append(
      "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\",
      serviceName);
   keyPathW = UNICODE_GET_UTF16(keyPath);

   if (!RegDeleteKeyW(HKEY_LOCAL_MACHINE, keyPathW) != ERROR_SUCCESS) {
      Warning("%s: Could not delete registry key. Error %d.\n",
              __FUNCTION__, GetLastError());
      goto exit;
   }

   res = TRUE;

  exit:
   UNICODE_RELEASE_UTF16(keyPathW);

   return res;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_SetSDWithVmGroupPriv --
 *
 *      Wrapper for W32Util_SetSDWithVmGroupPriv that doesnt add the
 *      current user ACE to the ACL.
 *
 * Results:
 *      TRUE on success. User must free *pAcl with free() when done.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_SetSDWithVmGroupPriv(PSECURITY_DESCRIPTOR pSecurityDescriptor,  // IN
                             DWORD accessType,        // IN
                             PACL *pAcl,              // OUT
                             Unicode *errString)      // OUT
{
   return W32Util_SetSDWithVmGroupPrivEx(pSecurityDescriptor, accessType,
                                         FALSE, pAcl, errString);
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_SetSDWithVmGroupPrivEx --
 *
 *     Sets the given SD with access privileges to VMWARE_GROUP and
 *     administrative accounts. Also adds current user if requested.
 *
 * Results:
 *      TRUE on success. User must free *pAcl with free() when done.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_SetSDWithVmGroupPrivEx(PSECURITY_DESCRIPTOR pSecurityDescriptor,  // IN
                               DWORD accessType,        // IN
                               Bool addCurrentUser,     // IN
                               PACL *pAcl,              // OUT
                               Unicode *errString)      // OUT
{
   BYTE sidVMwareGroup[128];
   DWORD cbSid = sizeof sidVMwareGroup;
   WCHAR szDomainNameBuf[FILE_MAXPATH];
   DWORD cbDomainName = ARRAYSIZE(szDomainNameBuf);
   SID_NAME_USE sidUsage;
   PSID psidAdministrators = NULL;
   BYTE *aclVMwareGroupBuffer;
   PACL aclVMwareGroup = NULL;
   DWORD aclSize;
   Bool fSuccess = FALSE;
   PSID *sidCurrentUser = NULL;
   int count = 0;
   TOKEN_USER *tokenUser = NULL;

   if (errString) {
      *errString = NULL;
   }

   // Get SID for current user if requested
   if (addCurrentUser) {
      HANDLE token;
      int tokenUserSize;

      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
         if (errString) {
            char *temp = Str_Asprintf(NULL, "Can't open process: %s\n",
                                      Msg_ErrString());

            *errString = Unicode_AllocWithUTF8(temp);
            free(temp);
         }
         goto err;
      }

      GetTokenInformation(token, TokenUser, tokenUser, 0, &tokenUserSize);
      ASSERT(tokenUserSize > 0);

      tokenUser = (TOKEN_USER *) Util_SafeCalloc(1, tokenUserSize);
      if (!GetTokenInformation(token, TokenUser, tokenUser, tokenUserSize,
                               &tokenUserSize)) {
         if (errString) {
            char *temp = Str_Asprintf(NULL, "Can't get token info: %s\n",
                                      Msg_ErrString());

            *errString = Unicode_AllocWithUTF8(temp);
            free(temp);
         }
         CloseHandle(token);
         goto err;
      }

      sidCurrentUser = tokenUser->User.Sid;
      CloseHandle(token);
   }

   // Get SID for __vmware__ group
   if (!LookupAccountNameW (NULL,            // [in] system name: local system
                            VMWARE_GROUP,    // [in] account name: __vmware__
                            &sidVMwareGroup, // [out] ptr to sec identifier
                            &cbSid,          // [in,out] size of SID
                            szDomainNameBuf, // [out] domain name where found
                            &cbDomainName,   // [in,out] size domain name buf
                            &sidUsage)) {    // [out] SID-type indicator
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Can't look up %S group: %s.\n",
                                   VMWARE_GROUP_DESC, Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   if (sidUsage != SidTypeAlias) {
      // what the admin UI calls a "local group" is really an Alias
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Bad account type for %S group.\n",
                                   VMWARE_GROUP_DESC);

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   // get SID for Administrators group
   if (!W32Util_GetLocalAdminGroupSid(&psidAdministrators)) {
      if (errString) {
         char *temp = Str_Asprintf(NULL,
                                   "Can't look up Administrators group: %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   count = (addCurrentUser ? 3 : 2);
   aclSize = sizeof(ACL) + (sizeof(ACCESS_ALLOWED_ACE) * count) -
             (sizeof(DWORD) * count) + GetLengthSid(sidVMwareGroup) +
              GetLengthSid(psidAdministrators) +
              (addCurrentUser ? GetLengthSid(sidCurrentUser) : 0);

   aclVMwareGroupBuffer = (BYTE*) Util_SafeMalloc(aclSize);
   aclVMwareGroup = (PACL)aclVMwareGroupBuffer;

   // create DACL that allows only __vmware__ group or Admins
   if (!InitializeAcl(aclVMwareGroup, aclSize, ACL_REVISION)) {
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Can't initialize ACL: %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   if (!AddAccessAllowedAce(aclVMwareGroup, ACL_REVISION, accessType,
                            sidVMwareGroup)) {
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Can't add ACE (1): %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   if (!AddAccessAllowedAce(aclVMwareGroup, ACL_REVISION, accessType,
                            psidAdministrators)) {
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Can't add ACE (2): %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   if (addCurrentUser) {
      if (!AddAccessAllowedAce(aclVMwareGroup, ACL_REVISION, accessType,
                               sidCurrentUser)) {
         if (errString) {
            char *temp = Str_Asprintf(NULL,
                                      "Can't add ACE for current user: %s\n",
                                      Msg_ErrString());

            *errString = Unicode_AllocWithUTF8(temp);
            free(temp);
         }
         goto err;
      }
   }

   // create SD using that DACL and owned by __vmware__ group
   if (!InitializeSecurityDescriptor(pSecurityDescriptor,
                                     SECURITY_DESCRIPTOR_REVISION)) {
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Can't init sec dec: %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   if (!SetSecurityDescriptorDacl(pSecurityDescriptor, TRUE, aclVMwareGroup,
                                  FALSE)) {
      if (errString) {
         char *temp = Str_Asprintf(NULL, "Can't set DACL: %s\n",
                                   Msg_ErrString());

         *errString = Unicode_AllocWithUTF8(temp);
         free(temp);
      }
      goto err;
   }

   ASSERT(IsValidAcl(aclVMwareGroup));
   fSuccess = TRUE;
   goto exit;

err:
   free(aclVMwareGroup);
   aclVMwareGroup = NULL;

exit:
   free(tokenUser);
   *pAcl = aclVMwareGroup;
   free(psidAdministrators);

   return fSuccess;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_SetSecurityDescriptorW --
 *
 *      Set the DACL of a security descriptor to only allow access by a
 *      particular account.
 *
 * Results:
 *      TRUE on success. User must free *pAcl with free() when done.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_SetSecurityDescriptorW(PSECURITY_DESCRIPTOR pSecurityDescriptor, //IN/OUT
                               ConstUnicode owner,                       //IN
                               PACL *pAcl)                               //OUT
{
   BYTE sidBuffer[1024];
   PSID psidOwner = (PSID) sidBuffer;
   DWORD cbSid = sizeof sidBuffer;
   WCHAR RefDomain[FILE_MAXPATH];
   DWORD cbRefDomain = ARRAYSIZE(RefDomain);
   SID_NAME_USE snu;
   const utf16_t *ownerW;
   Bool res = FALSE;

   *pAcl = NULL;

   if (owner == NULL) {
      res = W32Util_SetSecurityDescriptorSid(pSecurityDescriptor, NULL, pAcl);
      goto exit;
   }

   /*
    * Get the SID for the account/group
    */

   ownerW = UNICODE_GET_UTF16(owner);

   if (!LookupAccountNameW(NULL, ownerW, psidOwner, &cbSid,
                           RefDomain, &cbRefDomain, &snu)) {
      goto exit;
   }

   res = W32Util_SetSecurityDescriptorSid(pSecurityDescriptor, psidOwner,
                                          pAcl);

  exit:
   UNICODE_RELEASE_UTF16(ownerW);

   return res;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_SetSecurityDescriptorSid --
 *
 *      Set the DACL of a security descriptor to only allow all access
 *      from a particular SID.
 *
 * Results:
 *      TRUE on success. User must free *pAcl with free() when done.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_SetSecurityDescriptorSid(PSECURITY_DESCRIPTOR sd,     //IN/OUT
                                 PSID sid,                    //IN: SID to add
                                 PACL *pAcl)                  //OUT
{
   DWORD aclsize;

   *pAcl = NULL;

   if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION)) {
      return FALSE;
   }

   if (sid == NULL) {
      /* No security required. Allow anybody to access. */
      return SetSecurityDescriptorDacl(sd, TRUE, NULL, FALSE);
   }

   /* Create the ACL, initialize it, and add the SID to the list. */
   aclsize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) +
             GetLengthSid(sid) - sizeof(DWORD);

   *pAcl = (PACL) Util_SafeCalloc(1, aclsize);

   if (!InitializeAcl(*pAcl, aclsize, ACL_REVISION)) {
      free(*pAcl);
      *pAcl = NULL;

      return FALSE;
   }

   if (!AddAccessAllowedAce(*pAcl, ACL_REVISION, GENERIC_ALL, sid)) {
      free(*pAcl);
      *pAcl = NULL;

      return FALSE;
   }

   /* Add the created ACL to the discretionary control list */
   if (!SetSecurityDescriptorDacl(sd, TRUE, *pAcl, FALSE)) {
      free(*pAcl);
      *pAcl = NULL;

      return FALSE;
   }

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetSecurityDescriptor --
 *
 *      Gets the Security Descriptor of a file system path.
 *
 * Results:
 *      TRUE on success. FALSE on failure.
 *
 * Side effects:
 *      On success, a SECURITY_DESCRIPTOR is allocated, which the caller
 *      should free.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_GetSecurityDescriptor(ConstUnicode path,               // IN
                              PSECURITY_DESCRIPTOR *ppSecOut)  // OUT
{
   PSECURITY_DESCRIPTOR pSec = NULL;
   DWORD secSize;
   DWORD secSizeNeeded;
   const utf16_t *pathW;
   Bool ret = FALSE;

   pathW = UNICODE_GET_UTF16(path);

   secSize = 0;
   for (;;) {
      if (GetFileSecurityW(pathW, OWNER_SECURITY_INFORMATION |
                                  GROUP_SECURITY_INFORMATION |
                                  DACL_SECURITY_INFORMATION,
                           pSec, secSize, &secSizeNeeded)) {
         break;
      }

      if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
         Warning("%s: Unable to get the security descriptor for '%s' (%d): "
                 "%s\n", __FUNCTION__, UTF8(path), GetLastError(),
                 Msg_ErrString());
         goto exit;
      }

      pSec = (PSECURITY_DESCRIPTOR) Util_SafeRealloc(pSec, secSizeNeeded);
      secSize = secSizeNeeded;
   }

   *ppSecOut = pSec;
   ret = TRUE;

exit:
   UNICODE_RELEASE_UTF16(pathW);
   if (!ret) {
      free(pSec);
   }

   return ret;
}


/* ============================ XXX Reorg? ============================== */

#define err(x) Warning("%s: %s\n", x, Err_ErrString())

char *accessRights[32] = {
      /*0*/      "FILE_LIST_DIRECTORY,FILE_READ_DATA"
    , /*1*/      "FILE_ADD_FILE,FILE_WRITE_DATA"
    , /*2*/      "FILE_ADD_SUBDIRECTORY,FILE_APPEND_DATA"
    , /*3*/      "FILE_READ_EA"
    , /*4*/      "FILE_WRITE_EA"
    , /*5*/      "FILE_TRAVERSE,FILE_EXECUTE"
    , /*6*/      "FILE_DELETE_CHILD"
    , /*7*/      "FILE_READ_ATTRIBUTES"
    , /*8*/      "FILE_WRITE_ATTRIBUTES"

    , /*9 */     "???"
    , /*10*/     "???"
    , /*11*/     "???"
    , /*12*/     "???"
    , /*13*/     "???"
    , /*14*/     "???"
    , /*15*/     "???"

    //fr winnt.h
    , /*16*/     "DELETE"
    , /*17*/     "READ_CONTROL"
    , /*18*/     "WRITE_DAC"
    , /*19*/     "WRITE_OWNER"
    , /*20*/     "SYNCHRONIZE"

    , /*21*/     "???"
    , /*22*/     "???"
    , /*23*/     "???"
    , /*24*/     "???"
    , /*25*/     "???"
    , /*26*/     "???"
    , /*27*/     "???"

    //fr winnt.h (don't seem to be used)
    , /*28*/     "???GENERIC_ALL"
    , /*29*/     "???GENERIC_EXECUTE"
    , /*30*/     "???GENERIC_WRITE"
    , /*31*/     "???GENERIC_READ"
};


/*
 *----------------------------------------------------------------------
 *
 * SetDacl --
 *
 *      Returns a newly constructed DACL based on a existing DACL (if
 *      specified), and the new access rights given.
 *
 *      XXX Need to be more careful about the position at which new ACE is
 *          inserted.
 *
 * Results:
 *      On failure, NULL is returned.
 *      On success, a pointer to newly constructed ACL is returned, which
 *      the caller should free.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static PACL
SetDacl(PACL pacl,       // IN, the original dacl
        PSID psid,       // IN
        DWORD rights,    // IN
        BOOL isAllow)    // IN, TRUE/FALSE = add allow/deny ACE
{
   PACL pNewAcl = NULL;
   ACL_SIZE_INFORMATION aclSizeInfo;
   DWORD dwNewAclSize;
   PVOID pTempAce;

   //
   // initialize
   //
   ZeroMemory(&aclSizeInfo, sizeof(ACL_SIZE_INFORMATION));
   aclSizeInfo.AclBytesInUse = sizeof(ACL); // or should it be inited to 0?

   //
   // call only if NULL dacl
   //
   if (pacl != NULL) {
      //
      // determine the size of the ACL info
      //
      if (!GetAclInformation(pacl, (LPVOID)&aclSizeInfo,
                             sizeof(ACL_SIZE_INFORMATION),
                             AclSizeInformation)) {
         goto error;
      }
   }

   //
   // compute the size of the new acl
   //
   dwNewAclSize = aclSizeInfo.AclBytesInUse + sizeof(ACCESS_ALLOWED_ACE) +
                  GetLengthSid(psid) - sizeof(DWORD);

   pNewAcl = (PACL) Util_SafeCalloc(dwNewAclSize, 1);

   //
   // initialize the new acl
   //
   if (!InitializeAcl(pNewAcl, dwNewAclSize, ACL_REVISION)) {
      err("InitializeAcl failed");
      goto error;
   }

   //
   // if DACL is present, copy ACEs from old DACL to the new one.
   //
   if (aclSizeInfo.AceCount) {
      int i;

      for (i = 0; i < aclSizeInfo.AceCount; i++) {
         // get an ACE
         if (!GetAce(pacl, i, &pTempAce)) {
            goto error;
         }

         // add the ACE to the new ACL
         // XXX Not respecting ACE order right now.
         if (!AddAce(pNewAcl, ACL_REVISION, MAXDWORD, pTempAce,
                     ((PACE_HEADER)pTempAce)->AceSize)) {
            goto error;
         }
      }
   }

   //
   // add ace to the dacl
   //
   if (isAllow) {
      if (!AddAccessAllowedAce(pNewAcl, ACL_REVISION, rights, psid)) {
         err("AddAccessAllowedAce failed");
         goto error;
      }
   } else {
      if (!AddAccessDeniedAce(pNewAcl, ACL_REVISION, rights, psid)) {
         err("AddAccessDeniedAce failed");
         goto error;
      }
   }

   return pNewAcl;

error:
   free(pNewAcl);
   pNewAcl = NULL;

   return pNewAcl;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_ModifyRights --
 *
 *      Adds allow/deny rights for a user to a file.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      DACL of the file is replaced.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_ModifyRights(PSID psid,         // IN
                     ConstUnicode path, // IN
                     DWORD rights,      // IN
                     BOOL isAllow)      // IN
{
   BOOL ret = TRUE;
   PSECURITY_DESCRIPTOR psd = NULL;
   PACL pacl = NULL;
   PACL paclNew = NULL;
   BOOL bDaclExist;
   BOOL bDaclPresent;
   SECURITY_DESCRIPTOR sd;
   SECURITY_INFORMATION securityInformation = DACL_SECURITY_INFORMATION;
   const utf16_t *pathW = NULL;

#ifdef W32UTIL_VERBOSE
   {
      LPTSTR sidString = NULL;
      if (!ConvertSidToStringSid(psid, &sidString)) {
         err("ConvertSidToStringSid");
         ret = FALSE;
         goto cleanup;
      }
      wprintf(L"\nModify rights for %s (%s): (%s)%lx\n",
              user, sidString, isAllow ? "+" : "-", rights);
      LocalFree(sidString);
   }
#endif

   /*
    * Obtain the DACL for path.
    */

   if (!W32Util_GetSecurityDescriptor(path, &psd)) {
      err("W32Util_GetSecurityDescriptor");
      goto cleanup;
   }
   if (!GetSecurityDescriptorDacl(psd, &bDaclPresent, &pacl, &bDaclExist)) {
      err("GetSecurityDescriptorDacl");
      goto cleanup;
   }

   /* Modifies the DACL */
   paclNew = SetDacl(bDaclPresent ? pacl : NULL, psid, rights, isAllow);
   if (paclNew == NULL) {
      goto cleanup;
   }

   /* Initializes the security descriptor to be associated with the DACL */
   if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
      err("InitializeSecurityDescriptor");
      ret = FALSE;
      goto cleanup;
   }

   /* Assigns DACL to SD */
   if (!SetSecurityDescriptorDacl(&sd, TRUE, paclNew, FALSE)) {
      err("SetSecurityDescriptorDacl");
      ret = FALSE;
      goto cleanup;
   }

   /* Modifies the file's DACL */
   pathW = UNICODE_GET_UTF16(path);

   if (!SetFileSecurityW(pathW, securityInformation, &sd)) {
      err("SetFileSecurity failed");
   }

cleanup:
   free(paclNew);
   free(psd);
   UNICODE_RELEASE_UTF16(pathW);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * AddSidIfNew --
 *
 *      Adds a copy of an SID to the SID list if it is not already found
 *      in the list AND it satisfy criteria imposed by match function.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      An entry can be added to the sidList.
 *
 *----------------------------------------------------------------------
 */

static BOOL
AddSidIfNew(PSID *sidList,
            PSID newSid,
            SidFilterFunction match,
            void *cbData)
{
   PSID *ppsid;
   ASSERT(sidList);

   for (ppsid = sidList; *ppsid; ppsid++) {
      if (EqualSid(*ppsid, newSid)) {
         return FALSE;
      }
   }

   if (match == NULL || match(newSid, cbData)) {
      DWORD sidSize = GetLengthSid(newSid);

      *ppsid = (PSID) Util_SafeMalloc(sidSize);
      CopySid(sidSize, *ppsid, newSid);

      return TRUE;
   }

   return FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_FreeSids --
 *
 *      Frees the SID pointer list.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
W32Util_FreeSids(PSID *sidList)
{
   PSID *ppsid;

   for (ppsid = sidList; *ppsid; ppsid++) {
      free(*ppsid);
   }
   free(sidList);
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetMatchingSids --
 *
 *      Finds the SIDs associated with a file by iterating through ACEs in its
 *      DACL, and returns a list of pointers to distinct SIDs that satisfy
 *      criteria imposed by the filter function.
 *
 *      Caller should call W32Util_FreeSids to free the list.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      SID pointer list is allocated.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_GetMatchingSids(ConstUnicode path,          // IN
                        PSID **psidList,            // IN/OUT
                        SidFilterFunction matchCb,  // IN
                        void *cbData)               // IN
{
   PSECURITY_DESCRIPTOR pSD = NULL;
   PACL pAcl = NULL;
   BOOL bDaclExist;
   BOOL bDaclPresent;
   EXPLICIT_ACCESSW *aclEntries = NULL;
   EXPLICIT_ACCESSW *aclEntry = NULL;
   unsigned int i;
   unsigned int numEntries;
   BOOL ret = FALSE;

   if (!W32Util_GetSecurityDescriptor(path, &pSD)) {
      err("W32Util_GetSecurityDescriptor");
      goto cleanup;
   }

   if (!GetSecurityDescriptorDacl(pSD, &bDaclPresent, &pAcl, &bDaclExist)) {
      err("GetSecurityDescriptorDacl");
      goto cleanup;
   }

   if (!bDaclPresent) {
      pAcl = NULL;
      goto cleanup;
   }

   if (GetExplicitEntriesFromAclW(pAcl, &numEntries, &aclEntries) !=
       ERROR_SUCCESS) {
      Warning("GetExplicitEntriesFromAclW failed: error code 0x%lx\n",
              GetLastError());
      goto cleanup;
   }

   *psidList = Util_SafeCalloc(numEntries + 1, sizeof **psidList);

   for (i = 0, aclEntry = aclEntries; i < numEntries; i++, aclEntry++) {
      if (aclEntry->Trustee.TrusteeForm == TRUSTEE_BAD_FORM) {
         Warning("trustee is in bad form\n");
      } else if (aclEntry->Trustee.TrusteeForm == TRUSTEE_IS_SID) {
         AddSidIfNew(*psidList, (SID *)aclEntry->Trustee.ptstrName, matchCb,
                     cbData);
      } else {
         Warning("Unhandled trustee form %d\n", aclEntry->Trustee.TrusteeForm);
         goto cleanup;
      }
   }

cleanup:
   free(pSD);
   LocalFree(aclEntries);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * GetEffectiveRights --
 *
 *      Returns the effective rights in the form of a DWORD access mask
 *      for a given trustee on a resource path.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static BOOL
GetEffectiveRights(PTRUSTEE_W trustee, // IN
                   ConstUnicode path,  // IN
                   DWORD *rights)      // OUT
{
   ACCESS_MASK accessMask = 0;
   PSECURITY_DESCRIPTOR pSD = NULL;
   PACL pAcl = NULL;
   BOOL bDaclExist;
   BOOL bDaclPresent;
   BOOL ret = FALSE;
#ifdef W32UTIL_VERBOSE
   int i = 0;
#endif

   if (!W32Util_GetSecurityDescriptor(path, &pSD)) {
      err("W32Util_GetSecurityDescriptor");
      goto cleanup;
   }

   if (!GetSecurityDescriptorDacl(pSD, &bDaclPresent, &pAcl, &bDaclExist)) {
      err("GetSecurityDescriptorDacl");
      goto cleanup;
   }

   if (GetEffectiveRightsFromAclW(pAcl, trustee,
                                  &accessMask) != ERROR_SUCCESS) {
      err("GetEffectiveRightsFromAcl");
      goto cleanup;
   } else {
      Warning("Access rights = %lx\n", accessMask);
   }

#ifdef W32UTIL_VERBOSE
   for (i = 0; i < 32; i++) {
      BOOL isSet = accessMask & (0x1 << i);

      if (isSet || accessRights[i][0] != '?') {
         Warning("  %3.3d : %s %s\n",
                 i, isSet ? "[x]" : "[ ]", accessRights[i]);
      }
   }
#endif

   *rights = accessMask;
   ret = TRUE;

cleanup:
   free(pSD);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_GetEffectiveRightsForName --
 * W32Util_GetEffectiveRightsForSid --
 *
 *      Returns the effective rights in the form of a DWORD access mask
 *      for a given trustee. Trustee can be identified either by its
 *      name or SID.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

BOOL
W32Util_GetEffectiveRightsForName(ConstUnicode user,  // IN
                                  ConstUnicode path,  // IN
                                  DWORD *rights)      // IN/OUT
{
   TRUSTEE_W trustee;
   const utf16_t *userW;

   userW = UNICODE_GET_UTF16(user);
   BuildTrusteeWithNameW(&trustee, (LPWSTR) userW);
   UNICODE_RELEASE_UTF16(userW);

#ifdef W32UTIL_VERBOSE
   Warning("Effective rights for %s on %s:\n\n", UTF8(user), path);
#endif
   return GetEffectiveRights(&trustee, path, rights);
}


BOOL
W32Util_GetEffectiveRightsForSid(PSID psid,          // IN
                                 ConstUnicode path,  // IN
                                 DWORD *rights)      // IN/OUT
{
   TRUSTEE_W trustee;

   BuildTrusteeWithSidW(&trustee, psid);

   return GetEffectiveRights(&trustee, path, rights);
}

/* ============================ XXX Reorg? ============================== */


/*
 *----------------------------------------------------------------------
 *
 * W32Util_AccessCheck --
 *
 *      Checks if a principal as identified by the user token has specific
 *      access rights to an object as identified by the security desciptor.
 *
 * Results:
 *      TRUE on success. FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_AccessCheck(HANDLE token,                 // IN
                    PSECURITY_DESCRIPTOR pSec,    // IN
                    int desiredAccess)            // IN
{
   Bool ret;
   GENERIC_MAPPING genMap;
   DWORD accessMask;
   PRIVILEGE_SET privSet;
   DWORD privSetLength;
   DWORD grantedAccess;
   BOOL accessStatus;

   ASSERT(token != INVALID_HANDLE_VALUE);

   genMap.GenericRead = FILE_GENERIC_READ;
   genMap.GenericWrite = FILE_GENERIC_WRITE;
   genMap.GenericExecute = FILE_GENERIC_EXECUTE;
   /* genMap.GenericAll should really be FILE_ALL_ACCESS? */
   genMap.GenericAll = FILE_GENERIC_READ |
                       FILE_GENERIC_WRITE |
                       FILE_GENERIC_EXECUTE;
   accessMask = desiredAccess;
   MapGenericMask(&accessMask, &genMap);
   privSetLength = sizeof privSet;
   if (!AccessCheck(pSec, token, accessMask, &genMap, &privSet, &privSetLength,
                    &grantedAccess, &accessStatus)) {
      Warning("%s: Unable to check access rights. %d: %s\n",
              __FUNCTION__, GetLastError(), Msg_ErrString());
      ret = FALSE;
      goto end;
   }
   ret = accessStatus;

end:
   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * CheckAccessUsingOpen --
 *
 *      Checks if the current process (or acct being impersonated by
 *      the caller) has specific access rights to an object.  This
 *      check is performed by attempting to open the file with
 *      the specified access rights.
 *
 * Results:
 *      TRUE if all desired access is granted, otherwise FALSE
 *      (i.e., error, or not all access is granted)
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static BOOL
CheckAccessUsingOpen(ConstUnicode fileName,     // IN: filename to check
                     ACCESS_MASK desiredAccess) // IN: desired permissions
{
   BOOL accessStatus = FALSE;
   HANDLE fileHandle;
   const utf16_t *fileNameW;

   ASSERT(fileName);

   fileNameW = UNICODE_GET_UTF16(fileName);

   fileHandle = CreateFileW(fileNameW, desiredAccess, FILE_SHARE_READ |
                            FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   UNICODE_RELEASE_UTF16(fileNameW);

   if (fileHandle == INVALID_HANDLE_VALUE) {
      DWORD lastError = GetLastError();

      /*
       * Treat the file-in-use case as meaning access was granted.
       * Any other error codes to special case?
       */

      switch (lastError) {
      case ERROR_SHARING_VIOLATION:
         accessStatus = TRUE;
         break;
      case ERROR_ACCESS_DENIED:
         break;
      default:
         break;
      }
   } else {
      accessStatus = TRUE;
      CloseHandle(fileHandle);
   }

   return accessStatus;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_HasAccessToFile --
 *
 *      Checks if current [impersonated] account has specific
 *      access rights to an object as identified by the
 *      security desciptor.
 *
 * Results:
 *      TRUE if all desired access is granted, otherwise FALSE
 *      (i.e., error, or not all access is granted)
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_HasAccessToFile(ConstUnicode filename,     // IN: filename
                        ACCESS_MASK desiredAccess, // IN: desired access
                        HANDLE token)              // IN: login session (OPT)
{
   BOOL accessStatus = FALSE;

   ASSERT(filename);

#if 0
   if (token == INVALID_HANDLE_VALUE || token == NULL) {
      /* try to compute a token ?? */
   }
#endif

   if (token != INVALID_HANDLE_VALUE && token != NULL) {
      PSECURITY_DESCRIPTOR sd = NULL;
      GENERIC_MAPPING genMap = {FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                                FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
      PRIVILEGE_SET privSet = {0};
      DWORD privSetLength = sizeof privSet;
      ACCESS_MASK mappedDesiredAccess = desiredAccess;
      ACCESS_MASK grantedAccess = 0;
      HANDLE iToken = INVALID_HANDLE_VALUE;

      if (!W32Util_GetSecurityDescriptor(filename, &sd)) {
         Warning("%s: Unable to get SD. %d\n", __FUNCTION__, GetLastError());
         goto end;
      }

      if (DuplicateToken(token, SecurityImpersonation, &iToken) == 0) {
         Warning("%s: Unable to duplicate token %d\n", __FUNCTION__,
                 GetLastError());
         free(sd);
         goto end;
      }

      MapGenericMask(&mappedDesiredAccess, &genMap);

      if (!AccessCheck(sd, iToken, mappedDesiredAccess, &genMap, &privSet,
                       &privSetLength, &grantedAccess, &accessStatus)) {
         Warning("%s: Unable to check access rights. %d\n", __FUNCTION__,
                 GetLastError());
         accessStatus = FALSE;
      }
      free(sd);

      CloseHandle(iToken);
   }

end:
   if (accessStatus == FALSE) {
      accessStatus = CheckAccessUsingOpen(filename, desiredAccess);
   }

   return accessStatus;
}


/*
 *----------------------------------------------------------------------------
 *
 * W32Util_GetThreadHandle --
 *
 *      Returns a real handle to the current thread; unlike the pseudo
 *      handle returned from GetCurrentThread(), this handle is valid in
 *      other threads of this process.
 *
 * Returns:
 *      TRUE if the handle was successfully duplicated, false otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

BOOL
W32Util_GetThreadHandle(HANDLE *handle) {

   return DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                          GetCurrentProcess(), handle,
                          0, FALSE, DUPLICATE_SAME_ACCESS);

}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_InitStdioConsole --
 *
 *    Creates a text console, and attaches the standard stdio files to it.
 *
 * Results:
 *    void
 *
 * Side effects:
 *    stdio works.
 *
 *-----------------------------------------------------------------------------
 */

void
W32Util_InitStdioConsole(void)
{
   HANDLE handle;
   int fd;
   FILE *fp;

   if (AllocConsole() == 0) {
      Warning("Error in AllocConsole(). %d\n", GetLastError());

      return;
   }

   handle = GetStdHandle(STD_INPUT_HANDLE);
   if (handle == INVALID_HANDLE_VALUE) {
      Warning("Error in GetStdHandle(STD_INPUT_HANDLE). %d\n", GetLastError());
   }
   fd = _open_osfhandle((long) handle, _O_RDONLY);
   ASSERT(fd >= 0);
   fclose(stdin);
   fp = _fdopen(fd, "r");
   ASSERT(fp);

   handle = GetStdHandle(STD_OUTPUT_HANDLE);
   if (handle == INVALID_HANDLE_VALUE) {
      Warning("Error in GetStdHandle(STD_OUTPUT_HANDLE). %d\n",
              GetLastError());
   }
   fd = _open_osfhandle((long) handle, 0);
   ASSERT(fd >= 0);
   fclose(stdout);
   fp = _fdopen(fd, "w");
   ASSERT(fp);

   handle = GetStdHandle(STD_ERROR_HANDLE);
   if (handle == INVALID_HANDLE_VALUE) {
      Warning("Error in GetStdHandle(STD_ERROR_HANDLE). %d\n", GetLastError());
   }
   fd = _open_osfhandle((long) handle, 0);
   ASSERT(fd >= 0);
   fclose(stderr);
   fp = _fdopen(fd, "w");
   ASSERT(fp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_ExitStdioConsole --
 *
 *    Destroys console created by InitStdioConsole.
 *
 * Results:
 *    void
 *
 * Side effects:
 *    stdio ceases working.
 *
 *-----------------------------------------------------------------------------
 */

void
W32Util_ExitStdioConsole(void)
{
   if (FreeConsole() == 0) {
      Warning("Error in FreeConsole(): %d.\n", GetLastError());
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_CreateWellKnownSid --
 *
 *    Wrapper around the post-Win2k only API CreateWellKnownSid. Fails
 *    on Win2k.
 *
 * Results:
 *    TRUE on success, FALSE on failure. The allocated SID should be
 *    free'd with free().
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_CreateWellKnownSid(WELL_KNOWN_SID_TYPE wsdType,  // IN
                           PSID domainSid,               // IN
                           PSID *pSid)                   // OUT
{
   Bool succeeded = FALSE;
   DWORD size;
   static HMODULE hAdvapi = NULL;
   static CreateWellKnownSidFnType CreateWellKnownSidFn = NULL;

   *pSid = NULL;

   if (!hAdvapi) {
      hAdvapi = LoadLibraryW(L"advapi32.dll");
   }

   if (hAdvapi && !CreateWellKnownSidFn) {
      CreateWellKnownSidFn = (CreateWellKnownSidFnType)
         GetProcAddress(hAdvapi, "CreateWellKnownSid");
   }

   if (!CreateWellKnownSidFn) {
      Warning("%s: GetProcAddress: %d\n", __FUNCTION__, GetLastError());
      goto exit;
   }

   size = SECURITY_MAX_SID_SIZE;
   *pSid = (PSID) Util_SafeMalloc(size);
   succeeded = CreateWellKnownSidFn(wsdType, domainSid, *pSid, &size);

   if (!succeeded) {
      Warning("%s: CreateWellKnownSid: %d\n", __FUNCTION__, GetLastError());
      goto exit;
   }

   succeeded = TRUE;

  exit:
   if (!succeeded) {
      free(*pSid);
      *pSid = NULL;
   }

   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_GetCurrentUserSid --
 *
 *    Returns the SID of the thread's current user.
 *
 * Results:
 *    TRUE on success, FALSE on failure. The allocated SID should be
 *    free'd with free().
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_GetCurrentUserSid(PSID *pSid)  // OUT
{
   Bool succeeded = FALSE;
   HANDLE hToken = NULL;
   TOKEN_USER *tokenUser = NULL;
   DWORD retLen, sidSize;

   ASSERT(pSid);

   if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hToken)) {
      if (ERROR_NO_TOKEN != GetLastError()) {
         Warning("%s: OpenThreadToken: %d\n", __FUNCTION__, GetLastError());
         goto exit;
      }

      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
         Warning("%s: OpenProcessToken: %d\n", __FUNCTION__, GetLastError());
         goto exit;
      }
   }

   GetTokenInformation(hToken, TokenUser, NULL, 0, &retLen);

   tokenUser = (PTOKEN_USER) Util_SafeMalloc(retLen);

   if (!GetTokenInformation(hToken, TokenUser, tokenUser, retLen, &retLen)) {
      Warning("%s: GetTokenInformation: %d\n", __FUNCTION__, GetLastError());
      goto exit;
   }

   sidSize = GetLengthSid(tokenUser->User.Sid);

   *pSid = Util_SafeMalloc(sidSize);
   memcpy(*pSid, tokenUser->User.Sid, sidSize);

   succeeded = TRUE;

  exit:
   if (!succeeded) {
      *pSid = NULL;
   }

   if (hToken) {
      CloseHandle(hToken);
   }

   free(tokenUser);

   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_GetLocalAdminGroupSid --
 *
 *    Returns the SID of the local admin group.
 *
 * Results:
 *    TRUE on success, FALSE on failure. The allocated SID should be
 *    free'd with free().
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_GetLocalAdminGroupSid(PSID *pSid)  // OUT
{
   Bool succeeded = FALSE;
   PSID adminSid = NULL;
   DWORD sidSize;
   SID_IDENTIFIER_AUTHORITY siaNtAuthority = SECURITY_NT_AUTHORITY;

   ASSERT(pSid);

   if (!AllocateAndInitializeSid(&siaNtAuthority, 2,
          SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
          0, 0, 0, 0, 0, 0, &adminSid)) {
      Warning("%s: AllocateAndInitializeSid: %d\n", __FUNCTION__,
              GetLastError());
      goto exit;
   }

   sidSize = GetLengthSid(adminSid);
   *pSid = Util_SafeMalloc(sidSize);

   memcpy(*pSid, adminSid, sidSize);
   succeeded = TRUE;

  exit:
   if (adminSid) {
      FreeSid(adminSid);
   }

   if (!succeeded) {
      *pSid = NULL;
   }

   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_IsDirectorySafe --
 *
 *    Checks if the specified directory is a 'safe' directory, defined
 *    as a directory that has a DACL that gives access to only local
 *    admins and the current user, is owned by the current user, and is
 *    not a directory junction.
 *
 * Results:
 *    TRUE if the specified directory is safe, FALSE otherwise.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_IsDirectorySafe(ConstUnicode path)  // IN
{
   Bool succeeded = FALSE;
   Bool sawCorrectAdminAce = FALSE;
   Bool sawCorrectCurUserAce = FALSE;
   PSID adminSid = NULL;
   PSID curUserSid = NULL;
   PSID owner = NULL;
   DWORD err;
   PACL dacl = NULL;
   PSECURITY_DESCRIPTOR sd = NULL;
   SECURITY_DESCRIPTOR_CONTROL control;
   DWORD revision;
   ULONG numEntries;
   PEXPLICIT_ACCESSW eas = NULL;
   int i;
   DWORD attribs;
   Unicode path2;
   const utf16_t *path2W;

   ASSERT(path);

   /* nuke trailing slashes */
   path2 = Unicode_Duplicate(path);

   while (Unicode_EndsWith(path2, "/") &&
          Unicode_EndsWith(path2, "\\")) {
      Unicode temp = Unicode_Truncate(path2,
                                      Unicode_LengthInCodeUnits(path2) - 1);
      Unicode_Free(path2);
      path2 = temp;
   }

   path2W = UNICODE_GET_UTF16(path2);

   /* check is a directory and is not a directory junction */
   attribs = GetFileAttributesW(path2W);

   if ((INVALID_FILE_ATTRIBUTES == attribs) ||
       !(attribs & FILE_ATTRIBUTE_DIRECTORY) ||
       (attribs & FILE_ATTRIBUTE_REPARSE_POINT)) {
      Log("%s: Failed directory attributes check, \"%s\"\n", __FUNCTION__,
          UTF8(path2));
      goto exit;
   }

   /* get admin and current user sids */
   if (!W32Util_GetLocalAdminGroupSid(&adminSid) ||
       !W32Util_GetCurrentUserSid(&curUserSid)) {
      Log("%s: Couldn't get local admin or user SID\n", __FUNCTION__);
      goto exit;
   }

   /* get current security info */
   err = GetNamedSecurityInfoW((LPWSTR) path2W, SE_FILE_OBJECT,
                               DACL_SECURITY_INFORMATION |
                               OWNER_SECURITY_INFORMATION, &owner, NULL,
                               &dacl, NULL, &sd);

   if (ERROR_SUCCESS != err) {
      Log("%s: GetNamedSecurityInfoW failed: %d\n", __FUNCTION__,
          GetLastError());
      goto exit;
   }

   /* DACL must exist */
   if (!dacl) {
      Log("%s: Failed DACL presence check, \"%s\"\n", __FUNCTION__,
          UTF8(path2));
      goto exit;
   }

   /* check that owner matches (assume NULL owner is match?) */
   if (owner && !EqualSid(curUserSid, owner) &&
       !EqualSid(adminSid, owner)) {
      Log("%s: Failed owner SID match, \"%s\"\n", __FUNCTION__, UTF8(path2));
      goto exit;
   }

   /* check that DACL doesn't inherit ACEs */
   if (!GetSecurityDescriptorControl(sd, &control, &revision)) {
      Log("%s: GetSecurityDescriptorControl failed: %d\n", __FUNCTION__,
          GetLastError());
      goto exit;
   }

   if (!(control & SE_DACL_PROTECTED)) {
      Log("%s: Failed DACL inherit check, \"%s\"\n", __FUNCTION__,
          UTF8(path2));
      goto exit;
   }

   /* check the DACL ACEs */
   err = GetExplicitEntriesFromAclW(dacl, &numEntries, &eas);

   if (ERROR_SUCCESS != err) {
      Log("%s: GetExplicitEntriesFromAclW failed: %d\n", __FUNCTION__,
          GetLastError());
      goto exit;
   }

   if (2 != numEntries) {
      Log("%s: Failed DACL num entries check, \"%s\"\n", __FUNCTION__,
          UTF8(path2));
      goto exit;
   }

   for (i = 0; i < numEntries; i++) {
      DWORD requiredAccessRights = STANDARD_RIGHTS_READ |
                                   STANDARD_RIGHTS_WRITE;
      PSID sid;

      if (TRUSTEE_IS_SID != eas[i].Trustee.TrusteeForm) {
         Log("%s: Failed trustee SID identity check, \"%s\"\n", __FUNCTION__,
             UTF8(path2));
         goto exit;
      }

      sid = (PSID) eas[i].Trustee.ptstrName;

      if (EqualSid(sid, adminSid)) {
         if ((GRANT_ACCESS == eas[i].grfAccessMode) &&
             (eas[i].grfAccessPermissions & requiredAccessRights)) {
            sawCorrectAdminAce = TRUE;
         }
      } else if (EqualSid(sid, curUserSid)) {
         if ((GRANT_ACCESS == eas[i].grfAccessMode) &&
             (eas[i].grfAccessPermissions & requiredAccessRights)) {
            sawCorrectCurUserAce = TRUE;
         }
      }
   }

   if (!sawCorrectCurUserAce || !sawCorrectAdminAce) {
      Log("%s: Failed cur user and admin ACE presence check, \"%s\"\n",
          __FUNCTION__, UTF8(path2));
      goto exit;
   }

   succeeded = TRUE;

  exit:
   Unicode_Free(path2);
   UNICODE_RELEASE_UTF16(path2W);
   free(adminSid);
   free(curUserSid);
   LocalFree(sd);
   LocalFree(eas);

   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_MakeSafeDirectory --
 *
 *    Creates a new 'safe' directory with a DACL that allows access only
 *    to the current user and the local Administrators group, and is
 *    owned by the current user.
 *
 * Results:
 *    TRUE on success, FALSE on failure.
 *
 * Side effects:
 *    A safe directory is born.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_MakeSafeDirectory(ConstUnicode path)  // IN
{
   Bool succeeded = FALSE;
   PSID adminSid = NULL;
   PSID curUserSid = NULL;
   PACL dacl = NULL;
   DWORD daclSize;
   SECURITY_ATTRIBUTES sa;
   SECURITY_DESCRIPTOR sd;
   DWORD rightsToGive = GENERIC_ALL | STANDARD_RIGHTS_ALL;
   Unicode path2;
   const utf16_t *path2W;
   static HMODULE hAdvapi = NULL;
   static SetSecurityDescriptorControlFnType SetSecurityDescriptorControlFn =
      NULL;

   ASSERT(path);

   /* nuke trailing slashes */
   path2 = Unicode_Duplicate(path);

   while (Unicode_EndsWith(path2, "/") &&
          Unicode_EndsWith(path2, "\\")) {
      Unicode temp = Unicode_Truncate(path2,
                                      Unicode_LengthInCodeUnits(path2) - 1);
      Unicode_Free(path2);
      path2 = temp;
   }

   path2W = UNICODE_GET_UTF16(path2);

   /* get admin and current user sids */
   if (!W32Util_GetLocalAdminGroupSid(&adminSid) ||
       !W32Util_GetCurrentUserSid(&curUserSid)) {
      Log("%s: Couldn't get local admin or user SID\n", __FUNCTION__);
      goto exit;
   }

   /* create DACL with only current user and admin access */
   daclSize = sizeof(ACL) + (sizeof(ACCESS_ALLOWED_ACE) * 2) -
              (sizeof(DWORD) * 2) + GetLengthSid(adminSid) +
               GetLengthSid(curUserSid);

   dacl = (PACL) Util_SafeMalloc(daclSize);

   if (!InitializeAcl(dacl, daclSize, ACL_REVISION)) {
      Log("%s: Couldn't init ACL\n", __FUNCTION__);
      goto exit;
   }

   if (!AddAccessAllowedAce(dacl, ACL_REVISION, rightsToGive, adminSid) ||
       !AddAccessAllowedAce(dacl, ACL_REVISION, rightsToGive, curUserSid)) {
      Log("%s: Couldn't add SID to DACL\n", __FUNCTION__);
      goto exit;
   }

   /* init SD */
   if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
      Log("%s: InitializeSecurityDescriptor failed: %d\n", __FUNCTION__,
          GetLastError());
      goto exit;
   }

   /* set SD DACL */
   if (!SetSecurityDescriptorDacl(&sd, TRUE, dacl, FALSE)) {
      Log("%s: SetSecurityDescriptorOwner failed: %d\n", __FUNCTION__,
          GetLastError());
   }

   /* set SD owner */
   if (!SetSecurityDescriptorOwner(&sd, curUserSid, FALSE)) {
      Log("%s: SetSecurityDescriptorOwner failed: %d\n", __FUNCTION__,
          GetLastError());
   }

   /* turn off DACL ACE inheritance (post-NT4 only) */
   if (!hAdvapi) {
      hAdvapi = LoadLibraryW(L"advapi32.dll");
   }

   if (hAdvapi && !SetSecurityDescriptorControlFn) {
      SetSecurityDescriptorControlFn = (SetSecurityDescriptorControlFnType)
         GetProcAddress(hAdvapi, "SetSecurityDescriptorControl");
   }

   if (SetSecurityDescriptorControlFn &&
       !SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED,
                                     SE_DACL_PROTECTED)) {
      Log("%s: SetSecurityDescriptorControl failed: %d\n", __FUNCTION__,
          GetLastError());
   }

   /* init SA */
   sa.nLength = sizeof sa;
   sa.bInheritHandle = FALSE;
   sa.lpSecurityDescriptor = &sd;

   /* create the new directory */
   if (!CreateDirectoryW(path2W, &sa)) {
      Log("%s: CreateDirectoryW failed: %d\n", __FUNCTION__,
          GetLastError());
   }

   succeeded = TRUE;

  exit:
   Unicode_Free(path2);
   UNICODE_RELEASE_UTF16(path2W);
   free(adminSid);
   free(curUserSid);
   free(dacl);

   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_DoesVolumeSupportAcls --
 *
 *    Determines if the volume that the pathname resides on supports
 *    ACLs.
 *
 * Results:
 *    TRUE if it does, FALSE if it doesn't.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_DoesVolumeSupportAcls(ConstUnicode path)  // IN
{
   Bool res;
   Unicode vol, vol2;
   const utf16_t *vol2W;
   DWORD fsFlags;
   Bool succeeded = FALSE;

   ASSERT(path);

   File_SplitName(path, &vol, NULL, NULL);
   vol2 = Unicode_Append(vol, DIRSEPS);

   vol2W = UNICODE_GET_UTF16(vol2);
   res = GetVolumeInformationW(vol2W, NULL, 0, NULL, NULL, &fsFlags, NULL, 0);
   UNICODE_RELEASE_UTF16(vol2W);

   if (res) {
      if ((fsFlags & FS_PERSISTENT_ACLS) == 0) {
         goto exit;
      }
   } else {
      Log("%s: GetVolumeInformation failed: %d\n", __FUNCTION__,
          GetLastError());
      goto exit;
   }

   succeeded = TRUE;

  exit:
   Unicode_Free(vol);
   Unicode_Free(vol2);

   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_GetRegistryAutorun --
 *
 *    Get the Autorun state from the registry.
 *
 *    Reads the different registry settings to determine if autorun has been
 *    disabled:
 *    * "HKLM\SYSTEM\CurrentControlSet\Services\Cdrom\Autorun"
 *       - Older version
 *    * "HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\...
 *          ...\Explorer\NoDriveTypeAutoRun"
 *       - Newer, less harsh version (does not block Media Change
 *          Notification (MCN) messages coming from the device allowing the
 *          drive state to be reflected in explorer).
 *
 * Results:
 *    TRUE for a successful read; FALSE otherwise (ie. lacking permissions).
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_GetRegistryAutorun(AutorunState* state) // OUT: State of autorun
{
   DWORD autorun = 1;
   RegistryError ret;

   // Check the old autorun setting
   *state = AUTORUN_UNDEFINED;
   ret = W32UtilGetRegDWORD(HKEY_LOCAL_MACHINE,
                            "SYSTEM\\CurrentControlSet\\Services\\Cdrom",
                            "Autorun",
                            &autorun);

   if (ret == REGISTRY_SUCCESS || ret == REGISTRY_KEY_DOES_NOT_EXIST) {
      // The "Autorun"  setting overrides the "NoDriveTypeAutoRun"  setting.
      if (ret == REGISTRY_SUCCESS && autorun == 0) {
         *state = AUTORUN_OFF;

         return TRUE;
      }

      // We can go ahead and check the more passive autorun setting
      ret = W32UtilGetRegDWORD(HKEY_LOCAL_MACHINE,
                               "Software\\Microsoft\\Windows\\CurrentVersion"
                               "\\Policies\\Explorer",
                               "NoDriveTypeAutoRun",
                               &autorun);

      // If the value does not exist, the default is AUTORUN_VALUE_ON
      if (ret == REGISTRY_KEY_DOES_NOT_EXIST) {
         *state = AUTORUN_ON;

         return TRUE;
      }

      if (ret == REGISTRY_SUCCESS) {
         if (autorun == AUTORUN_VALUE_OFF) {
            *state = AUTORUN_OFF;
         } else if (autorun == AUTORUN_VALUE_ON) {
            *state = AUTORUN_ON;
         } else {
            *state = AUTORUN_UNDEFINED;
         }

         return TRUE;
      }
   }

   // We must have run into an error while checking the registry. So,
   // we cannot determine if autorun is on or off.

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_SetRegistryAutorun --
 *
 *    Set the Autorun state in the registry.
 *
 *    Writes to these registry paths:
 *    * "HKLM\SYSTEM\CurrentControlSet\Services\Cdrom\Autorun"
 *       - Sets the older version back to the default.
 *    * "HKLM\Software\Microsoft\Windows\CurrentVersion\Policies\...
 *          ...\Explorer\NoDriveTypeAutoRun"
 *       - Sets the newer, less harsh version (does not block Media Change
 *          Notification (MCN) messages coming from the device allowing the
 *          drive state to be reflected in explorer).
 *
 * Results:
 *    TRUE if the state was written to the registry successfully; FALSE for
 *    errors (no permission).
 *
 * Side effects:
 *    Writes to the Windows registry.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_SetRegistryAutorun(const AutorunState state)   // IN: State to set
{
   LONG retGet;
   LONG retSet;
   DWORD autorun = 1;

   // Don't need to do anything if the state requested is AUTORUN_UNDEFINED.
   if (state == AUTORUN_UNDEFINED) {
      return TRUE;
   }

   // Get the old registry setting.
   retGet = W32UtilGetRegDWORD(HKEY_LOCAL_MACHINE,
                               "SYSTEM\\CurrentControlSet\\Services\\Cdrom",
                               "Autorun",
                               &autorun);

   // The "Autorun" setting overrides the "NoDriveTypeAutoRun" setting, so
   //    bomb out if we cannot set it.
   if (retGet == REGISTRY_SUCCESS && autorun == 0) {
      retSet = W32UtilSetRegDWORD(HKEY_LOCAL_MACHINE,
                                  "SYSTEM\\CurrentControlSet\\Services\\Cdrom",
                                  "Autorun",
                                  1);
      if (retSet != REGISTRY_SUCCESS) {
         return FALSE;
      }
   }

   // Now we can go ahead and set the new value.
   if (retGet == REGISTRY_SUCCESS || retGet == REGISTRY_KEY_DOES_NOT_EXIST) {
      if (W32UtilSetRegDWORD(HKEY_LOCAL_MACHINE,
                             "Software\\Microsoft\\Windows\\CurrentVersion"
                              "\\Policies\\Explorer",
                             "NoDriveTypeAutoRun",
                             state == AUTORUN_OFF ?
                              AUTORUN_VALUE_OFF : AUTORUN_VALUE_ON)
          == REGISTRY_SUCCESS) {
         return TRUE;
      }
   }

   // We must have run into an error while writing to the registry
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32UtilGetRegDWORD --
 *
 *    Get the value of a DWORD variable in the registry.
 *
 * Results:
 *    REGISTRY_SUCCESS on a successful read or a different error code on
 *    failure.  The parameter valuePtr is left unchanged on failure.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

RegistryError
W32UtilGetRegDWORD(HKEY base,            // IN: Base key (ie. HKLM)
                   ConstUnicode subPath, // IN: Sub key path
                   ConstUnicode var,     // IN: Variable name at path location
                   DWORD *valuePtr)      // OUT: DWORD to store variable value
                                         // Unchanged on error
{
   HKEY hKey;
   LONG ret;
   DWORD value;
   DWORD len = sizeof value;
   DWORD type = REG_DWORD;
   const utf16_t *subPathW;
   const utf16_t *varW;
   RegistryError regErr;

   ASSERT(subPath);
   ASSERT(var);
   ASSERT(valuePtr);

   subPathW = UNICODE_GET_UTF16(subPath);
   varW = UNICODE_GET_UTF16(var);

   ret = RegOpenKeyExW(base, subPathW, 0, KEY_QUERY_VALUE, &hKey);

   if (ret == ERROR_SUCCESS) {
      ret = RegQueryValueExW(hKey, varW, NULL, &type, (LPBYTE) &value, &len);
      RegCloseKey(hKey);
   }

   if (type != REG_DWORD) {
      regErr = REGISTRY_TYPE_MISMATCH;
      goto exit;
   }

   switch (ret) {
   case ERROR_SUCCESS:
      *valuePtr = value;
      regErr = REGISTRY_SUCCESS;
      goto exit;
   case ERROR_FILE_NOT_FOUND:
      regErr = REGISTRY_KEY_DOES_NOT_EXIST;
      goto exit;
   case ERROR_ACCESS_DENIED:
      regErr = REGISTRY_ACCESS_DENIED;
      goto exit;
   default:
      Log("%s: A Windows registry operation failed: %d\n", __FUNCTION__, ret);
      regErr = REGISTRY_UNKNOWN_ERROR;
      goto exit;
   }

  exit:
   UNICODE_RELEASE_UTF16(subPathW);
   UNICODE_RELEASE_UTF16(varW);

   return regErr;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32UtilSetRegDWORD --
 *
 *    Set a DWORD variable in the registry.
 *
 *    If the key or value does not exist, it will be created.
 *
 * Results:
 *    REGISTRY_SUCCESS on a successful write or a different error code on
 *    failure.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

RegistryError
W32UtilSetRegDWORD(HKEY base,            // IN: Base key (ie. HKLM)
                   ConstUnicode subPath, // IN: Sub key path
                   ConstUnicode var,     // IN: Variable name at path location
                   DWORD value)          // IN: DWORD to write to registry
{
   LONG ret;
   HKEY hKey;
   const utf16_t *subPathW;
   const utf16_t *varW;
   RegistryError regErr;

   ASSERT(base);
   ASSERT(subPath);
   ASSERT(var);

   subPathW = UNICODE_GET_UTF16(subPath);
   varW = UNICODE_GET_UTF16(var);

   // Create/Open the given key path
   ret = RegCreateKeyExW(base, subPathW, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_SET_VALUE, NULL, &hKey, NULL);

   if (ret == ERROR_SUCCESS) {
      ret = RegSetValueExW(hKey, varW, 0, REG_DWORD, (LPBYTE) &value,
                           sizeof value);
      RegCloseKey(hKey);
   }

   switch (ret) {
   case ERROR_SUCCESS:
      regErr = REGISTRY_SUCCESS;
      goto exit;
   case ERROR_ACCESS_DENIED:
      regErr = REGISTRY_ACCESS_DENIED;
      goto exit;
   default:
      Log("%s: A Windows registry operation failed: %d\n", __FUNCTION__, ret);
      regErr = REGISTRY_UNKNOWN_ERROR;
      goto exit;
   }

  exit:
   UNICODE_RELEASE_UTF16(subPathW);
   UNICODE_RELEASE_UTF16(varW);

   return regErr;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32UtilRegDelete --
 *
 *    Delete a key or a variable at a specified location in the registry.
 *
 *    Deleting a key will delete the key, all the subkeys, and all the
 *    variables within the key.
 *
 *    Can delete a key by setting 'var' as NULL.
 *
 * Results:
 *    REGISTRY_SUCCESS on a successful delete or if the key or value does not
 *    exist; a different error code otherwise.
 *
 * Side effects:
 *    Deletes an element in the registry.
 *
 *-----------------------------------------------------------------------------
 */

RegistryError
W32UtilRegDelete(HKEY base,            // IN: Base key (ie. HKLM)
                 ConstUnicode subPath, // IN: Sub key path
                 ConstUnicode var)     // IN: Variable name at path location
{
   LONG ret;
   HKEY hKey;
   const utf16_t *subPathW;
   const utf16_t *varW = NULL;
   RegistryError regErr;

   ASSERT(base);
   ASSERT(subPath);

   subPathW = UNICODE_GET_UTF16(subPath);
   if (var) {
      varW = UNICODE_GET_UTF16(var);
   }

   // Delete the key and subkeys if there is no variable name
   if (var == NULL) {
      ret = RegDeleteKeyW(base, subPathW);
   } else {
      // Otherwise delete only the variable
      ret = RegOpenKeyExW(base, subPathW, 0, KEY_SET_VALUE, &hKey);

      if (ret == ERROR_SUCCESS) {
         ret = RegDeleteValueW(hKey, varW);
         RegCloseKey(hKey);
      }
   }

   switch (ret) {
   // If the location was not found, pretend like things are peachy keen.
   case ERROR_FILE_NOT_FOUND:
   case ERROR_SUCCESS:
      regErr = REGISTRY_SUCCESS;
      goto exit;
   case ERROR_ACCESS_DENIED:
      regErr = REGISTRY_ACCESS_DENIED;
      goto exit;
   default:
      Log("%s: A Windows registry operation failed: %d\n", __FUNCTION__, ret);
      regErr = REGISTRY_UNKNOWN_ERROR;
      goto exit;
   }

  exit:
   UNICODE_RELEASE_UTF16(subPathW);
   UNICODE_RELEASE_UTF16(varW);

   return regErr;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_AllowAdminCOM --
 *
 *    Check if launching elevated COM is allowed.
 *
 *    If the application is running under Vista and is running with
 *    limited token, we allow to create elevated COM.
 *
 * Results:
 *    TRUE if the elevated COM is allowed. FALSE, otherwise.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_AllowAdminCOM(void)
{
   Bool isAdmin = FALSE;
   OSVERSIONINFOA osvi = { 0 };

   osvi.dwOSVersionInfoSize = sizeof osvi;
   Win32U_GetVersionEx(&osvi);
   isAdmin = Util_HasAdminPriv() > 0;

   return osvi.dwMajorVersion >= VERSION_VISTA  && !isAdmin;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_ConstructSecureObjectSD --
 *
 *      We want some Windows objects (notably the VMX process and
 *      threads) to have security descriptors that don't allow
 *      non-admin users to open a handle to them with rights that
 *      could lead to exploits (code injection, stealing of handles,
 *      etc.)
 *
 *      Currently only process and thread objects are supported. The
 *      rights are tweaked to allow the user who owns the specified
 *      token terminate access to the object only (and makes admin the
 *      owner of the object).
 *
 * Results:
 *      The constructed security descriptor on success, NULL on failure.
 *      Caller must free with free().
 *
 * Side effects:
 *
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

PSECURITY_DESCRIPTOR
W32Util_ConstructSecureObjectSD(HANDLE hToken,          // IN (OPT)
                                SecureObjectType type)  // IN
{
   BOOL res = FALSE;
   PACL newDacl = NULL;
   DWORD newDaclLen;
   PSECURITY_DESCRIPTOR absSD = NULL;
   PSECURITY_DESCRIPTOR srSD = NULL;
   DWORD sdLen;
   DWORD err;
   SID_IDENTIFIER_AUTHORITY siaNtAuthority = SECURITY_NT_AUTHORITY;
   PSID adminsSid = NULL;
   TOKEN_OWNER *owner = NULL;
   DWORD retLen = 0;
   Bool isProcess = (SecureObject_Process == type);
   Bool freeToken = FALSE;

   if (NULL == hToken) {
      if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hToken)) {
         err = GetLastError();
         if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            err = GetLastError();
            goto exit;
         }
      }
      freeToken = TRUE;
   }

   // get token owner
   GetTokenInformation(hToken, TokenOwner, NULL, 0, &retLen);

   if (0 == retLen) {
      err = GetLastError();
      goto exit;
   }

   owner = (TOKEN_OWNER *) Util_SafeMalloc(retLen);

   if (!GetTokenInformation(hToken, TokenOwner, owner, retLen, &retLen)) {
      err = GetLastError();
      goto exit;
   }

   // make admin SID
   if (!AllocateAndInitializeSid(&siaNtAuthority, 2,
                                 SECURITY_BUILTIN_DOMAIN_RID, 
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &adminsSid)) {
      err = GetLastError();
      goto exit;
   }

   // alloc DACL
   newDaclLen = 1024;
   newDacl = (PACL) Util_SafeMalloc(newDaclLen);

   if (!InitializeAcl(newDacl, newDaclLen, ACL_REVISION)) {
      err = GetLastError();
      goto exit;
   }

   // add access-allowed ACE granting admins all rights
   if (!AddAccessAllowedAce(newDacl, ACL_REVISION,
                            isProcess ? PROCESS_ALL_ACCESS : THREAD_ALL_ACCESS,
                            adminsSid))	{
      err = GetLastError();
      goto exit;
   }

   if (isProcess) {
      // add access-allowed ACE granting the owner terminate rights
      if (!AddAccessAllowedAce(newDacl, ACL_REVISION, PROCESS_TERMINATE,
                               owner->Owner)) {
         err = GetLastError();
         goto exit;
      }
   }

   // alloc sec dec
   absSD = (PSECURITY_DESCRIPTOR) Util_SafeMalloc(sizeof(SECURITY_DESCRIPTOR));

   // init sec dec
   if (!InitializeSecurityDescriptor(absSD, SECURITY_DESCRIPTOR_REVISION)) {
      err = GetLastError();
      goto exit;
   }

   // set the DACL
   if (!SetSecurityDescriptorDacl(absSD, TRUE, newDacl, FALSE)) {
      err = GetLastError();
      goto exit;
   }

   // set the owner to admin
   if (!SetSecurityDescriptorOwner(absSD, adminsSid, FALSE)) {
      err = GetLastError();
      goto exit;
   }

   // make self-relative
   sdLen = 0;
   MakeSelfRelativeSD(absSD, NULL, &sdLen);

   if (0 == sdLen) {
      err = GetLastError();
      goto exit;
   }

   srSD = (PSECURITY_DESCRIPTOR) Util_SafeMalloc(sdLen);

   if (!MakeSelfRelativeSD(absSD, srSD, &sdLen)) {
      err = GetLastError();
      goto exit;
   }

   res = TRUE;

  exit:
   if (!res) {
      free(srSD);
   }

   if (adminsSid) {
      FreeSid(adminsSid);
   }

   if (freeToken) {
      CloseHandle(hToken);
   }

   free(absSD);
   free(newDacl);
   free(owner);

   return res ? srSD : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * W32Util_ReplaceObjectSD -- 
 *
 *      Replace the DACL and owner of an object. (Yes, technically
 *      not the entire SD but who's keeping track?)
 *
 * Return:
 *      TRUE if object was successfully modified, otherwise FALSE.
 *
 * Side effects:
 *      None beside those described above.
 *
 *----------------------------------------------------------------------
 */

Bool
W32Util_ReplaceObjectSD(HANDLE hObject,                  // IN
                        const PSECURITY_DESCRIPTOR pSD)  // IN
{
   Bool res = FALSE;
   DWORD err;

   ASSERT(hObject);
   ASSERT(pSD);

   if (!SetKernelObjectSecurity(hObject, DACL_SECURITY_INFORMATION |
                                OWNER_SECURITY_INFORMATION, pSD)) {
      err = GetLastError();
      goto exit; 
   }

   res = TRUE;

  exit:
   return res;
}


/*
 *-----------------------------------------------------------------------------
 *
 * W32Util_VerifyXPModeHostLicense --
 *
 *      Verifies whether host is licensed to run XP Mode VM.
 *
 * Results:
 *      TRUE if host has license.
 *      FALSE if running XP Mode VM should not be allowed.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
W32Util_VerifyXPModeHostLicense(void)
{
   Bool ret = FALSE;
   /* Whoever created VerifyVersionInfo must have been sick. */
   OSVERSIONINFOEX osVersion;
   ULONGLONG mask;

   mask = VerSetConditionMask(0,    VER_MAJORVERSION,     VER_GREATER_EQUAL);
   mask = VerSetConditionMask(mask, VER_MINORVERSION,     VER_GREATER_EQUAL);
   mask = VerSetConditionMask(mask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
   mask = VerSetConditionMask(mask, VER_SERVICEPACKMINOR, VER_GREATER_EQUAL);

   memset(&osVersion, 0, sizeof osVersion);
   osVersion.dwOSVersionInfoSize = sizeof osVersion;
   osVersion.dwMajorVersion = 6;
   osVersion.dwMinorVersion = 1;

   if (VerifyVersionInfo(&osVersion,
                         VER_MAJORVERSION | VER_MINORVERSION |
                         VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR,
                         mask)) {
      HMODULE slcHandle;

      slcHandle = Win32U_LoadLibrary("slc.dll");
      if (slcHandle) {
         HRESULT (FAR WINAPI *slGetWindowsInformationDWORD)(PCWSTR pwszValueName,
                                                            DWORD *pdwValue);

         slGetWindowsInformationDWORD =
                       (void *)GetProcAddress(slcHandle,
                                              "SLGetWindowsInformationDWORD");
         if (slGetWindowsInformationDWORD) {
            DWORD isAllowed;
            HRESULT res;

/* This define should be in SDK headers, but it is not... */
#ifndef SL_VIRTUALXP_ENABLED
#define SL_VIRTUALXP_ENABLED L"VirtualXP-licensing-Enabled"
#endif
            res = slGetWindowsInformationDWORD(SL_VIRTUALXP_ENABLED,
                                               &isAllowed);
            if (res != S_OK) {
               LOG(0, (LGPFX "Could not detect VirtualXP license: %08X\n",
                       res));
            } else if (isAllowed == 0) {
               LOG(0, (LGPFX "VirtualXP is disabled.\n"));
            } else if (isAllowed == 1) {
               LOG(0, (LGPFX "VirtualXP is enabled.\n"));
               ret = TRUE;
            } else {
               LOG(0, (LGPFX "VirtualXP enablement state is %u.  Enabling.\n",
                       isAllowed));
               ret = TRUE;
            }
         } else {
            LOG(0, (LGPFX "VirtualXP detection cannot find "
                    "SLGetWindowsInformationDWORD function.\n"));
         }
         FreeLibrary(slcHandle);
      } else {
         LOG(0, (LGPFX "VirtualXP detection cannot load slc.dll: %s.\n",
                 Err_ErrString()));
      }
   } else {
      LOG(0, (LGPFX "System does not meet minimum VirtualXP requirements: %s.\n",
              Err_ErrString()));
   }

   return ret;
}
