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

/*
 *  win32uRegistry.c ---
 *
 *     Win32 registry function wrappers to make it Unicode safe
 */

#undef WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <string.h>

#include "vmware.h"
#include "str.h"
#include "win32uRegistry.h"
#include "unicode.h"
#include "codeset.h"

/*
 *-----------------------------------------------------------------------------
 *
 * Win32UCodeSetUtf16leToUtf8 --
 *
 *      Convenience wrapper to CodeSet_Utf16leToUtf8, by taking an input
 *      buffer of a known size, and copy the result into it only if
 *      it's big enough.
 *
 * Results:
 *      TRUE if buffer passed in is big enough, data is converted and copied.
 *      FALSE if CodeSet conversion failed, or buffer isn't big enough,
 *      in which case the retBuf data is undefined.
 *
 *      *retSize will contain the size of the resulting buffer
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static Bool
Win32UCodeSetUtf16leToUtf8(const utf16_t *bufIn,  // IN:
                           DWORD sizeIn,          // IN:
                           char *retBuf,          // OUT: buffer (not char **)
                           LPDWORD retSize)       // IN/OUT: gets overwritten
{
   Bool ret;
   char *bufOut;
   size_t sizeOut;

   ret = CodeSet_Utf16leToUtf8((const char *)bufIn, sizeIn, &bufOut, &sizeOut);
   if (ret) {
      /*
       * sizeOut excludes terminating NUL, whereas
       * retSize includes terminating NUL.
       */

      if (sizeOut + 1 <= *retSize) {
         memcpy(retBuf, bufOut, sizeOut);
         retBuf[sizeOut] = '\0';
      } else {
         /*
          * Buffer not big enough, not copying data, will return FALSE.
          */

         ret = FALSE;
      }

      /*
       * Record and return the resulting size in any case.
       */

      *retSize = (DWORD)sizeOut;
      free(bufOut);
   } else {
      /* Conversion failure. */
      NOT_IMPLEMENTED();
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32UWriteBackRegData --
 *
 *      Common logic to handle the write-back of value data retrieved from the
 *      Registry.  If it is a string type, convert back to UTF-8.
 *
 * Results:
 *      Returns ERROR_SUCCESS if the buffer passed in is big
 *      enough; data is converted and copied.
 *
 *      Returns ERROR_MORE_DATA if the data buffer isn't big enough,
 *      in which case the retBuf data is undefined.
 *
 *      *dataSize will contain the size of the resulting buffer.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static LONG
Win32UWriteBackRegData(const void *rawData,  // IN: May be NULL
                       DWORD rawDataSize,    // IN:
                       LPBYTE data,          // OUT:
                       LPDWORD dataSize,     // OUT:
                       DWORD type,           // IN:
                       LONG oldRet)          // IN:
{
   LONG ret = oldRet;

   switch (type) {
   case REG_SZ:
   case REG_MULTI_SZ:
   case REG_EXPAND_SZ:
      /* Do UTF-16->UTF-8 conversion for string types. */
      if (ret == ERROR_SUCCESS && data != NULL) {
         ASSERT(dataSize != NULL);
         if (!Win32UCodeSetUtf16leToUtf8(rawData, rawDataSize,
                                         data, dataSize)) {
            ret = ERROR_MORE_DATA;
         }
      } else if (dataSize != NULL) {
         /*
          * Without having the actual string data, we don't know how many
          * bytes are needed.  Overestimate the number of bytes needed to
          * store the UTF-16 string as UTF-8.
          */

         *dataSize = rawDataSize * 2;
      }
      break;
   default:
      if (ret == ERROR_SUCCESS && data != NULL) {
         /* Copy data back only if input buffer is large enough. */
         ASSERT(dataSize != NULL);
         if (rawDataSize <= *dataSize) {
            memcpy(data, rawData, rawDataSize);
         } else {
            ret = ERROR_MORE_DATA;
         }
      }

      if (dataSize) {
         *dataSize = rawDataSize;
      }
      break;
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegOpenKeyEx --
 *
 *      Trivial wrapper to RegOpenKeyEx
 *
 * Results:
 *      Returns the result of RegOpenKeyExW.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegOpenKeyEx(HKEY keyName,        // IN:
                    LPCSTR subKey,       // IN: can be NULL
                    DWORD options,       // IN: reserved
                    REGSAM samDesired,   // IN:
                    HKEY *resultHandle)  // OUT:
{
   LONG ret;
   utf16_t *subKeyW;

   subKeyW = Unicode_GetAllocUTF16(subKey);

   ret = RegOpenKeyExW(keyName, subKeyW, options, samDesired, resultHandle);

   free(subKeyW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegCreateKeyEx --
 *
 *      Trivial wrapper to RegCreateKeyEx
 *
 * Results:
 *      Returns the result of RegCreateKeyExW.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegCreateKeyEx(HKEY keyName,                      // IN:
                      LPCSTR subKey,                     // IN:
                      DWORD reserved,                    // IN: reserved
                      LPCSTR className,                  // IN:
                      DWORD options,                     // IN:
                      REGSAM samDesired,                 // IN:
                      LPSECURITY_ATTRIBUTES attributes,  // IN:
                      HKEY *resultHandle,                // OUT:
                      DWORD *disposition)                // OUT: optional
{
   LONG ret;
   utf16_t *subKeyW;
   utf16_t *classNameW;

   ASSERT(subKey);              /* subKey must not be NULL */

   subKeyW = Unicode_GetAllocUTF16(subKey);
   classNameW = Unicode_GetAllocUTF16(className);

   ret = RegCreateKeyExW(keyName, subKeyW, reserved, classNameW, options,
                         samDesired, attributes, resultHandle, disposition);

   free(classNameW);
   free(subKeyW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegDeleteKey --
 *
 *      Trivial wrapper to RegDeleteKey
 *
 * Results:
 *      Returns the result of RegDeleteKey.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegDeleteKey(HKEY keyName,   // IN:
                    LPCSTR subKey)  // IN:
{
   LONG ret;
   utf16_t *subKeyW = Unicode_GetAllocUTF16(subKey);

   ret = RegDeleteKeyW(keyName, subKeyW);
   free(subKeyW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegLoadKey --
 *
 *      Trivial wrapper to RegLoadKey
 *
 * Results:
 *      Returns the result of RegLoadKey
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegLoadKey(HKEY keyName,    // IN:
                  LPCSTR subKey,   // IN:
                  LPCSTR regFile)  // IN:
{
   LONG ret;
   utf16_t *subKeyW = Unicode_GetAllocUTF16(subKey);
   utf16_t *regFileW = Unicode_GetAllocUTF16(regFile);

   ret = RegLoadKeyW(keyName, subKeyW, regFileW);
   free(subKeyW);
   free(regFileW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegUnLoadKey --
 *
 *      Trivial wrapper to RegUnLoadKey
 *
 * Results:
 *      Returns the result of RegUnLoadKey
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegUnLoadKey(HKEY keyName,   // IN:
                    LPCSTR subKey)  // IN:
{
   LONG ret;
   utf16_t *subKeyW = Unicode_GetAllocUTF16(subKey);

   ret = RegUnLoadKeyW(keyName, subKeyW);
   free(subKeyW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegSaveKey --
 *
 *      Trivial wrapper to RegSaveKey
 *
 * Results:
 *      Returns the result of RegSaveKey
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegSaveKey(HKEY keyName,                // IN:
                  LPCSTR keyFile,              // IN:
                  LPSECURITY_ATTRIBUTES attr)  // IN:
{
   LONG ret;
   utf16_t *keyFileW = Unicode_GetAllocUTF16(keyFile);

   ret = RegSaveKeyW(keyName, keyFileW, attr);
   free(keyFileW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegRestoreKey --
 *
 *      Trivial wrapper to RegRestoreKey
 *
 * Results:
 *      Returns the result of RegRestoreKey
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegRestoreKey(HKEY keyName,    // IN:
                     LPCSTR keyFile,  // IN:
                     DWORD flags)     // IN:
{
   LONG ret;
   utf16_t *keyFileW = Unicode_GetAllocUTF16(keyFile);

   ret = RegRestoreKeyW(keyName, keyFileW, flags);
   free(keyFileW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegQueryInfoKey --
 *
 *      Trivial wrapper to RegQueryInfoKeyW
 *
 * Results:
 *      Returns the result of RegQueryInfoKeyW
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegQueryInfoKey(HKEY keyName,                 // IN:
                       LPSTR className,              // OUT:
                       LPDWORD classNameSize,        // IN/OUT:
                       LPDWORD reserved,             // IN: reserved
                       LPDWORD subKeysSize,          // OUT:
                       LPDWORD maxSubKeyLen,         // OUT:
                       LPDWORD maxClassLen,          // OUT:
                       LPDWORD values,               // OUT:
                       LPDWORD maxValueNameLen,      // OUT:
                       LPDWORD maxValueLen,          // OUT:
                       LPDWORD securityDescriptor,   // OUT:
                       PFILETIME lpftLastWriteTime)  // OUT:
{
   LONG ret;

   /* caller not interested in the actual data 'className' */
   if (className == NULL) {
      ret = RegQueryInfoKeyW(keyName, NULL, classNameSize, reserved,
                             subKeysSize, maxSubKeyLen, maxClassLen, values,
                             maxValueNameLen, maxValueLen, securityDescriptor,
                             lpftLastWriteTime);
   } else {
      // Receiving buffer for className.
      utf16_t classNameW[REG_MAX_KEY_LEN] = { 0 };
      DWORD classNameSizeW = ARRAYSIZE(classNameW);

      ret = RegQueryInfoKeyW(keyName, classNameW, &classNameSizeW, reserved,
                             subKeysSize, maxSubKeyLen, maxClassLen, values,
                             maxValueNameLen, maxValueLen, securityDescriptor,
                             lpftLastWriteTime);

      if (ret == ERROR_SUCCESS) {
         if (!Win32UCodeSetUtf16leToUtf8(classNameW, classNameSizeW * 2,
                                         className, classNameSize)) {
            ret = ERROR_MORE_DATA;
         }
      }
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegDeleteValue --
 *
 *      Trivial wrapper to RegDeleteValue
 *
 * Results:
 *      Returns the result of RegDeleteValue.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegDeleteValue(HKEY keyName,      // IN:
                      LPCSTR valueName)  // IN:
{
   LONG ret;
   utf16_t *valueNameW = Unicode_GetAllocUTF16(valueName);

   ret = RegDeleteValueW(keyName, valueNameW);
   free(valueNameW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegSetValueEx --
 *
 *      Wrapper to RegSetValueEx().
 *
 * Results:
 *      Returns the result of RegDeleteValue.
 *
 *      String types of registry value need Unicode conversion
 *      whereas other types don't.  See the list of registry value types:
 *
 *      http://msdn2.microsoft.com/en-us/library/ms724884(VS.85).aspx
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegSetValueEx(HKEY keyName,      // IN:
                     LPCSTR valueName,  // IN: can be NULL
                     DWORD reserved,    // IN: reserved
                     DWORD type,        // IN:
                     const BYTE* data,  // IN, can be NULL
                     DWORD cbData)      // IN:
{
   LONG ret;
   utf16_t *valueNameW = Unicode_GetAllocUTF16(valueName);
   LPBYTE dataW;
   size_t cbDataW;

   switch (type) {
   case REG_SZ:
   case REG_EXPAND_SZ:
   case REG_MULTI_SZ:
      CodeSet_Utf8ToUtf16le(data, cbData, (char **)&dataW, &cbDataW);
      break;

   default: /* Other types of registry values pass-thru as- */
      dataW = (LPBYTE) data;
      cbDataW = cbData;
   }

   ret = RegSetValueExW(keyName, valueNameW, reserved, type, dataW, cbDataW);

   if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
      free(dataW);
   }
   free(valueNameW);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegEnumKeyEx --
 *
 *      non-trivial wrapper to RegEnumKeyEx
 *
 *      Use a stack buffer to obtain the key in UTF-16 format,
 *      convert it back to UTF-8, copy into the buffer passed in.
 *
 * Results:
 *      key, keySize, className, classNameSize will be filled in.
 *      ERROR_MORE_DATA if className buffer is not large enough.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegEnumKeyEx(HKEY keyName,             // IN:
                    DWORD index,              // IN:
                    LPSTR key,                // OUT: buffer
                    LPDWORD keySize,          // IN/OUT:
                    LPDWORD reserved,         // IN: reserved
                    LPSTR className,          // IN/OUT:
                    LPDWORD classNameSize,    // IN/OUT:
                    PFILETIME lastWriteTime)  // OUT:
{
   LONG ret;
   // Receiving buffer for key.
   utf16_t keyW[REG_MAX_KEY_LEN] = { 0 };
   DWORD keySizeW = ARRAYSIZE(keyW);

   // Receiving buffer for className.
   utf16_t classNameW[REG_MAX_KEY_LEN] = { 0 };
   DWORD classNameSizeW = ARRAYSIZE(classNameW);

   ret = RegEnumKeyExW(keyName, index, keyW, &keySizeW, reserved,
                       className == NULL ? NULL : classNameW,
                       classNameSize == NULL ? NULL : &classNameSizeW,
                       lastWriteTime);

   if (ret == ERROR_SUCCESS) {
      /*
       * Convert key name back to UTF-8
       *
       * If the call returns FALSE, the buffer is not big enough and
       * *keySize has the necessary size (excluding the terminating NUL).
       * In this case we fake ERROR_MORE_DATA.
       */

      if (!Win32UCodeSetUtf16leToUtf8(keyW, keySizeW * 2, key, keySize)) {
         ret = ERROR_MORE_DATA;
      }

      /* Convert class name back to UTF-8 */
      if (className) {
         if (!Win32UCodeSetUtf16leToUtf8(classNameW, classNameSizeW * 2,
                                         className, classNameSize)) {
            ret = ERROR_MORE_DATA;
         }
      }
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegEnumValue --
 *
 *      non-trivial wrapper to RegEnumValue
 *
 *      Use a buffer on the stack  to obtain value in UTF-16 format,
 *      convert it back to UTF-8, copy into the buffer passed in.
 *
 *      If caller needs data, use a temp buffer to obtain it from the W call.
 *      If the value type required is REG_SZ or REG_EXPAND_SZ or REG_MULTI_SZ,
 *      attempt to convert it back to UTF-8, and see if it fits in the buffer
 *      passed in.
 *
 * Results:
 *      ERROR_MORE_DATA if buffer isn't enough, in which case required size
 *      in bytes is returned in dataSize.
 *
 *      ERROR_SUCCESS, data is returned; if type is a string type, data
 *      returned is in UTF-8 format.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegEnumValue(HKEY keyName,           // IN
                    DWORD index,            // IN
                    LPSTR valueName,        // OUT: buffer
                    LPDWORD valueNameSize,  // IN/OUT:
                    LPDWORD reserved,       // IN: reserved
                    LPDWORD type,           // OUT: can be NULL
                    LPBYTE data,            // OUT: can be NULL
                    LPDWORD dataSize)       // OUT: can be NULL
{
   LONG ret;

   utf16_t *valueNameW;
   DWORD valueNameSizeW = REG_MAX_VALUE_NAME_LEN+1;
   char *dataTemp = NULL;
   DWORD dataSizeTemp = 0;
   DWORD valueType;

   valueNameW = Util_SafeCalloc(valueNameSizeW, sizeof *valueNameW);

   if (data) {
      ASSERT(dataSize != NULL);
      dataSizeTemp = *dataSize * 2;
      dataTemp = Util_SafeMalloc(dataSizeTemp);
   }

   ret = RegEnumValueW(keyName, index,
                       valueNameW, &valueNameSizeW /* # of characters */,
                       reserved, &valueType,
                       dataTemp, &dataSizeTemp /* # of bytes */);

   if (ret != ERROR_NO_MORE_ITEMS) { /* valueName is valid */
      /* Attempt to convert value name back to UTF-8 */
      if (!Win32UCodeSetUtf16leToUtf8(valueNameW, valueNameSizeW * 2,
                                      valueName, valueNameSize)) {
         ret = ERROR_MORE_DATA;
      }
   }

   ret = Win32UWriteBackRegData(dataTemp, dataSizeTemp, data, dataSize,
                                valueType, ret);

   /* Write back the type information if asked for */
   if (type) {
      *type = valueType;
   }

   free(valueNameW);
   free(dataTemp);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_RegQueryValueEx --
 *
 *      non-trivial wrapper to RegQueryValueEx
 *
 *      If caller needs data, use a temp buffer to obtain it from the W call.
 *
 *      If the value type required is REG_SZ or REG_EXPAND_SZ or REG_MULTI_SZ,
 *      attempt to convert it back to UTF-8, and see if it fits in the buffer
 *      passed in.
 *
 * Results:
 *      ERROR_MORE_DATA if buffer isn't enough, in which case required size
 *      in bytes is returned in dataSize.
 *
 *      ERROR_SUCCESS, data is returned; if type is a string type, data
 *      returned is in UTF-8 format.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_RegQueryValueEx(HKEY keyName,      // IN:
                       LPCSTR valueName,  // IN: can be NULL
                       LPDWORD reserved,  // IN: reserved
                       LPDWORD type,      // OUT: can be NULL
                       LPBYTE data,       // OUT: can be NULL
                       LPDWORD dataSize)  // IN/OUT: can be NULL if data is NULL
{
   LONG ret;
   utf16_t *valueNameW = Unicode_GetAllocUTF16(valueName);
   DWORD valueType;

   char *dataTemp = NULL;
   DWORD dataSizeTemp = 0;

   ASSERT(reserved == NULL);

   if (data) {
      ASSERT(dataSize != NULL);
      dataSizeTemp = *dataSize * 2;

      /*
       * We always allocate one extra word of space, so that we can write a
       * null character to the end of the data returned from the registry.
       * This protects us from crashing in the Win32UWriteBackRegData function
       * if the caller is attempting to get a value of type REG_SZ, and the
       * data is the registry is not properly null terminated.
       */

      dataTemp = Util_SafeMalloc(dataSizeTemp + sizeof L'\0');
   }

   ret = RegQueryValueExW(keyName, valueNameW, reserved, &valueType,
                          dataTemp, &dataSizeTemp);

   if (dataTemp && ERROR_SUCCESS == ret && REG_SZ == valueType) {
      /*
       * Append a null word to the returned data, in case the value is not
       * null terminated in the registry.
       */

      *((PWCHAR) (dataTemp + dataSizeTemp)) = L'\0';

      /*
       * Get the length of the string. The registry doesn't enforce any rules
       * on the consistency of the data written into a value of REG_SZ type,
       * so a poorly written application can end up writing bytes _after_ the
       * NULL character in the string (e.g., because of an off-by-one error).
       *
       * If we try to convert those extra bytes to utf8, the conversion will
       * typically fail and we get a Panic() (cf. bug 352057). Getting the
       * string length here prevents us from crashing in this (fairly common)
       * case, and cuts down on the number of support calls.
       */

      dataSizeTemp = Unicode_LengthInBytes(dataTemp, STRING_ENCODING_UTF16);
   }

   ret = Win32UWriteBackRegData(dataTemp, dataSizeTemp, data, dataSize,
                                valueType, ret);

   /* Write back the type information if asked for */
   if (type) {
      *type = valueType;
   }

   free(valueNameW);
   free(dataTemp);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Win32U_AllocRegQueryValueEx --
 *
 *      Non-trivial wrapper to RegQueryValueEx that allocates a buffer
 *      for the return value.
 *
 * Results:
 *      ERROR_SUCCESS, data is returned; if type is a string type, data returned
 *      is in UTF-8 format.
 *
 * Side effects:
 *      Allocates memory for 'data'. Caller is responsible for freeing it.
 *
 *-----------------------------------------------------------------------------
 */

LONG
Win32U_AllocRegQueryValueEx(HKEY keyName,      // IN:
                            LPCSTR valueName,  // IN: can be NULL
                            LPDWORD reserved,  // IN: reserved
                            LPDWORD type,      // OUT/OPT: can be NULL
                            LPBYTE *data,      // OUT:
                            LPDWORD dataSize)  // OUT:
{
   LONG ret;
   utf16_t *valueNameW = Unicode_GetAllocUTF16(valueName);
   DWORD valueType;
   char *rawData = NULL;
   DWORD rawDataSize = 0;
   DWORD bufferSize = 0;

   ASSERT(data);
   ASSERT(dataSize);
   ASSERT(reserved == NULL);

   *data = NULL;
   *dataSize = 0;

   ret = RegQueryValueExW(keyName, valueNameW, reserved, NULL, NULL,
                          &bufferSize);
   if (ret != ERROR_SUCCESS || bufferSize == 0) {
      bufferSize = 256;
   }

   /*
    * This loops with a growing buffer because:
    * * The registry value could be modified between calls to
    *   RegQueryValueExW.
    * * RegQueryValueExW will never tell us how large a buffer we need
    *   for HKEY_PERFORMANCE_DATA values.
    */

   while (TRUE) {
      rawData = Util_SafeRealloc(rawData, bufferSize);
      rawDataSize = bufferSize;
      ret = RegQueryValueExW(keyName, valueNameW, reserved, &valueType,
                             rawData, &rawDataSize);
      if (ERROR_MORE_DATA != ret) {
         break;
      }
      bufferSize *= 2;
   }

   if (ERROR_SUCCESS != ret) {
      goto exit;
   }

   ASSERT(rawDataSize <= bufferSize);

   /* Write back the data */
   switch (valueType) {
      /* Do UTF-16->UTF-8 conversion for string types. */
      case REG_SZ:
      case REG_MULTI_SZ:
      case REG_EXPAND_SZ:
      {
         size_t sizeOut;

         if (!CodeSet_Utf16leToUtf8(rawData, rawDataSize, data, &sizeOut)) {
            /*
             * Someone may not have set the right data size.  Let's try
             * to convert up to NUL if one is found.
             */

            DWORD lenInBytes = rawDataSize;
            const utf16_t *p1 = (utf16_t*)rawData;
            const utf16_t *p2 = (utf16_t*)&rawData[rawDataSize];
            
            if (valueType == REG_MULTI_SZ) {    /* look for four NUL bytes */
               while (p1 < p2) {
                  for (; *p1 != 0 && p1 < p2; p1++) {
                  }
                  p1++;
                  if (p1 < p2 && *p1 == 0) {
                     lenInBytes = (char *)p1 - rawData + 2;
                     break;
                  }
               }
            } else {
               for (; *p1 != 0 && p1 < p2; p1++) {
               }
               if (p1 < p2) {
                  lenInBytes = (char *)p1 - rawData + 2;
               }
            }

            ASSERT(lenInBytes <= rawDataSize);
            ASSERT(lenInBytes % 2 == 0);

            if (lenInBytes >= rawDataSize ||
                !CodeSet_Utf16leToUtf8(rawData, lenInBytes, data, &sizeOut)) {
               ret = ERROR_INVALID_DATA;
               goto exit;
            }
         }
         *dataSize = sizeOut;
         break;
      }
      default:
         /* Shrink the buffer down to what we actually used. */
         *data = Util_SafeRealloc(rawData, rawDataSize);
         *dataSize = rawDataSize;
         rawData = NULL;
         break;
   }

   /* Write back the type information if asked for */
   if (type) {
      *type = valueType;
   }

exit:
   free(valueNameW);
   free(rawData);

   return ret;
}
