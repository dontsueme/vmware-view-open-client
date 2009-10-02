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

#import <Cocoa/Cocoa.h>


#import "app.hh"
#import "cdkString.h"


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
 * cdk::App::ShowDialog --
 *
 *      Tells the app controller to display a message to the user.
 *      The format argument is a printf format string.
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
App::ShowDialog(GtkMessageType msgType,    // IN
		const Util::string format, // IN
		...)
{
   va_list args;
   va_start(args, format);
   NSString *label =
      [NSString stringWithUtilString:Util::FormatV(format.c_str(), args)];
   va_end(args);

   NSAlert *alert = [NSAlert alertWithMessageText:nil
                                    defaultButton:nil
                                  alternateButton:nil
                                      otherButton:nil
                        informativeTextWithFormat:@"%@", label];

   [alert beginSheetModalForWindow:[NSApp mainWindow]
                     modalDelegate:nil
                    didEndSelector:nil
                       contextInfo:nil];
}


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
   NSArray *components =
      [NSArray arrayWithObjects:[[NSBundle mainBundle] resourcePath],
               @"locale", nil ];
   return [[NSString pathWithComponents:components] utilString];
}


} // namespace cdk
