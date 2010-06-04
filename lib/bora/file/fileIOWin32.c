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
 * fileIOWin32.c --
 *
 *      Implementation of the file library host specific functions for windows.
 */

#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "vmware.h"
/* Check for non-matching prototypes */
#include "file.h"
#include "fileInt.h"
#include "util.h"
#include "str.h"
#include "err.h"
#include "stats_file.h"
#include "win32u.h"

#include "unicodeOperations.h"

static const DWORD FileIOSeekOrigins[] = {
   FILE_BEGIN,
   FILE_CURRENT,
   FILE_END,
};

static const DWORD FileIOOpenActions[] = {
   OPEN_EXISTING,
   TRUNCATE_EXISTING,
   OPEN_ALWAYS,
   CREATE_NEW,
   CREATE_ALWAYS,
};


/*
 *----------------------------------------------------------------------
 *
 * FileIO_OptionalSafeInitialize -- 
 *
 *      Initialize global state. If this module is called from a 
 *      thread other than the VMX or VCPU threads, like an aioGeneric worker
 *      thread, then we cannot do things like call config. Do that sort
 *      of initialization here, which is called from a safe thread.
 *
 *      This routine is OPTIONAL if you do not call this module from a
 *      worker thread. The same initialization can be done lazily when
 *      a read/write routine is called.
 *
 * Results:
 *      None
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void 
FileIO_OptionalSafeInitialize(void)
{
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Invalidate -- 
 *
 *      Initialize a FileIODescriptor with an invalid value
 *
 * Results:
 *      None
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

void
FileIO_Invalidate(FileIODescriptor *fd) // OUT
{
   ASSERT(fd);

   memset(fd, 0, sizeof (FileIODescriptor));
}

/*
 *----------------------------------------------------------------------------
 *
 * FileIO_IsValid -- 
 *
 *      Check whether a FileIODescriptor is valid.
 *
 * Results:
 *      True if valid.
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

Bool
FileIO_IsValid(const FileIODescriptor *fd)      // IN
{
   ASSERT(fd);

   return fd->win32 != NULL;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_CreateFDWin32 --
 *
 *      This function is for specific needs: for example, when you need
 *      a CreateFile call with flags outside the scope of FileIO_Open, all
 *      these flag combinations shouldn't find their way into the file lib;
 *      make your own CreateFile call and then create a FileIODescriptor
 *      using this function. Use only FileIO_* library functions on the
 *      FileIODescriptor from that point on.
 *
 *      Because FileIODescriptor struct is different on two platforms,
 *      this function is the only one in the file library that's
 *      platform-specific.
 *
 * Results:
 *      FileIODescriptor
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

FileIODescriptor
FileIO_CreateFDWin32(HANDLE win32,              // IN: Win32 file handle
                     DWORD access,              // IN: Win32 access flags
                     DWORD attributes)          // IN: Win32 attributes
{
   FileIODescriptor fd;
   uint32 flags = 0;

   FileIO_Invalidate(&fd);

#if defined(VMX86_STATS)
   STATS_USER_INIT_MODULE_ONCE();
   fd.stats = STATS_USER_INIT_INST("Created");
#endif

   /*
    * Do the reverse of what FileIO_Open does. Since this function is likely
    * used to create a FileIODescriptor after a syscall, it's easiest for the
    * callee to pass native flags and attributes.  The list is incomplete:
    * values that aren't known to the file library are ignored.
    */

   if (access & GENERIC_READ) {
      flags |= FILEIO_OPEN_ACCESS_READ;
   }
   if (access & GENERIC_WRITE) {
      flags |= FILEIO_OPEN_ACCESS_WRITE;
   }
   if (attributes & FILE_FLAG_WRITE_THROUGH) {
      flags |= FILEIO_OPEN_SYNC;
   }
   if ((attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE)) ==
       (FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE)) {
      flags |= FILEIO_OPEN_DELETE_ASAP;
   }
   if (attributes & FILE_FLAG_NO_BUFFERING) {
      flags |= FILEIO_OPEN_UNBUFFERED;
   }

   fd.win32 = win32;
   fd.flags = flags;

   return fd;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_GetVolumeSectorSize --
 *
 *      Get sector size of underlying volume.
 *
 * Results:
 *      FALSE on error; TRUE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Bool
FileIO_GetVolumeSectorSize(const char *fileName,        // IN
                           uint32 *sectorSize)          // OUT
{
   char *vol = NULL;
   char *pos;
   DWORD dummy;
   uint32 deviceType;
   Bool success = FALSE;

   /* GetDriveType requires a trailing backslash. */
   pos = Str_Strrchr(fileName, '\\');
   if (!pos || pos[1] != '\0') {
      char *name = Str_SafeAsprintf(NULL, "%s\\", fileName);

      deviceType = Win32U_GetDriveType(name);
      free(name);
   } else {
      deviceType = Win32U_GetDriveType(fileName);
   }

   if (deviceType == DRIVE_CDROM) {
      /*
       * Bug 72924 is a race where IOCTL_STORAGE_CHECK_VERIFY reports back
       * that there is a new media, but GetDiskFreeSpace will fail as it's
       * not ready. Furthermore, issuing the latter will mess up even the
       * host so we really shouldn't do that on CDROMs.  --Tommy
       */

      *sectorSize = 2048;

      return TRUE;
   }

   /*
    * What we get is a filename with the full path. Split out the actual file
    * name and use the rest as param for GetDiskFreeSpace. Ie. if we get
    * c:\foo\bar, we want c:\. If it's a unc, we want everything upto and
    * including the 4th \. ie. \\oslo\ISO-images\ if \\oslo\ISO-images\foobar
    * was given to us.
    */

   vol = File_FullPath(fileName);
   if (vol == NULL) {
      return FALSE;
   }

   ASSERT(strlen(vol) >= 3);

   /*
    * XXX: See PR 79877. By adding this check I am restoring the incorrect
    * (but working) behaviour prior to change 269136 on hosted2005.
    * GetDiskFreeSpace does not work with names like \\.\PhysicalDrive0. If
    * we find an API call that would work with those, we should use it here.
    * File_FullPath(NULL) returns the current working directory on Windows.
    */

   if (!strncmp("\\\\.\\", vol, 4)) {
      free(vol);
      vol = File_FullPath(NULL);

      if (!vol) {
         goto exit;
      }
   }

   if (!strncmp("\\\\", vol, 2)) {
      pos = Str_Strchr(vol+2, DIRSEPC);
      if (!pos) {
         goto exit;
      }
      pos = Str_Strchr(pos+1, DIRSEPC);
      if (!pos) {
         goto exit;
      }
      pos[1] = '\0';
   } else {
      vol[3] = '\0';
   }

   success = Win32U_GetDiskFreeSpace(vol, &dummy, sectorSize, &dummy, &dummy);

exit:
   free(vol);

   return success;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Create -- 
 *
 *      Open/create a file.
 *
 * Results:
 *      FILEIO_SUCCESS on success: 'file' is set
 *      FILEIO_OPEN_ERROR_EXIST if the file already exists
 *      FILEIO_FILE_NOT_FOUND if the file could not be found
 *      FILEIO_ERROR for other errors
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

FileIOResult
FileIO_Create(FileIODescriptor *fd,     // OUT:
              ConstUnicode pathName,    // IN:
              int access,               // IN:
              FileIOOpenAction action,  // IN:
              int tbd)                  // IN: to be determined; ignored
{
   HANDLE hFile;
   DWORD attributes, desiredAccess;
   DWORD shareMode;
   FileIOResult ret;
   const utf16_t *path;
   DWORD status;

   ASSERT(fd);
   ASSERT(!FileIO_IsValid(fd));
   ASSERT(FILEIO_ERROR_LAST < 16); /* See comment in fileIO.h */

   if (pathName == NULL) {
      SetLastError(ERROR_INVALID_ADDRESS);

      return FILEIO_ERROR;
   }

#if defined(VMX86_STATS)
   {
      Unicode tmp;
      File_SplitName(pathName, NULL, NULL, &tmp);
      STATS_USER_INIT_MODULE_ONCE();
      fd->stats = STATS_USER_INIT_INST(tmp);
      Unicode_Free(tmp);
   }
#endif

   FileIO_Init(fd, pathName);

   ret = FileIO_Lock(fd, access);
   if (!FileIO_IsSuccess(ret)) {
      FileIO_Cleanup(fd);
      FileIO_Invalidate(fd);

      return ret;
   }

   desiredAccess = ((access & FILEIO_OPEN_ACCESS_READ) ? GENERIC_READ : 0) | 
                   ((access & FILEIO_OPEN_ACCESS_WRITE) ? GENERIC_WRITE : 0);

   attributes = FILE_ATTRIBUTE_NORMAL;
   if (access & FILEIO_OPEN_DELETE_ASAP) {
      attributes |= FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE;
   }
   if (access & FILEIO_OPEN_SYNC) {
      attributes |= FILE_FLAG_WRITE_THROUGH;
   }
   if (access & FILEIO_OPEN_UNBUFFERED) {
      attributes |= FILE_FLAG_NO_BUFFERING;
   }
   if (access & FILEIO_ASYNCHRONOUS) {
      attributes |= FILE_FLAG_OVERLAPPED;
      attributes |= FILE_FLAG_NO_BUFFERING;
   }
   if (access & FILEIO_OPEN_SEQUENTIAL_SCAN) {
      attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
   }
   fd->flags = access;

   /*
    * We implement FILEIO_OPEN_EXCLUSIVE_(READ|WRITE) by not passing
    * FILE_SHARE_(READ|WRITE) to CreateFile. By default we share read/write.
    */

   shareMode = 0;
   if (!(access & FILEIO_OPEN_EXCLUSIVE_READ)) {
      shareMode |= FILE_SHARE_READ;
   }
   if (!(access & FILEIO_OPEN_EXCLUSIVE_WRITE)) {
      shareMode |= FILE_SHARE_WRITE;
   }

   /* Obtain a UTF-16 path name */
   path = UNICODE_GET_UTF16(pathName);

   /* Too long for this implementation to handle? */
   if (!Unicode_StartsWith(pathName, "\\\\?\\") &&
       wcslen(path) > MAX_PATH) {
      UNICODE_RELEASE_UTF16(path);
      FileIO_Unlock(fd);
      FileIO_Cleanup(fd);
      SetLastError(ERROR_INVALID_PARAMETER);

      return FILEIO_FILE_NAME_TOO_LONG;
   }

   hFile = CreateFileW(path, desiredAccess, shareMode, NULL,
                       FileIOOpenActions[action], attributes, NULL);

   status = (hFile == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;

   UNICODE_RELEASE_UTF16(path);

   if (status != ERROR_SUCCESS) {
      FileIO_Unlock(fd);
      FileIO_Cleanup(fd);
      SetLastError(status);

      /*
       * Despite the fact that MSDN seems to indicate that GetLastError() will
       * return ERROR_ALREADY_EXISTS in this situation, it really returns
       * ERROR_FILE_EXISTS.
       * Check for both error codes to catch either conditions.
       */

      switch (status) {
      case ERROR_ALREADY_EXISTS:
      case ERROR_FILE_EXISTS:
         return FILEIO_OPEN_ERROR_EXIST;
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
         return FILEIO_FILE_NOT_FOUND;
      case ERROR_ACCESS_DENIED:
      case ERROR_INVALID_ACCESS:
      case ERROR_WRITE_PROTECT:
         return FILEIO_NO_PERMISSION;
      default:
         Warning("%s: Unrecognized error code: %u\n", __FUNCTION__, status);
         return FILEIO_ERROR;
      }
   }
   
   fd->win32 = hFile;

   FileIO_StatsInit(fd);

   return FILEIO_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Open -- 
 *
 *      Open/create a file.
 *
 * Results:
 *      FILEIO_SUCCESS on success: 'file' is set
 *      FILEIO_OPEN_ERROR_EXIST if the file already exists
 *      FILEIO_FILE_NOT_FOUND if the file could not be found
 *      FILEIO_ERROR for other errors
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

FileIOResult
FileIO_Open(FileIODescriptor *fd,     // OUT:
            ConstUnicode pathName,    // IN:
            int access,               // IN:
            FileIOOpenAction action)  // IN:
{
   return FileIO_Create(fd, pathName, access, action, 0);
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Seek -- 
 *
 *      Change the current position in a file.
 *
 * Results:
 *      On success: the new current position in bytes from the beginning of the
 *                file
 *      On failure: -1
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

uint64
FileIO_Seek(const FileIODescriptor *fd, // IN
            int64 distance,             // IN
            FileIOSeekOrigin origin)    // IN
{
   LARGE_INTEGER li;

   ASSERT(fd);

   li.QuadPart = distance;
   li.LowPart = SetFilePointer(fd->win32, li.LowPart, &li.HighPart,
                               FileIOSeekOrigins[origin]);

   if ((li.LowPart == INVALID_SET_FILE_POINTER) &&
       (GetLastError() != NO_ERROR)) {
      li.QuadPart = -1;
   }

   return li.QuadPart;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Write -- 
 *
 *      Write to a file. 
 *
 * Results:
 *      FILEIO_SUCCESS on success: '*actual' = 'requested' bytes have been
 *       written
 *      FILEIO_ERROR for other errors: only '*actual' bytes have been
 *       written for sure
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

FileIOResult
FileIO_Write(FileIODescriptor *fd,  // IN:
             const void *buf,       // IN:
             size_t requested,      // IN:
             size_t *actual)        // OUT:
{
   uint32 bytesWritten = 0, initial_requested = (uint32)requested;

   ASSERT(fd);

   STAT_INST_INC(fd->stats, NumWrites);
   STAT_INST_INC_BY(fd->stats, BytesWritten, (uint32)requested);
   STATS_ONLY({
      fd->writeIn++;
      fd->bytesWritten += requested;
   })

   /*
    * There are various places which depend on int values and
    * one day we may convert everything to int64
    */
   ASSERT_NOT_IMPLEMENTED(requested < 0x80000000);

   while (requested > 0) {
      STATS_ONLY(fd->writeDirect++;)
      if (!WriteFile(fd->win32, buf, (DWORD)requested, &bytesWritten, NULL)) {
         if (actual) {
            *actual = initial_requested - requested + bytesWritten;
         }

         return FILEIO_ERROR;
      }

      (char *)buf += bytesWritten;
      requested -= bytesWritten;
   }

   if (actual) {
      *actual = initial_requested;
   }

   return FILEIO_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Read -- 
 *
 *      Read from a file. 
 *
 * Results:
 *      FILEIO_SUCCESS on success: '*actual' = 'requested' bytes have
 *       been read
 *      FILEIO_READ_ERROR_EOF if the end of the file was reached: only
 *       '*actual' bytes have been read for sure
 *      FILEIO_ERROR for other errors: only '*actual' bytes have been
 *        read for sure
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */ 

FileIOResult
FileIO_Read(FileIODescriptor *fd,  // IN:
            void *buf,             // OUT:
            size_t requested,      // IN:
            size_t *actual)        // OUT:
{

   uint32 bytesRead = 0, initial_requested = (uint32)requested;

   ASSERT(fd);

   STAT_INST_INC(fd->stats, NumReads);
   STAT_INST_INC_BY(fd->stats, BytesRead, (uint32)requested);
   STATS_ONLY({
      fd->readIn++;
      fd->bytesRead += requested;
   })

   ASSERT_NOT_IMPLEMENTED(requested < 0x80000000);

   while (requested > 0) {
      STATS_ONLY(fd->readDirect++;)
      if (!ReadFile(fd->win32, buf, (DWORD)requested, &bytesRead, NULL)) {
         if (actual) {
            *actual = initial_requested - requested + bytesRead;
         }

         return FILEIO_ERROR;
      }

      if (bytesRead == 0) {
         if (actual) {
            *actual = initial_requested - requested;
         }

         return FILEIO_READ_ERROR_EOF;
      }

      (char *)buf += bytesRead;
      requested -= bytesRead;
   }

   if (actual) {
      *actual = initial_requested;
   }

   return FILEIO_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Truncate --
 *
 *      Truncates a file to a given length
 *
 * Results:
 *      Bool - FALSE failure, TRUE success
 *
 * Side effects:
 *      FileIODescriptor will point to the new EOF after the call
 *
 *----------------------------------------------------------------------------
 */

Bool
FileIO_Truncate(FileIODescriptor *file,  // IN:
                uint64 newLength)        // IN:
{
   uint64 curPos, seekRv;
   BOOL eofRv;
   DWORD err;

   ASSERT(file);

   /* Get current position */
   curPos = FileIO_Seek(file, 0, FILEIO_SEEK_CURRENT);
   if (curPos == -1) {
      return FALSE;
   }

   /* Seek to new EOF */
   seekRv = FileIO_Seek(file, newLength, FILEIO_SEEK_BEGIN);
   if (seekRv == -1) {
      return FALSE;
   }

   eofRv = SetEndOfFile(file->win32);

   if (!eofRv) {
      err = GetLastError();
   }

   /* 
    * Even if we fail, try to return to old position.
    * Ugh, while this might fail it seems wrong to report
    * that the function failed. -Matt
    */

   FileIO_Seek(file, curPos, FILEIO_SEEK_BEGIN);

   if (!eofRv) {
      SetLastError(err);
   }

   return eofRv;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Close -- 
 *
 *      Close a file.
 *
 * Results:
 *      TRUE: an error occured
 *      FALSE: no error occured
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

Bool
FileIO_Close(FileIODescriptor *fd)  // IN:
{  
   DWORD err;

   ASSERT(fd);

   err = (CloseHandle(fd->win32) == 0) ? GetLastError() : 0;

   FileIO_StatsExit(fd);

   /* Unlock the file if it was locked */
   FileIO_Unlock(fd);
   FileIO_Cleanup(fd);
   FileIO_Invalidate(fd);

   if (err) {
      SetLastError(err);
   }

   return err != 0;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Sync -- 
 *
 *      Synchronize the disk state of a file with its memory state
 *
 * Results:
 *      On success: 0
 *      On failure: -1
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

int
FileIO_Sync(const FileIODescriptor *fd)  // IN:
{
   ASSERT(fd);

   return FlushFileBuffers(fd->win32) ? 0 : -1;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Readv --
 *
 *      Emulates a scatter-gather read since Win32 doesn't have one. 
 *
 * Results:
 *      FILEIO_SUCCESS, FILEIO_ERROR, FILEIO_ERROR_READ_EOF
 *
 * Side effects:
 *      File Read.
 *
 *----------------------------------------------------------------------------
 */

FileIOResult
FileIO_Readv(FileIODescriptor *fd,  // IN:
             struct iovec *vector,  // IN:
             int count,             // IN:
             size_t totalSize,      // IN:
             size_t *actual)        // OUT:
{
   size_t totalBytesRead = 0;
   size_t bytesRead = 0;
   FileIOResult fres;

   ASSERT(fd);

   STAT_INST_INC(fd->stats, NumReadvs);
   STAT_INST_INC_BY(fd->stats, BytesReadv, (uint32)totalSize);
   STATS_ONLY(fd->readvIn++;)

   ASSERT_NOT_IMPLEMENTED(totalSize < 0x80000000);

   while (count-- > 0) {
      STATS_ONLY(fd->readIn--;)

      fres = FileIO_Read(fd, vector->iov_base, vector->iov_len, &bytesRead);
      if (!FileIO_IsSuccess(fres)) {
         if (actual != NULL) {
            *actual = totalBytesRead + bytesRead;
         }

         return fres;
      }
      totalBytesRead += bytesRead;
      vector++;
   }
   if (actual != NULL) {
      *actual = totalSize;
   }

   return fres;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Writev --
 *
 *      Emulates a scatter-gather read since Win32 doesn't have one. 
 *
 * Results:
 *      FILEIO_SUCCESS, FILEIO_ERROR
 *
 * Side effects:
 *      File write.
 *
 *----------------------------------------------------------------------------
 */

FileIOResult
FileIO_Writev(FileIODescriptor *fd,  // IN:
              struct iovec *vector,  // IN:
              int count,             // IN:
              size_t totalSize,      // IN:
              size_t *actual)        // OUT:
{
   size_t totalBytesWritten = 0;
   size_t bytesWritten = 0;
   FileIOResult fres;

   ASSERT(fd);

   STAT_INST_INC(fd->stats, NumWritevs);
   STAT_INST_INC_BY(fd->stats, BytesWritev, (uint32)totalSize);
   STATS_ONLY(fd->writevIn++;)

   ASSERT_NOT_IMPLEMENTED(totalSize < 0x80000000);

   while (count-- > 0) {
      STATS_ONLY(fd->writeIn--;)

      fres = FileIO_Write(fd, vector->iov_base, vector->iov_len,
                          &bytesWritten);

      if (!FileIO_IsSuccess(fres)) {
         if (actual != NULL) {
            *actual = totalBytesWritten + bytesWritten;
         }

         return fres;
      }
      totalBytesWritten += bytesWritten;
      vector++;
   }
   if (actual != NULL) {
      *actual = totalSize;
   }

   return fres;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Preadv -- 
 *
 *      This function coalesces the struct iovec* it's given so that we
 *      eventually issue a single call to ReadFile. The OVERLAPPED 
 *      structure is used to specify the offset to start reading. Once 
 *      it's done we put the data back into the user-provided buffer.
 *
 *      Note: This function WILL update the file pointer so you will need to
 *      call FileIO_Seek before calling FileIO_Read/Write afterwards.
 *
 *      Notes: 
 *         If there's a need to use a bounce buffer to avoid the several calls
 *         to ReadFile in multiple entry cases, then make sure that this is
 *         done in a thread-safe way. -Maxime
 *
 * Results:
 *      FILEIO_SUCCESS, FILEIO_ERROR 
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

FileIOResult
FileIO_Preadv(FileIODescriptor *fd,    // IN: File descriptor 
              struct iovec *entries,   // IN: Vector to read into
              int numEntries,          // IN: Number of vector entries
              uint64 offset,           // IN: Offset to start reading 
              size_t totalSize)        // IN: totalSize (bytes) in entries 

{
   uint64 fileOffset;
   int i;

   ASSERT(fd);
   ASSERT(!(fd->flags & FILEIO_ASYNCHRONOUS));

   fileOffset = offset;

   STAT_INST_INC(fd->stats, NumPreadvs);
   STAT_INST_INC_BY(fd->stats, BytesPreadv, (uint32)totalSize);
   STATS_ONLY({
      fd->preadvIn++;
      fd->bytesRead += totalSize;
   })

   /*
    * There are various places which depend on int values and
    * one day we may convert everything to int64
    */

   ASSERT_NOT_IMPLEMENTED(totalSize < 0x80000000);

   for (i = 0; i < numEntries; i++) {
      int bytesRead;
      OVERLAPPED overlapped;
      
      overlapped.Offset = LODWORD(fileOffset);
      overlapped.OffsetHigh = HIDWORD(fileOffset);
      overlapped.hEvent = 0;
      
      STATS_ONLY(fd->preadDirect++;)
      if (!ReadFile(fd->win32, entries[i].iov_base, (DWORD)entries[i].iov_len,
          &bytesRead, &overlapped)) {
         return FILEIO_ERROR;
      }
      fileOffset += bytesRead;
   }

   return FILEIO_SUCCESS;
} 


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Pwritev -- 
 *
 *      This function coalesces the struct iovec* it's given so that we
 *      eventually issue a single call to WriteFile. The OVERLAPPED 
 *      structure is used to specify the offset to start writing. Once 
 *      it's done we put the data back into the user-provided buffer.
 *
 *      Note: This function WILL update the file pointer so you will need to
 *      call FileIO_Seek before calling FileIO_Read/Write afterwards.
 *
 * Results:
 *      FILEIO_SUCCESS, FILEIO_ERROR 
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

FileIOResult
FileIO_Pwritev(FileIODescriptor *fd,   // IN: File descriptor                                     
               struct iovec *entries,  // IN: Vector to write from
               int numEntries,         // IN: Number of vector entries
               uint64 offset,          // IN: Offset to start writing 
               size_t totalSize)       // IN: Total size (bytes) in entries 
{
   uint64 fileOffset;
   int i;

   ASSERT(fd);
   ASSERT(!(fd->flags & FILEIO_ASYNCHRONOUS));

   fileOffset = offset;

   STAT_INST_INC(fd->stats, NumPwritevs);
   STAT_INST_INC_BY(fd->stats, BytesPwritev, (uint32)totalSize);
   STATS_ONLY({
      fd->pwritevIn++;
      fd->bytesWritten += totalSize;
   })

   /*
    * There are various places which depend on int values and
    * one day we may convert everything to int64
    */

   ASSERT_NOT_IMPLEMENTED(totalSize < 0x80000000);

   for (i = 0; i < numEntries; i++) {
      int bytesWritten;
      OVERLAPPED overlapped;
      
      overlapped.Offset = LODWORD(fileOffset);
      overlapped.OffsetHigh = HIDWORD(fileOffset);
      overlapped.hEvent = 0;
      
      STATS_ONLY(fd->preadDirect++;)
      if (!WriteFile(fd->win32, entries[i].iov_base, (DWORD)entries[i].iov_len,
          &bytesWritten, &overlapped)) {
         return FILEIO_ERROR;
      }
      fileOffset += bytesWritten;
   }

   return FILEIO_SUCCESS;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_GetSize -- 
 *
 *      Get size of file.
 *
 * Results:
 *      Size of file or -1;
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

int64
FileIO_GetSize(const FileIODescriptor *fd)  // IN:
{
   BY_HANDLE_FILE_INFORMATION fileInfo;

   ASSERT(fd);

   if (!GetFileInformationByHandle(fd->win32, &fileInfo)) {
      return -1;
   }

   return QWORD(fileInfo.nFileSizeHigh, fileInfo.nFileSizeLow);
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_GetAllocSize --
 *
 *      Get allocated size of file.
 *
 * Results:
 *      NOT_IMPLEMENTED
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int64
FileIO_GetAllocSize(const FileIODescriptor *fd)  // IN
{
   NOT_IMPLEMENTED();

   return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_SetAllocSize --
 *
 *      Set allocated size of file, allocating new blocks if needed.
 *
 * Results:
 *      NOT_IMPLEMENTED
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Bool
FileIO_SetAllocSize(const FileIODescriptor *fd,  // IN
                    uint64 size)                 // IN
{
   NOT_IMPLEMENTED();

   return -1;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_GetSizeByPath -- 
 *
 *      Get size of a file specified by path.
 *
 * Results:
 *      Size of file or -1;
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

int64
FileIO_GetSizeByPath(ConstUnicode pathName)  // IN:
{
   int64 ret = -1;
   FileIOResult res;
   FileIODescriptor fd;

   FileIO_Invalidate(&fd);

   /* 
    *  Calling open with an access flag of 0 causes a file descriptor to be
    *  returned without actually opening the file. 
    */

   res = FileIO_Open(&fd, pathName, 0, FILEIO_OPEN);
   
   if (FileIO_IsSuccess(res)) {
      ret = FileIO_GetSize(&fd);
      FileIO_Close(&fd);
   } 

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_Access -- 
 *
 *      We return FILEIO_SUCCESS if the file is accessible with the
 *      specified mode. If not, we will return FILEIO_ERROR. 
 *
 *      TODO: The accessMode FILEIO_ACCESS_EXEC checking is ignored. In the
 *            future a list of file extensions considered executable could
 *            be examined to provide support for this access mode.
 *
 * Results:
 *      FILEIO_SUCCESS or FILEIO_ERROR.
 *      
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

FileIOResult
FileIO_Access(ConstUnicode pathName,  // IN: Path name to be tested. May be NULL.
              int accessMode)         // IN: Access modes to be asserted
{
   DWORD status;
   const utf16_t *path;

   if (pathName == NULL) {
      SetLastError(ERROR_INVALID_ADDRESS);

      return FILEIO_ERROR;
   }

   path = UNICODE_GET_UTF16(pathName);
   status = GetFileAttributesW(path);
   UNICODE_RELEASE_UTF16(path);

   if (status == INVALID_FILE_ATTRIBUTES) {
       /* file doesn't exist or its access path has issues */
      return FILEIO_ERROR;
   }

   /* The file exists - FILEIO_ACCESS_EXISTS is always true */
 
   if (status & FILE_ATTRIBUTE_DIRECTORY) {
      /* Directories are all read and write accessible */
      return FILEIO_SUCCESS;
   }

   if ((status & FILE_ATTRIBUTE_READONLY) &&
       (accessMode & FILEIO_ACCESS_WRITE)) {
      /* file is read-only and write access is requested */
      return FILEIO_ERROR;
   }

   /* it's accessible! */
   return FILEIO_SUCCESS;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileIO_DescriptorToStream
 *
 *      Return a FILE * stream equivalent to the given FileIODescriptor.
 *      This is the logical equivalent of Posix fdopen().
 *
 *      Since the passed descriptor and returned FILE * represent the same
 *      underlying file, and their cursor is shared, you should avoid
 *      interleaving uses to both.
 *
 * Results:
 *      A FILE * representing the same underlying file as the passed descriptor
 *      NULL if there was an error, or the mode requested was incompatible
 *      with the mode of the descriptor.
 *      Caller should fclose the returned descriptor when finished.
 *
 * Side effects:
 *      New fd allocated.
 *
 *-----------------------------------------------------------------------------
 */

FILE *
FileIO_DescriptorToStream(FileIODescriptor *fdesc,  // IN:
                          Bool textMode)            // IN:
{
   int fd;
   int osfMode;
   const char *fdopenMode;
   int tmpFlags;
   FILE *stream;
   HANDLE h2;

   /* The file you pass us should be valid and opened for *something* */
   ASSERT(FileIO_IsValid(fdesc));
   ASSERT((fdesc->flags & (FILEIO_OPEN_ACCESS_READ | FILEIO_OPEN_ACCESS_WRITE)) != 0);
   tmpFlags = fdesc->flags & (FILEIO_OPEN_ACCESS_READ | FILEIO_OPEN_ACCESS_WRITE);

   if (tmpFlags == (FILEIO_OPEN_ACCESS_READ | FILEIO_OPEN_ACCESS_WRITE)) {
      fdopenMode = "r+";
      osfMode = _O_RDWR;
   } else if (tmpFlags == FILEIO_OPEN_ACCESS_WRITE) {
      fdopenMode = "w";
      osfMode = _O_WRONLY;
   } else {  /* therefore (tmpFlags == FILEIO_OPEN_ACCESS_READ) */
      fdopenMode = "r";
      osfMode = _O_RDONLY;
   }

   /*
    * We need to duplicate the OS handle then allocate a new fd on top
    * of it then get a stream on top of that. That way, when the
    * caller calls fclose() on stream, the stream, the fd, and the
    * duped OS handle die at the same time, leaving fdesc->win32
    * intact (to be closed with FileIO_Close()).
    *
    * Think very carefully before changing this code, it's very easy to
    * either leak something or kill something.
    */

   if (!DuplicateHandle(GetCurrentProcess(), fdesc->win32, GetCurrentProcess(),
                        &h2, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
      Log("%s: DuplicateHandle failed: %s.\n", __FUNCTION__, Err_ErrString());

      return NULL;
   }

   fd = _open_osfhandle((intptr_t)h2, osfMode);
   if (fd == -1) {
      Log("%s: _open_osfhandle failed: %s.\n", __FUNCTION__, Err_ErrString());
      CloseHandle(h2);

      return NULL;
   }

   stream = fdopen(fd, fdopenMode);

   if (stream == NULL) {
      Log("%s: fdopen failed: %s.\n", __FUNCTION__, Err_ErrString());
      close(fd);
   } else {
      /*
       * Force the file descriptor (and stream) into the desired mode. This
       * is done explicitly because at the time of writing the fdopen causes
       * the input file descriptor (and its generated stream) to lose its
       * text mode attribute (if it had one).
       */

      if (_setmode(_fileno(stream), textMode ? _O_TEXT : _O_BINARY) == -1) {
         Log("%s: setmode failed: %s.\n", __FUNCTION__, Err_ErrString());
         fclose(stream);
         stream = NULL;
      }
   }

   return stream;
}


/*
 *----------------------------------------------------------------------------
 *
 * FileIO_GetFlags -- 
 *
 *      Accessor for fd->flags;
 *
 * Results:
 *      fd->flags
 *
 * Side Effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

uint32
FileIO_GetFlags(FileIODescriptor *fd)  // IN:
{
   ASSERT(fd);
   ASSERT(FileIO_IsValid(fd));

   return fd->flags;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_SupportsFileSize --
 *
 *      Test whether underlying filesystem supports specified file size.
 *
 * Results:
 *      Return TRUE if such file size is supported, FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
FileIO_SupportsFileSize(const FileIODescriptor *fd,  // IN:
                        uint64 requestedSize)        // IN:
{
   ASSERT(fd);
   ASSERT(FileIO_IsValid(fd));

   /* We know that all supported filesystems support files over 2GB. */
   if ((requestedSize > 0x7FFFFFF) && fd->fileName) {
      return File_SupportsFileSize(fd->fileName, requestedSize);
   }

   /*
    * Be overoptimistic on Windows if we cannot get info from file
    * descriptor
    */

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_GetModTime --
 *
 *      Retrieve last modification time.
 *
 * Results:
 *      Return POSIX epoch time or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int64
FileIO_GetModTime(const FileIODescriptor *fd)
{
   BY_HANDLE_FILE_INFORMATION finfo;

   if (GetFileInformationByHandle(fd->win32, &finfo)) {
      uint64 winTime = (uint64)finfo.ftLastWriteTime.dwHighDateTime << 32
                       | finfo.ftLastWriteTime.dwLowDateTime;

      /*
       * The modification time returned is in 100-nanosecond since Jan 1, 1601
       * so we need to do some math to convert it to POSIX epoch time.
       */

      return (int64)((winTime / 10000000) - CONST64U(11644473600));
   } else {
      return -1;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * FileIO_SupportsPrealloc --
 *
 *      Checks if the Host OS/filesystem supports preallocation.
 *
 * Results:
 *      FALSE.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

Bool
FileIO_SupportsPrealloc(const char *pathName,    // IN
                        Bool fsCheck)            // IN
{
   return FALSE;
}
