/*********************************************************
 * Copyright (C) 2008-2010 VMware, Inc. All rights reserved.
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

#ifndef _WIN32U_H_
#define _WIN32U_H_

#pragma push_macro("INITGUID")
#undef INITGUID

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include <windows.h>
#include <wincrypt.h>
#include <aclapi.h>
#include <winspool.h>
#include <userenv.h>
#ifndef __MINGW32__
#include <winscard.h>
#endif
#include "vm_basic_types.h"
#include "unicode.h"
#include "win32uRegistry.h"


/*
 *----------------------------------------------------------------------------
 *
 * WIN32U_CHECK_LONGPATH --
 *
 *    A utility macro to check the long path name.
 *
 *    The windows posix functions do not return the ENAMETOOLONG for path
 *    too long. We need to explicitly check here. Also, some callsites
 *    check Windows error by calling GetLastError(), so, we need to set
 *    the standard Windows error too.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    POSIX errno and Windows system error may be changed.
 *
 *----------------------------------------------------------------------------
 */

#define WIN32U_UNCPREFIX     L"\\\\?\\"
#define WIN32U_UNCPREFIX_LEN 4

#define WIN32U_CHECK_LONGPATH(path, retExpr, exitLabel)                           \
   if (   wcsncmp(path, WIN32U_UNCPREFIX, WIN32U_UNCPREFIX_LEN) != 0              \
       && wcslen(path) > MAX_PATH) {                                              \
      errno = ENAMETOOLONG;                                                       \
      SetLastError(ERROR_FILENAME_EXCED_RANGE);                                   \
      retExpr;                                                                    \
      goto exitLabel;                                                             \
   }


/*
 * The string returned is allocated on the heap and must be freed by the
 * calling routine.
 */

HRESULT Win32U_SHGetFolderPath(HWND hwndOwner,
                               int nFolder,
                               HANDLE hToken,
                               DWORD dwFlags,
                               Unicode *path);
HRESULT
Win32U_SHSetFolderPath(int csidl,
		       HANDLE hToken,
		       DWORD dwFlags,
		       Unicode path);
Unicode
Win32U_PathUnExpandEnvStrings(Unicode path);

PSID Win32U_LookupSidForAccount(ConstUnicode name);

Unicode Win32U_GetModuleFileName(HANDLE hModule);

Unicode Win32U_GetFullPathName(ConstUnicode path,
                               UnicodeIndex *iComponent);

Unicode Win32U_GetCurrentDirectory(void);

Unicode Win32U_GetClassName(HWND hwnd);

int Win32U_GetLogicalDriveStrings(Unicode **driveList);

UINT Win32U_GetDriveType(ConstUnicode driveString);

Unicode Win32U_GetComputerNameEx(int nameType);

HANDLE Win32U_FindFirstChangeNotification(ConstUnicode path,
                                          BOOL watchSubtree,
                                          DWORD notifyFilter);

HANDLE Win32U_FindFirstFileW(ConstUnicode pathName,
                             WIN32_FIND_DATAW *findData);

Unicode
Win32U_GetClipboardFormatName(UINT format);

Unicode
Win32U_DragQueryFile(HANDLE hDrop,
                     UINT iFile);

HMODULE Win32U_LoadLibrary(ConstUnicode pathName);

HMODULE Win32U_LoadLibraryEx(ConstUnicode pathName, HANDLE file, DWORD flags);

HANDLE Win32U_CreateFile(ConstUnicode pathName,
                         DWORD access,
                         DWORD share,
                         SECURITY_ATTRIBUTES *attributes,
                         DWORD disposition,
                         DWORD flags,
                         HANDLE templateFile);

BOOL Win32U_GetVolumeInformation(ConstUnicode pathName,
                                 Unicode *volumeName,
                                 DWORD *volumeSerialNumber,
                                 DWORD *volumeMaximumComponentPath,
                                 DWORD *fileSystemFlags,
                                 Unicode *fileSystemName);

BOOL Win32U_DeleteFile(ConstUnicode lpFileName);

HMODULE Win32U_GetModuleHandle(ConstUnicode lpModuleName);

void Win32U_OutputDebugString(ConstUnicode lpOutputString);

int Win32U_MessageBox(HWND hWnd,
                      ConstUnicode lpText,
                      ConstUnicode lpCaption,
                      UINT uType);

BOOL Win32U_SetWindowText(HWND hWnd,
                          ConstUnicode lpString);

BOOL Win32U_CreateDirectory(ConstUnicode lpPathName,
                            LPSECURITY_ATTRIBUTES lpSecurityAttributes);

BOOL Win32U_RemoveDirectory(ConstUnicode lpPathName);

BOOL Win32U_CopyFile(ConstUnicode lpExistingFileName,
                     ConstUnicode lpNewFileName,
                     BOOL bFailIfExists);

BOOL Win32U_CopyFileEx(ConstUnicode lpExistingFileName,
                       ConstUnicode lpNewFileName,
                       LPPROGRESS_ROUTINE lpProgressRoutine,
                       LPVOID lpData,
                       LPBOOL phCancel,
                       DWORD dwCopyFlags);

BOOL Win32U_MoveFileEx(ConstUnicode lpExistingFileName,
                       ConstUnicode lpNewFileName,
                       DWORD dwFlags);

DWORD Win32U_GetFileAttributes(ConstUnicode lpFileName);

SC_HANDLE Win32U_OpenSCManager(ConstUnicode lpMachineName,
                               ConstUnicode lpDatabaseName,
                               DWORD dwDesiredAccess);

HANDLE Win32U_CreateFileMapping(HANDLE hFile,
                                LPSECURITY_ATTRIBUTES lpAttributes,
                                DWORD flProtect,
                                DWORD dwMaximumSizeHigh,
                                DWORD dwMaximumSizeLow,
                                ConstUnicode lpName);

BOOL Win32U_SetFileAttributes(ConstUnicode lpFileName,
                              DWORD dwFileAttributes);

SC_HANDLE Win32U_OpenService(SC_HANDLE hSCManager,
                             ConstUnicode lpServiceName,
                             DWORD dwDesiredAccess);

BOOL Win32U_CryptAcquireContext(HCRYPTPROV* phProv,
                                ConstUnicode pszContainer,
                                ConstUnicode pszProvider,
                                DWORD dwProvType,
                                DWORD dwFlags);

BOOL Win32U_GetDiskFreeSpace(ConstUnicode lpRootPathName,
                             LPDWORD lpSectorsPerCluster,
                             LPDWORD lpBytesPerSector,
                             LPDWORD lpNumberOfFreeClusters,
                             LPDWORD lpTotalNumberOfClusters);

BOOL Win32U_GetDiskFreeSpaceEx(ConstUnicode lpDirectoryName,
                               PULARGE_INTEGER lpFreeBytesAvailable,
                               PULARGE_INTEGER lpTotalNumberoOfBytes,
                               PULARGE_INTEGER lpTotalNumberofFreeBytes);

BOOL Win32U_SetCurrentDirectory(ConstUnicode lpPathName);

HCURSOR Win32U_LoadCursor(HINSTANCE hInstance,
                          ConstUnicode lpCursorName);

HRSRC Win32U_FindResource(HMODULE hModule,
                          ConstUnicode lpName,
                          ConstUnicode lpType);

BOOL Win32U_GetFileSecurity(ConstUnicode lpFileName,
                            SECURITY_INFORMATION RequestedInformation,
                            PSECURITY_DESCRIPTOR pSecurityDescriptor,
                            DWORD nLength,
                            LPDWORD lpnLengthNeeded);

HWND Win32U_CreateWindowEx(DWORD dwExStyle,
                           ConstUnicode lpClassName,
                           ConstUnicode lpWindowName,
                           DWORD dwStyle,
                           int x,
                           int y,
                           int nWidth,
                           int nHeight,
                           HWND hWndParent,
                           HMENU hMenu,
                           HINSTANCE hInstance,
                           LPVOID lpParam);

HANDLE Win32U_BeginUpdateResource(ConstUnicode lpFileName,
                                  BOOL bDeleteExistingResources);

BOOL Win32U_UpdateResource(HANDLE hUpdate,
                           ConstUnicode lpType,
                           ConstUnicode lpName,
                           WORD wLanguage,
                           LPVOID lpData,
                           DWORD cbData);

BOOL Win32U_GetVolumeInformation(ConstUnicode pathName,
                                 Unicode *volumeName,
                                 DWORD *volumeSerialNumber,
                                 DWORD *volumeMaximumComponentPath,
                                 DWORD *fileSystemFlags,
                                 Unicode *fileSystemName);

DWORD Win32U_QueryDosDevice(ConstUnicode lpDeviceName,
                            LPSTR lpTargetPath,
                            DWORD ucchMax);

DWORD Win32U_GetTempPath(DWORD nBufferLength, LPSTR lpBuffer);

DWORD Win32U_GetEnvironmentVariable(ConstUnicode lpName,
                                    LPSTR lpBuffer,
                                    DWORD nSize);

DWORD Win32U_ExpandEnvironmentStrings(ConstUnicode lpSrc,
                                      LPSTR lpDst,
                                      DWORD nSize);

DWORD Win32U_SetEnvironmentVariable(ConstUnicode lpName,
                                    ConstUnicode lpValue);

UINT Win32U_GetSystemDirectory(LPSTR lpBuffer, UINT uSize);
BOOL Win32U_GetUserName(LPSTR lpBuffer, LPDWORD lpnSize);

int Win32U_GetTimeFormat(LCID Locale,
                         DWORD dwFlags,
                         const SYSTEMTIME *lpTime,
                         ConstUnicode lpFormat,
                         LPSTR lpTimeStr,
                         int cchTime);

int Win32U_GetDateFormat(LCID Locale,
                         DWORD dwFlags,
                         const SYSTEMTIME *lpTime,
                         ConstUnicode lpFormat,
                         LPSTR lpDateStr,
                         int cchData);

Unicode Win32U_GetComputerName(void);

#ifdef __MINGW32__
static
#endif
DWORD Win32U_CertGetNameString(PCCERT_CONTEXT pCertContext,
                               DWORD dwType,
                               DWORD dwFlags,
                               void *pvTypePara,
                               LPSTR pszNameString,
                               DWORD cchNameString);

#ifdef __MINGW32__
static
#endif
DWORD Win32U_WNetGetLastError(LPDWORD lpError,
                              LPSTR lpErrorBuf,
                              DWORD nErrorBufSize,
                              LPSTR lpNameBuf,
                              DWORD nNameBufSize);

Unicode Win32U_FormatMessage(DWORD dwFlags,
                             LPCVOID lpSource,
                             DWORD dwMessageId,
                             DWORD dwLanguageId,
                             va_list *Arguments);

void Win32U_LoadString(HINSTANCE hInstance,
                       UINT uID,
                       LPSTR lpBuffer,
                       int nBufferMax);

Unicode Win32U_AllocString(HINSTANCE hInstance,
                           UINT uID);

BOOL Win32U_GetVolumePathName(ConstUnicode lpszFileName,
                              LPSTR lpszVolumePathName,
                              DWORD cchBufferLength);

BOOL Win32U_GetVolumeNameForVolumeMountPoint(
   ConstUnicode lpszVolumeMountPoint,
   LPSTR lpszVolumeName,
   DWORD cchBufferLength);

DWORD Win32U_GetLongPathName(ConstUnicode lpszShortPath,
                             LPSTR lpszLongPath,
                             DWORD cchBuffer);

BOOL Win32U_GetVersionEx(LPOSVERSIONINFOA lpVersionInfo);

BOOL Win32U_CreateProcess(ConstUnicode lpApplicationName,
                          ConstUnicode lpCommandLine,
                          LPSECURITY_ATTRIBUTES lpProcessAttributes,
                          LPSECURITY_ATTRIBUTES lpThreadAttributes,
                          BOOL bInheritHandles,
                          DWORD dwCreationFlags,
                          LPVOID lpEnvironment,
                          ConstUnicode lpCurrentDirectory,
                          LPSTARTUPINFOA lpStartupInfo,
                          LPPROCESS_INFORMATION lpProcessInformation);

BOOL Win32U_LookupAccountSid(ConstUnicode lpSystemName,
                             PSID lpSid,
                             Unicode lpName,
                             LPDWORD cchName,
                             Unicode lpReferencedDomainName,
                             LPDWORD cchReferencedDomainName,
                             PSID_NAME_USE peUse);

HANDLE Win32U_CreateNamedPipe(ConstUnicode lpName,
                              DWORD dwOpenMode,
                              DWORD dwPipeMode,
                              DWORD nMaxInstances,
                              DWORD nOutBufferSize,
                              DWORD nInBufferSize,
                              DWORD nDefaultTimeOut,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes);

BOOL Win32U_WaitNamedPipe(ConstUnicode lpName,
                          DWORD nTimeOut);

HANDLE Win32U_CreateMutex(LPSECURITY_ATTRIBUTES lpMutexAttributes,
                          BOOL bInitialOwner,
                          ConstUnicode lpName);

HANDLE Win32U_CreateSemaphore(LPSECURITY_ATTRIBUTES lpMutexAttributes,
                              LONG lInitialCount,
                              LONG lMaximumCount,
                              ConstUnicode lpName);

HANDLE Win32U_OpenSemaphore(DWORD dwDesiredAccess,
                            BOOL bInheritHandle,
                            ConstUnicode lpName);

HANDLE Win32U_OpenEvent(DWORD dwDesiredAccess,
                        BOOL bInheritHandle,
                        ConstUnicode lpName);

BOOL Win32U_ReportEvent(HANDLE hEventLog,
                        WORD wType,
                        WORD wCategory,
                        DWORD dwEventID,
                        PSID lpUserSid,
                        WORD wNumStrings,
                        DWORD dwDataSize,
                        ConstUnicode *lpStrings,
                        LPVOID lpRawData);

SC_HANDLE Win32U_CreateService(SC_HANDLE hSCManager,
                               ConstUnicode lpServiceName,
                               ConstUnicode lpDisplayName,
                               DWORD dwDesiredAccess,
                               DWORD dwServiceType,
                               DWORD dwStartType,
                               DWORD dwErrorControl,
                               ConstUnicode lpBinaryPathName,
                               ConstUnicode lpLoadOrderGroup,
                               LPDWORD lpdwTagId,
                               ConstUnicode lpDependencies,
                               ConstUnicode lpServiceStartName,
                               ConstUnicode lpPassword);


/*
 *----------------------------------------------------------------------------
 *
 * Win32UCertGetNameStringInt --
 *
 *    Dynamic-size wrapper around CertGetNameString. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE Unicode
Win32UCertGetNameStringInt(PCCERT_CONTEXT pCertContext, // IN
                           DWORD dwType,                // IN
                           DWORD dwFlags,               // IN
                           void *pvTypePara)            // IN
{
   Unicode nameString = NULL;
   utf16_t *nameStringW = NULL;
   DWORD size = 0;
   DWORD ret;

   while (TRUE) {
      nameStringW = (size > 0) ? (utf16_t *)
         Util_SafeMalloc(size * sizeof(utf16_t)) : NULL;
      ret = CertGetNameStringW(pCertContext, dwType, dwFlags, pvTypePara,
                               nameStringW, size);

      if (ret > size) {
         size = ret;
         free(nameStringW);
      } else {
         break;
      }
   }

   nameString = Unicode_AllocWithUTF16(nameStringW);
   free(nameStringW);

   return nameString;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_CertGetNameString --
 *
 *    Fixed-size wrapper around CertGetNameString. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE DWORD
Win32U_CertGetNameString(PCCERT_CONTEXT pCertContext, // IN
                         DWORD dwType,                // IN
                         DWORD dwFlags,               // IN
                         void *pvTypePara,            // IN
                         LPSTR pszNameString,         // OUT
                         DWORD cchNameString)         // IN
{
   Unicode nameString = Win32UCertGetNameStringInt(pCertContext, dwType,
                                                   dwFlags, pvTypePara);
   size_t retLen;

   if (NULL == nameString) {
      return 0;
   } else if ((NULL == pszNameString) || (0 == cchNameString)) {
      retLen = strlen(UTF8(nameString)) + 1;
   } else {
      Bool noTrunc = Unicode_CopyBytes(pszNameString, nameString,
                                       cchNameString, &retLen,
                                       STRING_ENCODING_UTF8);
      ASSERT_NOT_IMPLEMENTED(noTrunc);
      retLen++; /* include NUL */
   }

   Unicode_Free(nameString);
   return retLen;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_WNetGetLastError --
 *
 *    Wrapper around WNetGetLastError. See MSDN.
 *
 * Results:
 *    See MSDN.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE DWORD
Win32U_WNetGetLastError(LPDWORD lpError,     // IN
                        LPSTR lpErrorBuf,    // OUT
                        DWORD nErrorBufSize, // IN
                        LPSTR lpNameBuf,     // OUT
                        DWORD nNameBufSize)  // IN
{
   utf16_t *errorBufW = NULL;
   utf16_t *nameBufW = NULL;
   Unicode errorBuf = NULL;
   Unicode nameBuf = NULL;
   DWORD ret;

   errorBufW = (utf16_t *) Util_SafeMalloc(nErrorBufSize * sizeof(utf16_t));
   nameBufW = (utf16_t *) Util_SafeMalloc(nNameBufSize * sizeof(utf16_t));

   ret = WNetGetLastErrorW(lpError, errorBufW, nErrorBufSize, nameBufW,
                           nNameBufSize);

   if (NO_ERROR != ret) {
      goto exit;
   }

   errorBuf = Unicode_AllocWithUTF16(errorBufW);
   nameBuf = Unicode_AllocWithUTF16(nameBufW);

   /*
    * Don't report truncation to user because the API doesn't either.
    */
   Unicode_CopyBytes(lpErrorBuf, errorBuf, nErrorBufSize, NULL,
                     STRING_ENCODING_UTF8);
   Unicode_CopyBytes(lpNameBuf, lpNameBuf, nNameBufSize, NULL,
                     STRING_ENCODING_UTF8);

  exit:
   free(errorBufW);
   free(nameBufW);
   Unicode_Free(errorBuf);
   Unicode_Free(nameBuf);

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_LoadUserProfile --
 *
 *    Wrapper around LoadUserProfile. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE BOOL
Win32U_LoadUserProfile(HANDLE hToken,                 // IN
                       LPPROFILEINFOA lpProfileInfo)  // IN/OUT
{
   BOOL retval;
   PROFILEINFOW profInfoW;

   ASSERT(lpProfileInfo);

   if (lpProfileInfo->dwSize != sizeof(PROFILEINFOA) ||
       lpProfileInfo->lpUserName == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }

   profInfoW.dwSize = sizeof profInfoW;
   profInfoW.dwFlags = lpProfileInfo->dwFlags;
   profInfoW.lpUserName = Unicode_GetAllocUTF16(lpProfileInfo->lpUserName);
   profInfoW.lpProfilePath = Unicode_GetAllocUTF16(lpProfileInfo->lpProfilePath);
   profInfoW.lpDefaultPath = Unicode_GetAllocUTF16(lpProfileInfo->lpDefaultPath);
   profInfoW.lpServerName = Unicode_GetAllocUTF16(lpProfileInfo->lpServerName);
   profInfoW.lpPolicyPath = Unicode_GetAllocUTF16(lpProfileInfo->lpPolicyPath);
   profInfoW.hProfile = lpProfileInfo->hProfile;

   retval = LoadUserProfileW(hToken, &profInfoW);

   if (retval) {
     lpProfileInfo->hProfile = profInfoW.hProfile;
   }
   free(profInfoW.lpUserName);
   free(profInfoW.lpProfilePath);
   free(profInfoW.lpDefaultPath);
   free(profInfoW.lpServerName);
   free(profInfoW.lpPolicyPath);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_OpenPrinter --
 *
 *    Wrapper around OpenPrinter. See MSDN.
 *
 * Results:
 *    Returns TRUE on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE BOOL
Win32U_OpenPrinter(Unicode pPrinterName,          // IN
                   LPHANDLE phPrinter,            // OUT
                   LPPRINTER_DEFAULTSA pDefault)  // IN
{
   BOOL retval;
   utf16_t *pPrinterNameW;
   PRINTER_DEFAULTSW pDefaultW;
   LPPRINTER_DEFAULTSW lppDefaultW;

   ASSERT(pPrinterName);

   pPrinterNameW = Unicode_GetAllocUTF16(pPrinterName);
   if (pDefault) {
      pDefaultW.pDatatype = Unicode_GetAllocUTF16(pDefault->pDatatype);

      /*
       * TODO: Currently no call site supplies non-null pDevMode. If this
       *       changes, we need to convert struct pointed by pDevMode to a
       *       DEVMODEW struct.
       */
      ASSERT_NOT_IMPLEMENTED(pDefault->pDevMode == NULL);
      pDefaultW.pDevMode = NULL;
      pDefaultW.DesiredAccess = pDefault->DesiredAccess;
      lppDefaultW = &pDefaultW;
   } else {
      lppDefaultW = NULL;
   }

   retval = OpenPrinterW(pPrinterNameW, phPrinter, lppDefaultW);

   free(pPrinterNameW);
   if (pDefault) {
      free(pDefaultW.pDatatype);
   }

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SetEntriesInAcl --
 *
 *    Wrapper around SetEntriesInAcl. See MSDN.
 *
 * Results:
 *    Returns ERROR_SUCCESS on success, or a nonzero error code on failure.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE DWORD
Win32U_SetEntriesInAcl(ULONG cCountOfExplicitEntries,             // IN
                       PEXPLICIT_ACCESSA pListOfExplicitEntries,  // IN
                       PACL OldAcl,                               // IN
                       PACL* NewAcl)                              // OUT
{
   DWORD retval;
   PEXPLICIT_ACCESS_W pListW;
   ULONG i;

   pListW = (PEXPLICIT_ACCESS_W)Util_SafeCalloc(cCountOfExplicitEntries,
                                                sizeof *pListW);
   for (i = 0; i < cCountOfExplicitEntries; i++) {
      pListW[i].grfAccessPermissions = pListOfExplicitEntries[i].grfAccessPermissions;
      pListW[i].grfAccessMode = pListOfExplicitEntries[i].grfAccessMode;
      pListW[i].grfInheritance = pListOfExplicitEntries[i].grfInheritance;
      /* According to MSDN, pMultipleTrustee can only be NULL at this point */
      ASSERT_NOT_IMPLEMENTED(pListOfExplicitEntries[i].Trustee.pMultipleTrustee == NULL);
      pListW[i].Trustee.pMultipleTrustee = NULL;
      pListW[i].Trustee.MultipleTrusteeOperation =
         pListOfExplicitEntries[i].Trustee.MultipleTrusteeOperation;
      pListW[i].Trustee.TrusteeForm = pListOfExplicitEntries[i].Trustee.TrusteeForm;
      pListW[i].Trustee.TrusteeType = pListOfExplicitEntries[i].Trustee.TrusteeType;
      if (pListOfExplicitEntries[i].Trustee.TrusteeForm == TRUSTEE_IS_NAME) {
         pListW[i].Trustee.ptstrName = 
            Unicode_GetAllocUTF16(pListOfExplicitEntries[i].Trustee.ptstrName);
      } else {
         pListW[i].Trustee.ptstrName =
            (LPWSTR)(pListOfExplicitEntries[i].Trustee.ptstrName);
      }

      /*
       * TODO: Currently no call site uses TRUSTEE of the form OBJECTS_AND_NAME.
       *       If this changes, we need to properly convert the struct.
       */
      ASSERT_NOT_IMPLEMENTED(pListOfExplicitEntries[i].Trustee.TrusteeForm !=
                             TRUSTEE_IS_OBJECTS_AND_NAME);
   }

   retval = SetEntriesInAclW(cCountOfExplicitEntries, pListW, OldAcl, NewAcl);

   for (i = 0; i < cCountOfExplicitEntries; i++) {
      if (pListOfExplicitEntries[i].Trustee.TrusteeForm == TRUSTEE_IS_NAME) {
         free(pListW[i].Trustee.ptstrName);
      }
   }
   free(pListW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_WNetAddConnection2 --
 *
 *    Wrapper around WNetAddConnection2. See MSDN.
 *
 * Results:
 *    Returns NO_ERROR on success, or a system error code.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE DWORD
Win32U_WNetAddConnection2(LPNETRESOURCEA lpNetResource,  // IN
                          ConstUnicode lpPassword,       // IN
                          ConstUnicode lpUsername,       // IN
                          DWORD dwFlags)                 // IN
{
   DWORD retval;
   NETRESOURCEW netResourceW;
   utf16_t *lpPasswordW;
   utf16_t *lpUsernameW;

   ASSERT(lpNetResource);

   /* WNetAddConnection2 only cares about 4 members in NETRESOURCE */
   netResourceW.dwType = lpNetResource->dwType;
   netResourceW.lpLocalName = Unicode_GetAllocUTF16(lpNetResource->lpLocalName);
   netResourceW.lpRemoteName = Unicode_GetAllocUTF16(lpNetResource->lpRemoteName);
   netResourceW.lpProvider = Unicode_GetAllocUTF16(lpNetResource->lpProvider);
   /* Copy the other non-string members just to be safe */
   netResourceW.dwScope = lpNetResource->dwScope;
   netResourceW.dwDisplayType = lpNetResource->dwDisplayType;
   netResourceW.dwUsage = lpNetResource->dwUsage;
   netResourceW.lpComment = NULL;

   lpPasswordW = Unicode_GetAllocUTF16(lpPassword);
   lpUsernameW = Unicode_GetAllocUTF16(lpUsername);

   retval = WNetAddConnection2W(&netResourceW, lpPasswordW, lpUsernameW,
                                dwFlags);

   free(netResourceW.lpLocalName);
   free(netResourceW.lpRemoteName);
   free(netResourceW.lpProvider);
   free(lpPasswordW);
   free(lpUsernameW);

   return retval;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SCardConnect --
 *
 *    Wrapper around SCardConnect. See MSDN.
 *
 * Results:
 *    Returns SCARD_S_SUCCESS on success.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
#ifndef __MINGW32__
static INLINE LONG
Win32U_SCardConnect(SCARDCONTEXT hContext,             // IN
                    ConstUnicode szReader,             // IN
                    DWORD dwShareMode,                 // IN
                    DWORD dwPreferredProtocols,        // IN
                    LPSCARDHANDLE phCard,              // OUT
                    LPDWORD pdwActiveProtocol)         // OUT
{
   BOOL retval;
   utf16_t *szReaderW;

   ASSERT(szReader);

   szReaderW = Unicode_GetAllocUTF16(szReader);

   retval = SCardConnectW(hContext,
                          szReaderW,
                          dwShareMode,
                          dwPreferredProtocols,
                          phCard,
                          pdwActiveProtocol);

   free(szReaderW);

   return retval;
}
#endif


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_SetDefaultPrinter --
 *
 *    Wrapper around SetDefaultPrinter. See MSDN.
 *    This function is defined as INLINE to avoid requiring all apps to link
 *    against winspool.lib.
 *
 * Results:
 *    Nonzero value on success.
 *    0 on failure.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE BOOL
Win32U_SetDefaultPrinter(ConstUnicode pszPrinter)  // IN
{
   BOOL res;
   
   utf16_t *pszPrinterW = UNICODE_GET_UTF16(pszPrinter);
   res = SetDefaultPrinterW(pszPrinterW);
   UNICODE_RELEASE_UTF16(pszPrinterW);

   return res;
}


/*
 * The placement of mmsystem.h in the list of Windows header files matters.
 * In order not to require win32u.h to be included in a particular order
 * relative to the other Windows header files, I just replicated the relevant
 * declarations in mmsystem.h here.  The macros are renamed with a WIN32U_
 * prefix to avoid redefintion errors.
 */

#define WIN32U_MAXERRORLENGTH 256
#define WIN32U_MMSYSERR_NOERROR 0
typedef UINT MMRESULT;
#ifndef __MINGW32__
DECLSPEC_IMPORT MMRESULT WINAPI waveInGetErrorTextW(__in MMRESULT mmrError,
                                                    __out_ecount(cchText) LPWSTR pszText,
                                                    __in UINT cchText);
DECLSPEC_IMPORT MMRESULT WINAPI waveOutGetErrorTextW(__in MMRESULT mmrError,
                                                     __out_ecount(cchText) LPWSTR pszText,
                                                     __in UINT cchText);
#endif

/*
 *----------------------------------------------------------------------------
 *
 * Win32U_WaveInGetErrorText --
 *
 *    Wrapper around waveInGetErrorText. See MSDN.
 *
 * Results:
 *    Returns MMSYSERR_NOERROR if successful or an error otherwise.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE MMRESULT
Win32U_WaveInGetErrorText(MMRESULT mmrError, // IN
                          Unicode pszText,   // OUT
                          UINT cchText)      // IN
{
   MMRESULT ret;
   utf16_t errorTextW[WIN32U_MAXERRORLENGTH];
   Unicode errorText = NULL;

   if (cchText == 0) {
      return 0;
   }
   ASSERT(pszText);

   ret = waveInGetErrorTextW(mmrError, errorTextW, ARRAYSIZE(errorTextW));

   if (ret == WIN32U_MMSYSERR_NOERROR) {
      errorText = Unicode_AllocWithUTF16(errorTextW);

      /* Truncation is not reported. See MSDN. */
      Unicode_CopyBytes(pszText, errorText, cchText, NULL, STRING_ENCODING_UTF8);
      free(errorText);
   }

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * Win32U_WaveOutGetErrorText --
 *
 *    Wrapper around waveOutGetErrorText. See MSDN.
 *
 * Results:
 *    Returns MMSYSERR_NOERROR if successful or an error otherwise.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

static INLINE MMRESULT
Win32U_WaveOutGetErrorText(MMRESULT mmrError, // IN
                           Unicode pszText,   // OUT
                           UINT cchText)      // IN
{
   MMRESULT ret;
   utf16_t errorTextW[WIN32U_MAXERRORLENGTH];
   Unicode errorText = NULL;

   if (cchText == 0) {
      return 0;
   }
   ASSERT(pszText);

   ret = waveOutGetErrorTextW(mmrError, errorTextW, ARRAYSIZE(errorTextW));

   if (ret == WIN32U_MMSYSERR_NOERROR) {
      errorText = Unicode_AllocWithUTF16(errorTextW);

      /* Truncation is not reported. See MSDN. */
      Unicode_CopyBytes(pszText, errorText, cchText, NULL, STRING_ENCODING_UTF8);
      free(errorText);
   }

   return ret;
}

#ifndef __MINGW32__
LSTATUS
Win32U_SHCopyKey(HKEY srcKey,         // IN
                 ConstUnicode subKey, // IN
                 HKEY dstKey,         // IN
                 DWORD reserved);     // IN, reserved

LSTATUS
Win32U_SHDeleteKey(HKEY key,             // IN
                   ConstUnicode subKey); // IN
#endif

#pragma pop_macro("INITGUID")

#endif // _WIN32U_H_
