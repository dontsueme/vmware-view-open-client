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
 * fileWin32.c --
 *
 *      Interface to host-specific file functions for win32 hosts
 *
 */

#include "safetime.h"
#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <string.h>
#include <shlobj.h>

#include "vmware.h"
#include "vm_ctype.h"
#include "log.h"
#include "util.h"
#include "str.h"
#include "localconfig.h"
#include "file.h"
#include "fileInt.h"
#include "fileIO.h"
#include "dynbuf.h"
#include "win32u.h"

#include "unicodeOperations.h"

#define LOGLEVEL_MODULE main
#include "loglevel_user.h"

#define S_IWUSR   00200

/*
 * Typedef needed for File_GetTmpDir;
 */

typedef DWORD (WINAPI *GETTEMPPATHW)(DWORD, LPWSTR);
typedef int   (WINAPI *WCTOMB)(UINT, DWORD, LPCWSTR, int, LPSTR, int,
                               LPCSTR, LPBOOL);


/*
 *-----------------------------------------------------------------------------
 *
 * FileGetMountPoint --
 *
 *      Returns an allocated string which identifies the underlying filesystem
 *      for a given file (which need not exist). This is a wrapper around
 *      GetVolumePathName.
 *
 * Results:
 *      NULL on failure, the pathname otherwise (which must be freed by the
 *      caller).
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static Unicode
FileGetMountPoint(ConstUnicode pathName)  // IN:
{
   const utf16_t *path;
   Unicode result = NULL;
   utf16_t volume[MAX_PATH];

   path = UNICODE_GET_UTF16(pathName);
   if (GetVolumePathNameW(path, volume, MAX_PATH)) {
      result = Unicode_AllocWithUTF16(volume);
   }
   UNICODE_RELEASE_UTF16(path);

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * File_UnlinkDelayed --
 *
 *    Uses MoveFileEx to queue a file or directory for deletion upon reboot,
 *    or, on a Win9X system, uses the more complicated wininit.ini system.
 *    
 *    Much of this function was adapted from ReplaceFileOnReboot written by
 *    Jeffrey Richter. Code for this function is available from:
 *    http://www.microsoft.com/msj/archive/sf9ca.htm#fig1
 *
 * Results:
 *    Zero if queuing of the file was successful, non-zero otherwise.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

int
File_UnlinkDelayed(ConstUnicode fileName)  // IN:
{
   static const char winInitBase[] = "WININIT.INI";
   static const char renameHeader[] = "[Rename]\r\n";

   char *winInitFileName, *fileNameEntry, *winInitFileMap, *renameHeaderPos;
   char *fullPath, windowsDir[MAX_PATH];
   int deleteSuccess;
   HANDLE winInitFile, winInitFileMapping;
   DWORD res;
   LARGE_INTEGER renameLinePos;
   LARGE_INTEGER winInitFileCurSize;
   LARGE_INTEGER winInitFileMaxSize;
   utf16_t *fileNameW = Unicode_GetAllocUTF16(fileName);

   fileNameEntry = NULL;
   fullPath = NULL;
   winInitFileName = NULL;
   winInitFileMap = NULL;
   winInitFile = INVALID_HANDLE_VALUE;
   winInitFileMapping = INVALID_HANDLE_VALUE;
   winInitFileCurSize.QuadPart = INVALID_FILE_SIZE;
   deleteSuccess = -1;

   /* XXX:
      If the fileName is a directory, the MOVEFILE_DELAY_UNTIL_REBOOT option
      requires that the directory be empty (has no more files in it).
    */
   /* Try MoveFileEx (supported on NT or higher). */
   res = MoveFileExW(fileNameW, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);

   if (res == 0 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {

      /* 
       * Win9x guests fall in here. See MSDN reference on MoveFileEx to see
       * what must be done here.
       */
      
      /* Create WININIT.INI filename. */
      if (GetWindowsDirectoryA(windowsDir, sizeof windowsDir) == 0) {
         Log(LGPFX" %s: GetWindowsDirectory failed, code %d\n", __FUNCTION__,
             GetLastError());
         goto out;
      }
      winInitFileName = Str_SafeAsprintf(NULL, "%s%s%s", 
                                         windowsDir, DIRSEPS, winInitBase);

      /* Create filename to be deleted. */
      fullPath = File_FullPath(fileName);
      if (fullPath == NULL) {
         Log(LGPFX" %s: File_Fullpath failed\n", __FUNCTION__);
         goto out;
      }
      fileNameEntry = Str_SafeAsprintf(NULL, "NUL=%s\r\n", fullPath);

      /* Open or create WININIT.INI. */
      winInitFile = CreateFileA(winInitFileName, GENERIC_READ | GENERIC_WRITE, 
                               0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | 
                               FILE_FLAG_SEQUENTIAL_SCAN, NULL);
      if (winInitFile == INVALID_HANDLE_VALUE) {
         Log(LGPFX" %s: CreateFile on %s failed, code %d\n", __FUNCTION__,
             winInitFileName, GetLastError());
         goto out;
      }

      /* 
       * Create a file mapping object sized to WININIT.INI, the filename
       * entry, and the rename header (since we may need to add it).
       */

      winInitFileCurSize.LowPart = GetFileSize(winInitFile, 
                                               &winInitFileCurSize.HighPart);
      if (winInitFileCurSize.LowPart == INVALID_FILE_SIZE &&
          GetLastError() != NO_ERROR) {
         // Can't assume GetFileSize didn't corrupt HighPart on error case.
         winInitFileCurSize.QuadPart = INVALID_FILE_SIZE;
         Log(LGPFX" %s: GetFileSize on %s failed, code %d\n", __FUNCTION__,
             winInitFileName, GetLastError());
         goto out;
      }
      winInitFileMaxSize.QuadPart = winInitFileCurSize.QuadPart + 
         strlen(fileNameEntry) + strlen(renameHeader);
      winInitFileMapping = CreateFileMapping(winInitFile, NULL, 
                                             PAGE_READWRITE,
                                             winInitFileMaxSize.HighPart,
                                             winInitFileMaxSize.LowPart, 
                                             NULL);
      if (winInitFileMapping == INVALID_HANDLE_VALUE) {
         Log(LGPFX" %s: CreateFileMapping failed, code %d\n", __FUNCTION__,
             GetLastError());
         goto out;
      }
      
      /* Map the WININIT.INI map into memory. */
      winInitFileMap = (char *) MapViewOfFile(winInitFileMapping, 
                                              FILE_MAP_WRITE, 0, 0, 0);
      if (winInitFileMap == NULL) {
         Log(LGPFX" %s: MapViewOfFile failed, code %d\n", __FUNCTION__,
             GetLastError());
         goto out;
      }

      /* Find rename header in mapped file. */
      renameHeaderPos = strstr(winInitFileMap, renameHeader);
      if (renameHeaderPos == NULL) {

         /* Not found. Add it. */
         winInitFileCurSize.QuadPart += 
            Str_Sprintf(&winInitFileMap[winInitFileCurSize.QuadPart],
                        winInitFileMaxSize.QuadPart, "%s", renameHeader);
         renameLinePos.QuadPart = winInitFileCurSize.QuadPart;
      } else {
         /* 
          * Found. Shift all lines under it down by size of the new
          * line we're adding.
          */

         char *insertionPos = Str_Strchr(renameHeaderPos, '\n');

         insertionPos++;
         memmove(insertionPos + strlen(fileNameEntry), insertionPos, 
                 winInitFileMap + winInitFileCurSize.QuadPart - insertionPos);
         renameLinePos.QuadPart = insertionPos - winInitFileMap;
      }
      
      /* Add the filename entry. */
      memcpy(&winInitFileMap[renameLinePos.QuadPart], fileNameEntry, 
             strlen(fileNameEntry));
      winInitFileCurSize.QuadPart += strlen(fileNameEntry);

      deleteSuccess = 0;
   } else if (res == 0) {
      /* General MoveFileEx failure. */
      Log(LGPFX" %s: MoveFileExW failed on %s, code %d\n", __FUNCTION__,
          Unicode_GetUTF8(fileName), GetLastError());
   } else {
      /* Success on MoveFileEx */
      deleteSuccess = 0;
   }
   
  out:
   free(fileNameW);
   free(winInitFileName);
   free(fileNameEntry);
   free(fullPath);

   if (winInitFileMap != NULL) {
      UnmapViewOfFile(winInitFileMap);
   }

   if (winInitFileMapping != INVALID_HANDLE_VALUE) {
      CloseHandle(winInitFileMapping);
   }

   if (winInitFile != INVALID_HANDLE_VALUE) {
      /* 
       * This actually updates the file. Must happen after the mapping
       * handle is closed.
       */

      if (winInitFileCurSize.QuadPart != INVALID_FILE_SIZE) {
         SetFilePointer(winInitFile, winInitFileCurSize.LowPart, 
                        &winInitFileCurSize.HighPart, FILE_BEGIN);
      }
      SetEndOfFile(winInitFile);
      CloseHandle(winInitFile);
   }

   return deleteSuccess;
}


/*
 *----------------------------------------------------------------------
 *
 * File_IsRemote --
 *
 *      Return TRUE if this file is stored on a remote file system.
 *
 * Results:
 *      TRUE	On a remote file system
 *      FALSE	Not on a remove file system or an error occured
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
File_IsRemote(ConstUnicode pathName)  // IN: File name
{
   Bool ret;
   Unicode temp;
   const utf16_t *path;
   UINT driveType;
   UnicodeIndex index;

   Unicode fullPath = NULL;

   if (pathName == NULL) {
      ret = FALSE;
      goto end;
   }

   /* Physical drives succeed File_FullPath, filter out here */
   if (Unicode_CompareRange(pathName, 0, -1,
                            "\\\\.\\PhysicalDrive", 0, -1, TRUE) == 0) {
      ret = FALSE;
      goto end;
   }

   /* Insure that pathName is a full path */
   fullPath = File_FullPath(pathName);

   if (fullPath == NULL) {
      ret = FALSE;
      goto end;
   }

   /* Check for UNC paths (effectively remote) */
   if (Unicode_StartsWith(fullPath, "\\\\")) {
      ret = TRUE;
      goto end;
   }

   /* Attempt to find the first backslash */
   index = Unicode_FindSubstrInRange(fullPath, 0, -1,
                                     DIRSEPS, 0, 1);

   if (index == UNICODE_INDEX_NOT_FOUND) {
      Warning(LGPFX" %s: No backslash in file %s\n", __FUNCTION__,
              UTF8(fullPath));
      ret = FALSE;
      goto end;
   }

   temp = Unicode_Substr(fullPath, 0, index);

   path = UNICODE_GET_UTF16(temp);
   driveType = GetDriveTypeW(path);

   UNICODE_RELEASE_UTF16(path);
   Unicode_Free(temp);

   ret = (driveType == DRIVE_REMOTE) ? TRUE : FALSE;

end:
   Unicode_Free(fullPath);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * File_IsSymLink --
 *
 *      Check if the specified file is a symbolic link or not
 *
 * Results:
 *      Bool - TRUE -> is a symlink, FALSE -> not a symlink or error
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Bool
File_IsSymLink(ConstUnicode pathName)  // IN:
{
   return FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * File_FullPath --
 *
 *      Compute the full path of a file. If the file if NULL or "", the
 *      current directory is returned.
 *
 * Results:
 *      NULL if error (reported to the user).
 *
 * Side effects:
 *      The result is allocated.
 *
 *----------------------------------------------------------------------
 */

Unicode
File_FullPath(ConstUnicode pathName)  // IN:
{
   const utf16_t *path;
   Unicode result;
   utf16_t *answer;

   path = pathName ? UNICODE_GET_UTF16(pathName) : NULL;

   /* _wfullpath handles the NULL or "" argument */
   answer = _wfullpath(NULL, path, 0);

   UNICODE_RELEASE_UTF16(path);

   if (answer == NULL) {
      Warning(LGPFX" %s:  _wfullpath failed on (%s): %d\n", __FUNCTION__,
              pathName ? UTF8(pathName) : "", GetLastError());

      return NULL;
   }

   result = Unicode_AllocWithUTF16(answer);

   free(answer);

   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * FileDriveNumber
 *
 *      DRIVE is either NULL (current drive) or a string starting with
 *      [A-Za-z].
 *
 * Results:
 *      Drive number (0 for current drive, 1-26 for drives A-Z) or -1 if
 *      the drive is invalid.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int
FileDriveNumber(ConstUnicode drive)  // IN:
{
   int driveNum;

   if ((drive == NULL) || Unicode_IsEmpty(drive)) {
      driveNum = 0;
   } else {
      UnicodeIndex index;

      index = Unicode_FindSubstrInRange("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                                        0, -1, drive, 0, 1);

      if (index == UNICODE_INDEX_NOT_FOUND) {
         index = Unicode_FindSubstrInRange("abcdefghijklmnopqrstuvwxyz",
                                           0, -1, drive, 0, 1);
      }

      driveNum = (index == UNICODE_INDEX_NOT_FOUND) ? -1 : index + 1;
   }

   return driveNum;
}


/*
 *----------------------------------------------------------------------
 *
 * File_IsFullPath --
 *
 *      Is this a full path?
 *      On Windows, a path without a drive letter is considered full.
 *
 * Results:
 *      TRUE if full path.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
File_IsFullPath(ConstUnicode pathName)  // IN:
{
   ASSERT(pathName);

   return (Unicode_StartsWith(pathName, DIRSEPS) ||
          ((FileDriveNumber(pathName) > 0) &&
          (Unicode_FindSubstrInRange(pathName, 1, 2, ":" DIRSEPS,
                                     0, 2) != UNICODE_INDEX_NOT_FOUND)));
}


/*
 *----------------------------------------------------------------------
 *
 * File_Cwd --
 *
 *      Find the current directory on drive DRIVE.
 *      DRIVE is either NULL (current drive) or a string
 *      starting with [A-Za-z].
 *
 * Results:
 *      NULL if error (reported to the user).
 *
 * Side effects:
 *      The result is allocated.
 *
 *----------------------------------------------------------------------
 */

Unicode
File_Cwd(ConstUnicode drive)  // IN:
{
   int driveNum;
   utf16_t path[MAX_PATH];

   driveNum = FileDriveNumber(drive);
   ASSERT(driveNum != -1);

   if (_wgetdcwd(driveNum, path, MAX_PATH) == NULL) {
      Warning(LGPFX" %s failed for drive %d\n", __FUNCTION__, driveNum);

      return File_FullPath(NULL);
   } else {
      return Unicode_AllocWithUTF16(path);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * FileGetDiskFreeSpaceEx --
 *      Helper method to call GetDiskFreeSpaceEx and retrieve information
 *      about the amount of space available on a disk volume.
 *      None of freeBytesAvailablePtr, totalNumberOfBytesPtr or
 *      totalNumberOfFreeBytesPtr can be NULL.
 *
 * Results:
 *      freeBytesAvailablePtr - total amount of free space available to the
 *           user that is associated with the calling thread
 *      totalNumberOfBytesPtr - total amount of space available to the
 *           user that is associated with the calling thread
 *      totalNumberOfFreeBytesPtr - the total amount of free space
 *
 *      In case of error, the QuadPart of the above three is set to ~0.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
FileGetDiskFreeSpaceEx(ConstUnicode pathName,                      // IN:
                       PULARGE_INTEGER freeBytesAvailablePtr,      // OUT:
                       PULARGE_INTEGER totalNumberOfBytesPtr,      // OUT:
                       PULARGE_INTEGER totalNumberOfFreeBytesPtr)  // OUT:
{
   const utf16_t *path;
   Unicode fullPath;

   ASSERT(freeBytesAvailablePtr);
   ASSERT(totalNumberOfBytesPtr);
   ASSERT(totalNumberOfFreeBytesPtr);

   freeBytesAvailablePtr->QuadPart =
   totalNumberOfBytesPtr->QuadPart =
   totalNumberOfFreeBytesPtr->QuadPart = ~0;

   fullPath = FileGetMountPoint(pathName);

   if (fullPath == NULL) {
      return;
   }

   path = UNICODE_GET_UTF16(fullPath);
   GetDiskFreeSpaceExW(path, freeBytesAvailablePtr, totalNumberOfBytesPtr,
                       totalNumberOfFreeBytesPtr);

   Unicode_Free(fullPath);
   UNICODE_RELEASE_UTF16(path);
}


/*
 *----------------------------------------------------------------------
 *
 * File_GetFreeSpace --
 *
 *      Return the free space (in bytes) available to the user on a disk where
 *      a file is or would be. 
 *
 * Results:
 *      ~0 if error (reported to the user).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

uint64
File_GetFreeSpace(ConstUnicode pathName,  // IN:
                  Bool doNotAscend)       // IN: unused
{
   ULARGE_INTEGER ret, dummy1, dummy2;

   FileGetDiskFreeSpaceEx(pathName, &ret, &dummy1, &dummy2);

   return ret.QuadPart;
}


/*
 *----------------------------------------------------------------------
 *
 * File_GetCapacity --
 *
 *      Return the capacity (in bytes) available to the user on a disk where
 *      a file is or would be residing.
 *
 * Results:
 *      ~0 if error (reported to the user).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

uint64
File_GetCapacity(ConstUnicode pathName) // IN
{
   ULARGE_INTEGER ret, dummy1, dummy2;

   FileGetDiskFreeSpaceEx(pathName, &dummy1, &ret, &dummy2);

   return ret.QuadPart;
}


/*
 *-----------------------------------------------------------------------------
 *
 * File_Replace --
 *
 *      Replace old file with new file.
 *
 * Results:
 *      TRUE on success. FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
File_Replace(ConstUnicode oldName,  // IN: old file
             ConstUnicode newName)  // IN: new file
{
   DWORD status;

   if ((oldName == NULL) || (newName == NULL)) {
      status = ERROR_INVALID_PARAMETER;
   } else {
      const utf16_t *newPath;
      const utf16_t *oldPath;

      newPath = UNICODE_GET_UTF16(newName);
      oldPath = UNICODE_GET_UTF16(oldName);

      status = MoveFileExW(newPath, oldPath,
                           MOVEFILE_REPLACE_EXISTING) ? ERROR_SUCCESS :
                                                        GetLastError();

      UNICODE_RELEASE_UTF16(newPath);
      UNICODE_RELEASE_UTF16(oldPath);
   }

   SetLastError(status);

   return status == ERROR_SUCCESS;
}


/*
 *-----------------------------------------------------------------------------
 *
 * File_GetUniqueFileSystemID --
 *
 *      Returns a string which uniquely identifies the underlying filesystem
 *      for a given file.
 *
 *      On windows we choose the volume's serial number as the unique ID.
 *
 *      NB: The file need not exist.
 *
 *      Note, the old approach (which is known to not support mount points) is
 *      still used in case this library is used on WinNT or worse.
 *
 * Results:
 *      char* - NULL on failure, a unique ID otherwise (which must be free()'d
 *              by the caller).
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Unicode
File_GetUniqueFileSystemID(ConstUnicode pathName)  // IN:
{
   Unicode root;
   Unicode result;
   DWORD serialNum;

   /*
    * This function reports the network path upon remote filesystems.
    * Go figure. --Tommy
    */
   root = FileGetMountPoint(pathName);

   if ((root == NULL) || Unicode_StartsWith(root, "\\\\")) {
      return root;
   }

   /* Local file systems */

   if (Win32U_GetVolumeInformation(root, NULL, &serialNum, NULL,
                                   NULL, NULL)) {
      result = Unicode_Format("%d", serialNum);
   } else {
      result = NULL;
   }

   Unicode_Free(root);

   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * File_GetTimes --
 *
 *      Get the date and time that a file was created, last accessed, or
 *      last modified.
 *
 *      XXX Later on attribute change time should also be supported.
 *
 * Results:
 *      TRUE if succeed or FALSE if error.
 *      
 * Side effects:
 *      If a particular time is not available, -1 will be returned for
 *      that time.
 *
 *----------------------------------------------------------------------
 */

Bool
File_GetTimes(ConstUnicode pathName,      // IN:
              VmTimeType *createTime,     // OUT: Windows NT time format
              VmTimeType *accessTime,     // OUT: Windows NT time format
              VmTimeType *writeTime,      // OUT: Windows NT time format
              VmTimeType *attrChangeTime) // OUT: Windows NT time format
{
   HANDLE hFile;
   DWORD status;
   FILETIME cTime;
   FILETIME aTime;
   FILETIME wTime;

   if (pathName == NULL) {
      return FALSE;
   }

   ASSERT(createTime && accessTime && writeTime && attrChangeTime);

   hFile = Win32U_CreateFile(pathName, FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                             NULL);

   status = (hFile == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;

   if (status != ERROR_SUCCESS) {
      Log(LGPFX" %s: could not open \"%s\", error %u\n", __FUNCTION__,
          UTF8(pathName), status);

      return FALSE;
   }

   if (GetFileTime(hFile, &cTime, &aTime, &wTime) == 0) {
      Log(LGPFX" %s: failed to get file timestame, error %u\n", __FUNCTION__,
          GetLastError());

      return FALSE;
   }

   if (!CloseHandle(hFile)) {
      Log(LGPFX" %s: could not close file, error %u\n", __FUNCTION__,
          GetLastError());

      return FALSE;
   }

   *createTime     = QWORD(cTime.dwHighDateTime,
                           cTime.dwLowDateTime);
   *accessTime     = QWORD(aTime.dwHighDateTime,
                           aTime.dwLowDateTime);
   *writeTime      = QWORD(wTime.dwHighDateTime,
                           wTime.dwLowDateTime);
   *attrChangeTime = -1;

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * File_SetTimes --
 *
 *      Set the date and time that a file was created, last accessed, or
 *      last modified.
 *
 *      XXX Later on attribute change time should also be supported.
 *
 * Results:
 *      TRUE if succeed or FALSE if error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
File_SetTimes(ConstUnicode pathName,      // IN:
              VmTimeType createTime,      // IN: Windows NT time format
              VmTimeType accessTime,      // IN: Windows NT time format
              VmTimeType writeTime,       // IN: Windows NT time format
              VmTimeType attrChangeTime)  // IN: ignored
{
   HANDLE hFile;
   DWORD status;
   FILETIME cTime;
   FILETIME aTime;
   FILETIME wTime;

   Bool ret = TRUE;
   FILETIME *cTimePtr = NULL;
   FILETIME *aTimePtr = NULL;
   FILETIME *wTimePtr = NULL;

   if (pathName == NULL) {
      return FALSE;
   }

   hFile = Win32U_CreateFile(pathName, FILE_WRITE_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE |
                                               FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                             NULL);

   status = (hFile == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;

   if (status != ERROR_SUCCESS) {
      Log(LGPFX" %s: could not open \"%s\", error %u\n", __FUNCTION__,
          UTF8(pathName), status);

      return FALSE;
   }

   /*
    * Set time pointer so that provided time is set below.  If the time was
    * not provided (<=0), the pointer remains NULL and the original time is
    * preserved.
    */

   if (createTime > 0) {
      cTime.dwLowDateTime = LODWORD(createTime);
      cTime.dwHighDateTime = HIDWORD(createTime);
      cTimePtr = &cTime;
   }

   if (accessTime > 0) {
      aTime.dwLowDateTime = LODWORD(accessTime);
      aTime.dwHighDateTime = HIDWORD(accessTime);
      aTimePtr = &aTime;
   }

   if (writeTime > 0) {
      wTime.dwLowDateTime = LODWORD(writeTime);
      wTime.dwHighDateTime = HIDWORD(writeTime);
      wTimePtr = &wTime;
   }

   /* Old time will be preserved if time pointer is NULL. */
   if (SetFileTime(hFile, cTimePtr, aTimePtr, wTimePtr) == 0) {
      Log(LGPFX" %s: failed to set file timestame, error %u\n", __FUNCTION__,
          GetLastError());
      ret = FALSE;
   }

   if (!CloseHandle(hFile)) {
      Log(LGPFX" %s: could not close file, error %u\n", __FUNCTION__,
          GetLastError());
      ret = FALSE;
   }

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * File_SetFilePermissions --
 *
 *      Set file permissions. On Windows, we only toggle the read-only
 *      attribute. The read-only attribute is mapped to the negation
 *      of file owner's write bit.
 *
 * Results:
 *      TRUE if succeed or FALSE if error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
File_SetFilePermissions(ConstUnicode pathName,     // IN:
                        int perms)                 // IN: permissions
{
   DWORD fileAttributes;

   ASSERT(pathName);
   fileAttributes = Win32U_GetFileAttributes(pathName);
   if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
      DWORD lastError = GetLastError();
      Log(LGPFX" %s: failed to get file attributes, error %u\n", __FUNCTION__,
          GetLastError());

      return FALSE;
   }

   /*
    * Windows read-only attribute maps to the negation of file owner's
    * write bit.
    */

   if (perms & S_IWUSR) {
      fileAttributes &= ~FILE_ATTRIBUTE_READONLY;
   } else {
      fileAttributes |= FILE_ATTRIBUTE_READONLY;
   }

   if (!Win32U_SetFileAttributes(pathName, fileAttributes)) {
      DWORD lastError = GetLastError();

      Log(LGPFX" %s: failed to modify file attributes, error %u\n",
          __FUNCTION__, GetLastError());

      return FALSE;
   }

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * File_SupportsFileSize --
 *
 *      Check if the given file is on an FS that supports specified file size.
 *      Uses a hack to filter out suspicious NTFS volumes - map the network
 *      share to a drive to bypass this check.
 *
 * Results:
 *      TRUE if FS supports such file size.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
File_SupportsFileSize(ConstUnicode pathName,  // IN:
                      uint64 fileSize)        // IN:
{
   Unicode root = NULL;
   Unicode fileSystemName = NULL;
   DWORD fileSystemFlags = 0;

   Bool ret = fileSize <= CONST64U(0x7FFFFFFF);

   root = FileGetMountPoint(pathName);

   if (root == NULL) {
      goto bail;
   }

   if (Win32U_GetVolumeInformation(root, NULL, NULL, NULL, &fileSystemFlags,
                                   &fileSystemName)) {
      if (Unicode_CompareRange(fileSystemName, 0, -1,
                               "NTFS", 0, -1, TRUE) == 0) {
         /*
          * Some remote file systems claim to be NTFS, but can't do > 2GB
          * files.
          */

         if (Unicode_StartsWith(root, "\\\\")) {
            /* Use a heuristic to eliminate such claimants. */
            if (!(fileSystemFlags & FILE_CASE_SENSITIVE_SEARCH) ||
                !(fileSystemFlags & FILE_NAMED_STREAMS) ||
                !(fileSystemFlags & FILE_PERSISTENT_ACLS)) {
               goto bail;
            }
         }
         /* Current NTFS implementation limit is 16TB - 64KB. */
         ret = fileSize <= CONST64U(0xFFFFFFF0000);
      } else if ((Unicode_CompareRange(fileSystemName, 0, -1,
                                       "FAT", 0, -1, TRUE) == 0) ||
                 (Unicode_CompareRange(fileSystemName, 0, -1,
                                       "FAT32", 0, -1, TRUE) == 0)) {
         /* Maximum for FAT/FAT32 are 4GB - 1. */
         ret = fileSize <= CONST64U(0xFFFFFFFF);
      }

      Unicode_Free(fileSystemName);
   }

bail:
   Unicode_Free(root);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileRetryThisError --
 *
 *	Retry when this error is seen?
 *
 * Results:
 *	TRUE	Yes
 *	FALSE	No
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
FileRetryThisError(DWORD error,      // IN:
                   uint32 numCodes,  // IN:
                   DWORD *codes)     // IN:
{
   uint32 i;

   for (i = 0; i < numCodes; i++) { 
      if (codes[i] == error) {
         return TRUE;
      }
   }

   return FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * FileBeginSearch --
 *
 *      Begin a file search operation. Handle sharing violation errors
 *      by attempting a few retries before reporting a failure.
 *
 * Results:
 *      Return INVALID_HANDLE_VALUE on error; a valid handle on success.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static HANDLE
FileBeginSearch(const char *functionName,    // IN: calling function name
                ConstUnicode searchPath,     // IN: search path
                uint32 retryCount,           // IN: retries allowed
                WIN32_FIND_DATAW *findData,  // OUT:
                DWORD *status)               // OUT:
{
   HANDLE handle;
   Unicode pathStripped;
   const utf16_t *path;
   uint32 retries = retryCount;
   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   pathStripped = File_StripSlashes(searchPath);
   path = UNICODE_GET_UTF16(pathStripped);
   Unicode_Free(pathStripped);

   /*
    * Query a directories contents. This is not as easy as it sounds.
    * There are times when a file or its parent directory are being
    * manipulated and an operation will fail because something "is being
    * used at the moment". Examples of this are sharing violations and
    * access denied errors. The good news is that these situations should
    * be rare so the work-around is to attempt a few retries before giving up.
    */

   while (TRUE) {
      handle = FindFirstFileW(path, findData);

      *status = (handle == INVALID_HANDLE_VALUE) ? GetLastError() :
                                                   ERROR_SUCCESS;

      if (!FileRetryThisError(*status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      FileSleeper(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(path);

   if (retryCount && (retries == 0)) {
      Log(LGPFX" %s: retries exceeded on (%s); last error %d\n",
          functionName, UTF8(searchPath), *status);
   }

   return handle;
}


/*
 *----------------------------------------------------------------------
 *
 * FileListDirectoryRetry --
 *
 *      Gets the list of files (and directories) in a directory.
 *
 * Results:
 *      Returns the number of files returned or -1 on failure.
 *
 * Side effects:
 *      If ids is provided and the function succeeds, memory is
 *      allocated for both the unicode strings and the array itself
 *      and must be freed.  (See Unicode_FreeList.)
 *      The memory allocated for the array may be larger than necessary.
 *      The caller may trim it with realloc() if it cares.
 *
 *----------------------------------------------------------------------
 */

int
FileListDirectoryRetry(ConstUnicode pathName,  // IN: directory path
                       uint32 retries,         // IN: number of retries
                       Unicode **ids)          // OUT: relative paths
{
   DynBuf b;
   int count;
   DWORD status;
   HANDLE search;
   Unicode searchPath;
   WIN32_FIND_DATAW find;

   ASSERT(pathName);

   searchPath = Unicode_Append(pathName, "\\*.*");
   search = FileBeginSearch(__FUNCTION__, searchPath, retries, &find, &status);
   Unicode_Free(searchPath);

   if (search == INVALID_HANDLE_VALUE) {
      SetLastError(status);
      return -1;
   }

   DynBuf_Init(&b);
   count = 0;

   do {
      /* Strip out undesirable paths.  No one ever cares about these. */
      if (((find.cFileName[0] == L'.') &&
           (find.cFileName[1] == L'\0')) ||     // "."
          ((find.cFileName[0] == L'.') && (find.cFileName[1] == L'.')) &&
           (find.cFileName[2] == L'\0')) {      // ".."
         continue;
      }

      /* Don't create the file list if we aren't providing it to the caller. */
      if (ids) {
         Unicode id = Unicode_AllocWithUTF16(find.cFileName);
         DynBuf_Append(&b, &id, sizeof id);
      }

      count++;
   } while (FindNextFileW(search, &find));

   status = GetLastError();

   if (FindClose(search) == 0) {
      /* Don't return a failure - code review comment */
      Log(LGPFX" %s: FindClose failure (%d) on (%s)\n", __FUNCTION__,
          GetLastError(), UTF8(pathName));
   }

   if (status == ERROR_NO_MORE_FILES) {
      if (ids) {
         *ids = DynBuf_Detach(&b);
      }
   } else {
      Log(LGPFX" %s: FindNextFile failure (%d) on (%s)\n", __FUNCTION__,
          status, UTF8(pathName));
   }
   DynBuf_Destroy(&b);

   SetLastError(status);
   return (status == ERROR_NO_MORE_FILES) ? count : -1;
}


/*
 *----------------------------------------------------------------------
 *
 * File_ListDirectory --
 *
 *      Gets the list of files (and directories) in a directory.
 *
 * Results:
 *      Returns the number of files returned or -1 on failure.
 *
 * Side effects:
 *      If ids is provided and the function succeeds, memory is
 *      allocated for both the unicode strings and the array itself
 *      and must be freed.  (See Unicode_FreeList.)
 *      The memory allocated for the array may be larger than necessary.
 *      The caller may trim it with realloc() if it cares.
 *
 *----------------------------------------------------------------------
 */

int
File_ListDirectory(ConstUnicode pathName,  // IN: directory path
                   Unicode **ids)          // OUT: relative paths
{
   return FileListDirectoryRetry(pathName, 0, ids);
}


/*
 *-----------------------------------------------------------------------------
 *
 * File_WalkDirectoryStart --
 * File_WalkDirectoryNext --
 * File_WalkDirectoryEnd --
 *
 *      XXX not implemented.
 *
 *      This set of methods should be implemented to allow a pre-order
 *      traversal of the logical file system hierarchy starting at parent path
 *      avoiding cycles.
 *      
 *      File_WalkDirectoryStart should return a WalkDirContext that can be used
 *      as a token to resume the walk where it left off.
 *
 *      File_WalkDirectoryNext returns the path to the next file system item in
 *      the pre-order traversal.
 *
 *      File_WalkDirectoryEnd is used to clean up any work done to set up the
 *      WalkDirContext.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      ASSERTs.
 *
 *-----------------------------------------------------------------------------
 */

WalkDirContext
File_WalkDirectoryStart(ConstUnicode parentPath) // IN
{
   NOT_IMPLEMENTED();
}


Bool
File_WalkDirectoryNext(WalkDirContext context, // IN
                       Unicode *path)          // OUT
{
   NOT_IMPLEMENTED();
}


void
File_WalkDirectoryEnd(WalkDirContext context) // IN
{
   NOT_IMPLEMENTED();
}


/*
 *----------------------------------------------------------------------------
 *
 * File_IsSameFile --
 *
 *      Determine whether both paths point to the same file.
 *
 *      Caveats: GetFileAttributes() fails when called on a network share,
 *      (eg. \\SHARE\), and GetFileInformationByHandle() can either fail or
 *      return partial information depending on the underlying file system.
 *      While this function has been tested, and appears to work correctly,
 *      on local files and simple configurations of share drives, there are
 *      probably configurations consisting of multiple network hops and/or
 *      duplicate paths that return incorrect results.  See [178009].      
 *
 * Results:
 *      TRUE	both paths point to the same file
 *      FALSE	the paths are different or an error occured
 *
 * Side effects:
 *      Changes errno, maybe.
 *
 *----------------------------------------------------------------------------
 */

Bool
File_IsSameFile(ConstUnicode path1,  // IN:
                ConstUnicode path2)  // IN:
{
   HANDLE hFile1;
   HANDLE hFile2;
   BY_HANDLE_FILE_INFORMATION info1;
   BY_HANDLE_FILE_INFORMATION info2;
   DWORD attributes1;
   DWORD attributes2;

   const utf16_t *p1 = NULL;
   const utf16_t *p2 = NULL;
   Bool results = FALSE;
   DWORD openFlags = FILE_ATTRIBUTE_NORMAL;

   ASSERT(path1);
   ASSERT(path2);

   if (Unicode_Compare(path1, path2) == 0) {
      return TRUE;
   }

   p1 = UNICODE_GET_UTF16(path1);
   p2 = UNICODE_GET_UTF16(path2);

   attributes1 = GetFileAttributesW(p1);
   if (attributes1 == INVALID_FILE_ATTRIBUTES) {
      goto bail;
   }

   attributes2 = GetFileAttributesW(p2);
   if (attributes2 == INVALID_FILE_ATTRIBUTES) {
      goto bail;
   }

   if ((attributes1 & FILE_ATTRIBUTE_DIRECTORY) !=
       (attributes2 & FILE_ATTRIBUTE_DIRECTORY)) {
      goto bail;
   }

   if (attributes1 & FILE_ATTRIBUTE_DIRECTORY) {
      openFlags |= FILE_FLAG_BACKUP_SEMANTICS;
   }

   hFile1 = CreateFileW(p1, 0, 0, NULL, OPEN_EXISTING, openFlags, NULL);

   if (hFile1 == INVALID_HANDLE_VALUE) {
      goto bail;
   }

   if (!GetFileInformationByHandle(hFile1, &info1)) {
      CloseHandle(hFile1);
      goto bail;
   }

   hFile2 = CreateFileW(p2, 0, 0, NULL, OPEN_EXISTING, openFlags, NULL);

   if (hFile2 == INVALID_HANDLE_VALUE) {
      CloseHandle(hFile1);
      goto bail;
   }

   if (!GetFileInformationByHandle(hFile2, &info2)) {
      CloseHandle(hFile1);
      CloseHandle(hFile2);
      goto bail;
   }

   CloseHandle(hFile1);
   CloseHandle(hFile2);

   results = (info1.dwVolumeSerialNumber == info2.dwVolumeSerialNumber &&
              info1.nFileIndexHigh == info2.nFileIndexHigh &&
              info1.nFileIndexLow == info2.nFileIndexLow);

bail:
   UNICODE_RELEASE_UTF16(p1);
   UNICODE_RELEASE_UTF16(p2);

   return results;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIsWritableDir --
 *
 *	Determine in a non-intrusive way if the user can create a file in a
 *	directory.
 *
 * Results:
 *	FALSE if error (reported to the user).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
FileIsWritableDir(ConstUnicode dirName)
{
   Bool ret;
   DWORD res;
   const utf16_t *path;
   HANDLE hAccessToken = INVALID_HANDLE_VALUE;
   SECURITY_DESCRIPTOR *pSec;
   DWORD secSize;
   DWORD secSizeNeeded;
   GENERIC_MAPPING genMap;
   DWORD accessMask;
   PRIVILEGE_SET privSet;
   DWORD privSetLength;
   DWORD grantedAccess;
   BOOL accessStatus;

   pSec = NULL;

   path = UNICODE_GET_UTF16(dirName);
   res = GetFileAttributesW(path);

   if (res == 0xFFFFFFFF) {
      ret = FALSE;
      goto end;
   }
   if ((res & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) {
      ret = FALSE;
      goto end;
   }

   /*
    * Get the applicable access token, which is the thread
    * impersonation token or (if not impersonating) the process token.
    * However, if we call OpenProcessToken() then the AccessCheck()
    * below fails with ERROR_NO_IMPERSONATION_TOKEN, probably because
    * the token from OpenProcessToken() is not an impersonation token.
    * So we impersonate ourselves and get the thread token again.
    */

   if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE,
                        &hAccessToken)) {
      DWORD e = GetLastError();
      BOOL s;

      /*
       * Windows 9x ahoy! We can write anywhere we want, so return TRUE.
       */
      if (e == ERROR_CALL_NOT_IMPLEMENTED) {
         ret = TRUE;
         goto end;
      } else if (e != ERROR_NO_TOKEN) {
	 Warning(LGPFX" %s: Cannot get the thread access token (error %d).\n",
                 __FUNCTION__, e);
	 ret = FALSE;
	 goto end;
      }
      s = ImpersonateSelf(SecurityImpersonation);
      if (!s) {
	 Warning(LGPFX" %s: Unable to impersonate self (error %d).\n",
                 __FUNCTION__, GetLastError());
	 ret = FALSE;
	 goto end;
      }
      s = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE,
                          &hAccessToken);
      e = GetLastError();
      RevertToSelf();
      if (!s) {
	 Warning(LGPFX" %s: Cannot get the process access token (error %d).\n",
                 __FUNCTION__, e);
	 ret = FALSE;
	 goto end;
      }
   }

   secSize = 0;
   for (;;) {
      if (GetFileSecurityW(path,
                           OWNER_SECURITY_INFORMATION |
                           GROUP_SECURITY_INFORMATION |
                           DACL_SECURITY_INFORMATION,
                           pSec, secSize, &secSizeNeeded)) {
         break;
      }

      if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
         Warning(LGPFX" %s: Cannot get the directory's security descriptor."
                 "Directory is \"%S\", error is %d.\n", __FUNCTION__, path,
                 GetLastError());
         ret = FALSE;
         goto end;
      }

      pSec = (PSECURITY_DESCRIPTOR) realloc(pSec, secSizeNeeded);
      if (pSec == NULL) {
         Warning(LGPFX" %s: Couldn't realloc\n", __FUNCTION__);
         ret = FALSE;
         goto end;
      }

      secSize = secSizeNeeded;
   }

   /*
    * If there is no SID for the group and the owner of the file, then we
    * assume this is a file on a non-windows share. In that case, the
    * permissions check is sufficient.
    */

   if ((NULL == pSec->Group) && (NULL == pSec->Owner)) {
      ret = TRUE;
      goto end;
   }

   genMap.GenericRead = FILE_GENERIC_READ;
   genMap.GenericWrite = FILE_GENERIC_WRITE;
   genMap.GenericExecute = FILE_GENERIC_EXECUTE;
   genMap.GenericAll = FILE_GENERIC_READ |
                       FILE_GENERIC_WRITE |
                       FILE_GENERIC_EXECUTE;
   accessMask = FILE_GENERIC_WRITE;
   MapGenericMask(&accessMask, &genMap);
   privSetLength = sizeof(privSet);

   if (!AccessCheck(pSec, hAccessToken, accessMask, &genMap, &privSet,
                    &privSetLength, &grantedAccess, &accessStatus)) {
      Warning(LGPFX" %s: Unable to check access rights (error %d).\n",
              __FUNCTION__, GetLastError());
      ret = FALSE;
      goto end;
   }
   ret = accessStatus;

end:
   UNICODE_RELEASE_UTF16(path);

   if (hAccessToken != INVALID_HANDLE_VALUE) {
      CloseHandle(hAccessToken);
   }

   free(pSec);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * File_GetTmpDir --
 *
 *	Determine the best temporary directory. Unsafe since the
 *	returned directory may be writeable or readable by other users.
 *	Please use Util_GetSafeTmpDir if your dependencies permit it.
 *
 * Results:
 *	NULL if error (reported to the user).
 *
 * Side effects:
 *	The result is allocated.
 *
 *----------------------------------------------------------------------
 */

char *
File_GetTmpDir(Bool useConf)  // IN: Use the config file?
{
   DWORD len;
   char *dirName = NULL;
   char *edirName = NULL;
   char *localAppDir = NULL;

   /*
    * First check to see if we are supposed to get the temp directory
    * from the config file. If not, or if this fails, get it in the only
    * way approved by Microsoft's logo certification specs.
    */

   if (useConf) {
      dirName = (char *)LocalConfig_GetString(NULL, "tmpDirectory");
      edirName = File_ExpandAndCheckDir(dirName);
      if (edirName != NULL) {
         goto done;
      }
   }

   len = Win32U_GetTempPath(0, NULL);
   if (len > 0) {
      dirName = Util_SafeMalloc((len + 1) * sizeof(*dirName));
      len = Win32U_GetTempPath(len + 1, dirName);
      if (len > 0) {
         edirName = File_ExpandAndCheckDir(dirName);
         if (edirName != NULL) {
            goto done;
         }
      }
   }

   /*
    * We need to hand-build a reasonable fallback if GetTempPath
    * returns an unusable directory. (See bug 237586.)
    */

   if (SUCCEEDED(Win32U_SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA |
                                        CSIDL_FLAG_CREATE, NULL, 0,
                                        &localAppDir))) {
      dirName = Str_Asprintf(NULL, "%s\\Temp", localAppDir);
      if (dirName) {
         File_EnsureDirectory(dirName);
         edirName = File_ExpandAndCheckDir(dirName);
      }
   }

done:
   if (edirName == NULL) {
      Warning(LGPFX" %s: Couldn't get a temporary directory\n", __FUNCTION__);
   }

   free(localAppDir);
   free(dirName);

   return edirName;
}


/*
 *----------------------------------------------------------------------
 *
 * File_MakeCfgFileExecutable --
 *
 *	Make a .vmx file executable. This is sometimes necessary 
 *      to enable MKS access to the VM.
 *
 * Results:
 *	FALSE if error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Bool
File_MakeCfgFileExecutable(ConstUnicode pathName)
{
   /* Not implemented on Windows because unnecessary */
   return TRUE;
}


/*
 *----------------------------------------------------------------------------
 *
 * File_GetSizeAlternate --
 *
 *      An alternate way to determine the filesize. Useful for finding problems
 *      with files on remote fileservers, such as described in bug 19036.
 *
 * Results:
 *      Size of file or -1.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

int64
File_GetSizeAlternate(ConstUnicode pathName)  // IN:
{
   DWORD status;
   HANDLE findHandle;

   WIN32_FIND_DATAW findData = {0};

   if (pathName == NULL) {
      return -1;
   }

   findHandle = FileBeginSearch(__FUNCTION__, pathName, 0, &findData,
                                &status);

   if (findHandle == INVALID_HANDLE_VALUE) {
      return -1;
   }

   FindClose(findHandle);

   return QWORD(findData.nFileSizeHigh, findData.nFileSizeLow);
}


/*
 *----------------------------------------------------------------------
 *
 *  FileMapErrorToErrno --
 *
 *	Map a Windows error code into a POSIX errno value.
 *
 * Results:
 *	Returns the POSIX errno value
 *
 * Side effects:
 *      The GetLastError value is set via SetLastError after the specified
 *      Windows error code is mapped to an errno.
 *
 *----------------------------------------------------------------------
 */

int
FileMapErrorToErrno(char *functionName,  // IN:
                    DWORD status)        // IN:
{
   int err;

   switch (status) {
   case ERROR_SUCCESS:
      err = 0; // no error
      break;

   case ERROR_INVALID_FUNCTION:
   case ERROR_INVALID_ACCESS:
   case ERROR_INVALID_NAME:
   case ERROR_BAD_ARGUMENTS:
   case ERROR_INVALID_PARAMETER:
      err = EINVAL; // invalid argument
      break;

   case ERROR_INVALID_HANDLE:
      err = EBADF; // bad file number (file descriptor)
      break;

   case ERROR_FILE_NOT_FOUND:
   case ERROR_PATH_NOT_FOUND:
   case ERROR_DELETE_PENDING:  // Effectively not there
      err = ENOENT; // no such file or directory
      break;

   case ERROR_TOO_MANY_OPEN_FILES:
      err = EMFILE; // too many files opened
      break;

   case ERROR_ACCESS_DENIED:
   case ERROR_CANNOT_MAKE:
      err = EACCES; // access denied
      break;

   case ERROR_NOT_ENOUGH_MEMORY:
   case ERROR_OUTOFMEMORY:
      err = ENOMEM; // out of memory
      break;

   case ERROR_WRITE_PROTECT:
      err = EROFS; // read-only file system
      break;

   case ERROR_FILE_EXISTS:
   case ERROR_ALREADY_EXISTS:
      err = EEXIST; // file exists
      break;

   case ERROR_DISK_FULL:
      err = ENOSPC; // not space left on device
      break;

   case ERROR_DIRECTORY:
      err = ENOTDIR; // not a directory
      break;

   case ERROR_INVALID_ADDRESS:
      err = EFAULT; // bad address
      break;

   case ERROR_DIR_NOT_EMPTY:
      err = ENOTEMPTY; // directory not empty
      break;

   case ERROR_CURRENT_DIRECTORY:
   case ERROR_PATH_BUSY:
      err = EBUSY; // busy or "in use"
      break;

   case ERROR_READ_FAULT:
   case ERROR_WRITE_FAULT:
   case ERROR_GEN_FAILURE:
   case ERROR_SECTOR_NOT_FOUND:
      err = EIO; // I/O error
      break;

   case ERROR_NOT_SUPPORTED:
      err = ENOSYS; // function not implemented
      break;

   case ERROR_FILENAME_EXCED_RANGE:
      err = ENAMETOOLONG;
      break;

   default:
      Log(LGPFX" %s unmapped error code %d\n", functionName, status);

      err = EIO; // return something "fatal"
      break;
   }

   SetLastError(status);

   return err;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileRenameRetry --
 *
 *	Rename a file.
 *
 * Results:
 *	0	success
 *	not 0	failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *-----------------------------------------------------------------------------
 */

int
FileRenameRetry(ConstUnicode fromPathName,  // IN:
                ConstUnicode toPathName,    // IN:
                uint32 retryCount)          // IN:
{
   DWORD status;
   const utf16_t *toPath;
   const utf16_t *fromPath;
   uint32 retries = retryCount;
   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   if ((fromPathName == NULL) || (toPathName == NULL)) {
      status = ERROR_INVALID_ADDRESS;
      goto bail;
   }

   /*
    * Rename a directory. This is not as easy as it sounds. There are times
    * when a file or its parent directory are being manipulated and an
    * operation will fail because something "is being used at the moment".
    * Examples of this are sharing violations and access denied errors. The
    * good news is that these situations should be rare so the work-around
    * is to attempt a few retries before giving up.
    */ 

   fromPath = UNICODE_GET_UTF16(fromPathName);
   toPath = UNICODE_GET_UTF16(toPathName);

   while (TRUE) {
      status = (MoveFileW(fromPath, toPath) == 0) ? GetLastError() :
                                                    ERROR_SUCCESS;

      if (!FileRetryThisError(status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      FileSleeper(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(fromPath);
   UNICODE_RELEASE_UTF16(toPath);

   if (retryCount && (retries == 0)) {
      Log(LGPFX" %s: retries exceeded; last error %d\n", __FUNCTION__,
          status);
   }

bail:

   return FileMapErrorToErrno(__FUNCTION__, status);
}


/*
 *----------------------------------------------------------------------
 *
 *  FileDeletionRetry --
 *
 *	Delete the specified file.
 *
 * Results:
 *	0	success
 *	> 0	failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *----------------------------------------------------------------------
 */

int
FileDeletionRetry(ConstUnicode pathName,  // IN:
                  Bool handleLinks,       // IN:
                  uint32 retryCount)      // IN:
{
   DWORD status;
   const utf16_t *path;
   uint32 retries = retryCount;
   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   /* no symlinks to deal with */

   if (pathName == NULL) {
      status = ERROR_INVALID_ADDRESS;
      goto bail;
   }

   /*
    * Remove a file. This is not as easy as it sounds. There are times when
    * a file or its parent directory are being manipulated and an operation
    * will fail because something "is being used at the moment". Examples of
    * this are sharing violations and access denied errors. The good news is
    * that these situations should be rare so the work-around is to attempt
    * a few retries before giving up.
    */ 

   path = UNICODE_GET_UTF16(pathName);

   while (TRUE) {
      status = (DeleteFileW(path) == 0) ? GetLastError() : ERROR_SUCCESS;

      if (!FileRetryThisError(status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      FileSleeper(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(path);

   if (retryCount && (retries == 0)) {
      Log(LGPFX" %s: retries exceeded; last error %d\n", __FUNCTION__,
          status);
   }

bail:

   return FileMapErrorToErrno(__FUNCTION__, status);
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileCreateDirectoryRetry --
 *
 *	Create a directory.
 *
 * Results:
 *	0	success
 *	> 0	failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *-----------------------------------------------------------------------------
 */

int
FileCreateDirectoryRetry(ConstUnicode pathName,  // IN:
                         uint32 retryCount)      // IN:
{
   DWORD status;
   const utf16_t *path = NULL;
   uint32 retries = retryCount;
   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   if (pathName == NULL) {
      status = ERROR_INVALID_ADDRESS;
      goto exit;
   }

   /* Obtain a UTF-16 path name */
   path = UNICODE_GET_UTF16(pathName);
   WIN32U_CHECK_LONGPATH(path, status = ERROR_FILENAME_EXCED_RANGE, exit);

   /*
    * Create a directory. This is not as easy as it sounds. There are times
    * when a file or its parent directory are being manipulated and an
    * operation will fail because something "is being used at the moment".
    * Examples of this are sharing violations and access denied errors. The
    * good news is that these situations should be rare so the work-around
    * is to attempt a few retries before giving up.
    */ 

   while (TRUE) {
      status = (CreateDirectoryW(path, NULL) == 0) ? GetLastError() :
                                                     ERROR_SUCCESS;

      if (!FileRetryThisError(status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      FileSleeper(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(path);

   if (retryCount && (retries == 0)) {
      Log(LGPFX" %s: retries exceeded; last error %d\n", __FUNCTION__,
          status);
   }

  exit:
   return FileMapErrorToErrno(__FUNCTION__, status);
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileRemoveDirectoryRetry --
 *
 *	Delete a directory.
 *
 * Results:
 *	0	success
 *	> 0	failure (errno)
 *
 * Side effects:
 *      May change the host file system.
 *
 *-----------------------------------------------------------------------------
 */

int
FileRemoveDirectoryRetry(ConstUnicode pathName,  // IN:
                         uint32 retryCount)      // IN:
{
   DWORD status;
   const utf16_t *path;
   uint32 retries = retryCount;
   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   if (pathName == NULL) {
      status = ERROR_INVALID_ADDRESS;
      goto bail;
   }

   /*
    * Remove a directory. This is not as easy as it sounds. There are times
    * when a file or its parent directory are being manipulated and an
    * operation will fail because something "is being used at the moment".
    * Examples of this are sharing violations and access denied errors. The
    * good news is that these situations should be rare so the work-around
    * is to attempt a few retries before giving up.
    */ 

   path = UNICODE_GET_UTF16(pathName);

   while (TRUE) {
      status = (RemoveDirectoryW(path) == 0) ? GetLastError() : ERROR_SUCCESS;

      if (!FileRetryThisError(status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      FileSleeper(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(path);

   if (retryCount && (retries == 0)) {
      Log(LGPFX" %s: retries exceeded; last error %d\n", __FUNCTION__,
          status);
   }

bail:

   return FileMapErrorToErrno(__FUNCTION__, status);
}


/*
 *----------------------------------------------------------------------------
 *
 * File_IsCharDevice --
 *
 *      This function checks whether the given file is a char device
 *      and return TRUE in such case. This is often useful on Windows
 *      where files like COM?, LPT? must be differentiated from "normal"
 *      disk files.
 *
 * Results:
 *      TRUE    is a character device
 *      FALSE   is not a character device or error
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

Bool
File_IsCharDevice(ConstUnicode pathName)  // IN:
{
   Bool result;
   DWORD status;
   HANDLE handle;
   DWORD fileType;
   const utf16_t *path;

   if (pathName == NULL) {
      return FALSE;
   }

   path = UNICODE_GET_UTF16(pathName);

   handle = CreateFileW(path, 0, 0, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);

   UNICODE_RELEASE_UTF16(path);

   status = (handle == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;

   if (status == ERROR_SUCCESS) {
      fileType = GetFileType(handle);

      CloseHandle(handle);

      result = (fileType == FILE_TYPE_CHAR);
   } else {
      result = FALSE;
   }

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileAttributesRetry --
 *
 *	Return the attributes of a file. Time units are in OS native time.
 *
 * Results:
 *	0	success
 *	> 0	failure (errno)
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
FileAttributesRetry(ConstUnicode pathName,  // IN:
                    uint32 retries,         // IN:
                    FileData *fileData)     // OUT:
{
   DWORD status;
   const utf16_t *path;
   WIN32_FILE_ATTRIBUTE_DATA info = { 0 };
   DWORD retryErrorList[] = {
                               ERROR_SHARING_VIOLATION,
                               ERROR_ACCESS_DENIED
                            };

   if (pathName == NULL) {
      status = ERROR_INVALID_ADDRESS;
      goto byeBye;
   }

   path = UNICODE_GET_UTF16(pathName);

   /*
    * Obtain the attributes of the file. This is not as easy as it sounds. A
    * program (e.g. a virus checker) may be manipulating the directory
    * containing the file (or the file itself) at the time of the request.
    * This will cause the request to fail with a sharing violation. The good
    * news is that this should be rare so the work-around is to attempt a
    * few retries before giving up.
    */ 

   while (TRUE) {
      if (GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
         status = 0;
      } else {
         status = GetLastError();
      }

      if (!FileRetryThisError(status, ARRAYSIZE(retryErrorList),
                              retryErrorList) || (retries == 0)) {
         break;
      }

      FileSleeper(100);
      retries--;
   }

   UNICODE_RELEASE_UTF16(path);

   if (status != ERROR_SUCCESS) {
      goto byeBye;
   }

   if (fileData != NULL) {
      /*
       * XXX TODO: File permissions should be determined more accurately. See
       * HgfsCopyTypePermissions for reference.
       */

      if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          fileData->fileType = FILE_TYPE_DIRECTORY;
          fileData->fileMode = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ?
                               0555 : 0777;
      } else {
          fileData->fileType = FILE_TYPE_REGULAR;
          fileData->fileMode = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ?
                               0444 : 0666;
      }

      /* FileTimeToLocalFileTime/FileTimeToSystemTime on times? */
      fileData->fileCreationTime = QWORD(info.ftCreationTime.dwHighDateTime,
                                         info.ftCreationTime.dwLowDateTime);

      fileData->fileModificationTime =
                                    QWORD(info.ftLastWriteTime.dwHighDateTime,
                                          info.ftLastWriteTime.dwLowDateTime);

      fileData->fileAccessTime = QWORD(info.ftLastAccessTime.dwHighDateTime,
                                       info.ftLastAccessTime.dwLowDateTime);

      fileData->fileSize = QWORD(info.nFileSizeHigh,
                                 info.nFileSizeLow);

      fileData->fileOwner = 0; // no uid for Windows
      fileData->fileGroup = 0; // no gid for Windows
   }

byeBye:

   return FileMapErrorToErrno(__FUNCTION__, status);
}
