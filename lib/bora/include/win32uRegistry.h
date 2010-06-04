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
 * win32uRegistry.h --
 *
 *    These are identity wrappers for Unicode versions of Windows
 *    registry functions.
 *    Input strings are expected to be in UTF-8 encoding.
 */

#ifndef _WIN32UREGISTRY_H_
#define _WIN32UREGISTRY_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"

#define REG_MAX_KEY_LEN 255
#define REG_MAX_VALUE_NAME_LEN 16383

/*
 * Wrappers for Windows Registry operations
 * Most of them are constructed as identity wrappers
 * where input/output parameters are kept as the same type,
 * unless otherwise noted.
 */

LONG
Win32U_RegOpenKeyEx(HKEY keyName,      // IN
                    LPCSTR subKey,     //IN, can be null
                    DWORD options,     // reserved
                    REGSAM samDesired, // IN
                    PHKEY result);     // OUT


LONG
Win32U_RegCreateKeyEx(HKEY keyName,                     // IN             
                      LPCSTR subKey,                    // IN
                      DWORD reserved,                   // reserved       
                      LPCSTR className,                 // IN             
                      DWORD options,                    // IN             
                      REGSAM samDesired,                // IN
                      LPSECURITY_ATTRIBUTES attributes, // IN 
                      HKEY *resultHandle,               // OUT
                      DWORD *disposition);              // OUT, optional

LONG
Win32U_RegDeleteKey(HKEY keyName,          // IN
                    LPCSTR subKey);        // IN

LONG
Win32U_RegLoadKey(HKEY keyName,          // IN
                  LPCSTR subKey,         // IN
                  LPCSTR regFile);       // IN

LONG
Win32U_RegUnLoadKey(HKEY keyName,          // IN
                    LPCSTR subKey);        // IN

LONG
Win32U_RegSaveKey(HKEY keyName,                // IN
                  LPCSTR keyFile,              // IN
                  LPSECURITY_ATTRIBUTES attr); // IN

LONG
Win32U_RegRestoreKey(HKEY keyName,          // IN
                     LPCSTR keyFile,        // IN
                     DWORD flags);          // IN

LONG
Win32U_RegQueryInfoKey(HKEY keyName,                       // IN
                       LPSTR className,                    // IN
                       LPDWORD classNameSize,              // IN/OUT
                       LPDWORD reserved,                   // reserved
                       LPDWORD subKeysSize,                // OUT
                       LPDWORD maxSubKeyLen,               // OUT
                       LPDWORD maxClassLen,                // OUT
                       LPDWORD values,                     // OUT
                       LPDWORD maxValueNameLen,            // OUT
                       LPDWORD maxValueLen,                // OUT
                       LPDWORD securityDescriptor,         // OUT
                       PFILETIME lpftLastWriteTime);       // OUT

LONG
Win32U_RegDeleteValue(HKEY keyName,          // IN
                      LPCSTR valueName);     // IN


LONG
Win32U_RegSetValueEx(HKEY keyName,       // IN
                     LPCSTR valueName,   // IN, can be NULL
                     DWORD reserved,     // reserved
                     DWORD type,         // IN
                     const BYTE* data,   // IN, can be NULL
                     DWORD cbData);      // IN

LONG
Win32U_RegEnumKeyEx(HKEY keyName,               // IN
                    DWORD index,                // IN
                    LPSTR name,                 // OUT
                    LPDWORD cName,              // IN/OUT
                    LPDWORD reserved,           // reserved
                    LPSTR className,            // IN/OUT
                    LPDWORD cClassName,         // IN/OUT
                    PFILETIME lastWriteTime);   // OUT


LONG
Win32U_RegEnumValue(HKEY keyName,          // IN
                    DWORD index,           // IN
                    LPSTR valueName,       // OUT
                    LPDWORD chValueName,   // IN/OUT
                    LPDWORD reserved,      // reserved
                    LPDWORD type,          // OUT, optional
                    LPBYTE data,           // OUT, optional
                    LPDWORD cbData);       // IN/OUT, optional

LONG
Win32U_RegQueryValueEx(HKEY keyName,          // IN
                       LPCSTR valueName,      // IN, optional
                       LPDWORD reserved,      // reserved
                       LPDWORD type,          // OUT, optional
                       LPBYTE data,           // OUT, optional
                       LPDWORD cbData);       // IN/OUT, optional

LONG
Win32U_AllocRegQueryValueEx(HKEY keyName,               // IN
                            LPCSTR valueName,           // IN, can be NULL
                            LPDWORD reserved,           // reserved
                            LPDWORD type,               // OUT/OPT, can be NULL
                            LPBYTE  *data,              // OUT
                            LPDWORD dataSize);          // OUT
#endif // _WIN32UREGISTRY_H_
