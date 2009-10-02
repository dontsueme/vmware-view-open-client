/*********************************************************
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
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
 * cdkDebugAssert.c --
 *
 *      Implementation of CdkDebugAssert().
 */

#include "vm_assert.h"
#include "log.h"


#include "cdkDebugAssert.h"


/*
 *-----------------------------------------------------------------------------
 *
 * CdkDebugAssert --
 *
 *      Callback for AssertMacros failure logging; logs to Warning
 *      instead of the standard fprintf to stderr.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
CdkDebugAssert(const char *componentNameString,  // IN
               const char *assertionString,      // IN
               const char *exceptionLabelString, // IN
               const char *errorString,          // IN
               const char *fileName,             // IN
               long lineNumber,                  // IN
               int errorCode)                    // IN
{
   if ((assertionString != NULL) && (*assertionString != '\0')) {
      Warning("Assertion failed: %s: %s\n", componentNameString,
              assertionString);
   } else {
      Warning("Check failed: %s:\n", componentNameString);
   }
   if (exceptionLabelString != NULL) {
      Warning("    %s\n", exceptionLabelString);
   }
   if (errorString != NULL) {
      Warning("    %s\n", errorString);
   }
   if (fileName != NULL) {
      Warning("    file: %s\n", fileName);
   }
   if (lineNumber != 0) {
      Warning("    line: %ld\n", lineNumber);
   }
   if (errorCode != 0) {
      Warning("    error: %d\n", errorCode);
   }
}
