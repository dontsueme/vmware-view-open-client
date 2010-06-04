/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
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
 * app.m --
 *
 *      Implemention of cdk::App.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import <Cocoa/Cocoa.h>


#import "app.hh"
#import "cdkString.h"
#import "cdkWindowController.h"


#define VMWARE_VIEW "vmware-view"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::Main --
 *
 *      Main implementation for Mac.  Initializes BaseApp, and runs
 *      the cocoa main loop.
 *
 * Results:
 *      Status as returned by NSApplicationMain()
 *
 * Side effects:
 *      Application is started.
 *
 *-----------------------------------------------------------------------------
 */

int
App::Main(int argc,     // IN
          char *argv[]) // IN
{
   NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
   if (!Init(argc, argv)) {
      return 1;
   }
   int rc = NSApplicationMain(argc, (const char **)argv);
   Fini();
   [pool release];
   return rc;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::Show*Dialog --
 *
 *      Tells the window controller to display a message to the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#define SHOW_DIALOG(fn, style)                                          \
void                                                                    \
App::Show##fn##Dialog(const Util::string &message,                      \
                      const Util::string &details,                      \
                      va_list args)                                     \
{                                                                       \
   CdkWindowController *window = [[NSApp mainWindow] delegate];         \
   [window alertWithStyle:NS##style##AlertStyle                         \
              messageText:[NSString stringWithUtilString:message]       \
           informativeTextWithFormat:                                   \
              [NSString stringWithUtilString:details]                   \
                arguments:args];                                        \
}


SHOW_DIALOG(Error, Critical);
SHOW_DIALOG(Info, Informational);
SHOW_DIALOG(Warning, Warning);


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::GetLocaleDir --
 *
 *      Get the locale dir for gettext to use.  Use the "locale"
 *      directory inside of our resource directory.
 *
 * Results:
 *      A string containing the path to our locale dir.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
App::GetLocaleDir()
{
   return [NSString utilStringWithString:
                       [[[NSBundle mainBundle] resourcePath]
                                            stringByAppendingPathComponent:
                             @"locale"]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * App::InitLogging --
 *
 *      Initialize logging with a log file in ~/Library/Logs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Initializes Log.
 *
 *-----------------------------------------------------------------------------
 */

void
App::InitLogging()
{
   NSString *logFile = nil;
   NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,
                                                       NSUserDomainMask, YES);
   if ([dirs count] > 0) {
      logFile = [[dirs objectAtIndex:0]
                   stringByAppendingPathComponents:@"Logs",
                   @PRODUCT_VIEW_CLIENT_NAME,
                   nil];
      [[NSFileManager defaultManager] createDirectoryAtPath:logFile
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:nil];
      logFile = [[logFile stringByAppendingPathComponent:@VMWARE_VIEW]
                   stringByAppendingPathExtension:@"log"];
   }
   if (!Log_Init([logFile UTF8String], VMWARE_VIEW ".log.filename",
                 VMWARE_VIEW)) {
      Warning("Could not initialize logging.\n");
   }
   IntegrateGLibLogging();
}


} // namespace cdk
