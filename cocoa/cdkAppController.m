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
 * cdkAppController.m --
 *
 *     Implementation of CdkAppController
 */

#import <glib.h>
#import <mach-o/dyld.h>


#import "cdkAppController.h"
#import "cdkRdc.h"
#import "cdkString.h"
#import "cdkWindowController.h"


@interface CdkAppController () // Private setters
@property(readwrite, retain) CdkWindowController *windowController;
@end // @interface CdkAppController ()


static NSString *const RDC_URL = @"http://vmware.com/go/downloadmsrdc";


@implementation CdkAppController


@synthesize windowController;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkAppController dealloc] --
 *
 *      Release our resources.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Releases views.
 *
 *-----------------------------------------------------------------------------
 */

-(void)dealloc
{
   [self setWindowController:nil];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkAppController awakeFromNib] --
 *
 *      Awake handler.  If RDC is available, prompt the user for a
 *      broker.  Otherwise, display an error.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Broker screen is displayed, or an error if RDC is not found.
 *
 *-----------------------------------------------------------------------------
 */

-(void)awakeFromNib
{
   if ([CdkRdc protocolAvailable]) {
      [self setWindowController:[CdkWindowController windowController]];
      [windowController showWindow:self];
   } else {
      NSAlert *alert = [NSAlert alertWithMessageText:NS_("Additional Software Required")
                                       defaultButton:NS_("Download")
                                     alternateButton:NS_("Quit")
                                         otherButton:nil
                           informativeTextWithFormat:
                                   NS_("Microsoft Remote Desktop Connection "
                                       "Client 2 is required to connect to "
                                       "your VMware View desktops. "
                                       "Would you like to download it now?")];

      if (NSAlertDefaultReturn == [alert runModal]) {
         [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:RDC_URL]];
      }
      [NSApp terminate:self];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkAppController applicationShouldTerminateAfterLastWindowClosed:] --
 *
 *      NSApplication delegate method; indicates that the application
 *      should close when the last window closes.
 *
 * Results:
 *      YES to terminate when the last window is closed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApp // IN
{
   return YES;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkAppController applicationWillTerminate:] --
 *
 *      NSApplication delegate method; called when the app is about to
 *      terminate.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Resets the window, closing any remote sessions and killing the
 *      tunnel.
 *
 *-----------------------------------------------------------------------------
 */

-(void)applicationWillTerminate:(NSNotification *)notification // IN
{
   [windowController brokerDidRequestBroker:[windowController broker]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkAppController newDocument:] --
 *
 *      Launch a new instance of the client.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      New client is started.
 *
 *-----------------------------------------------------------------------------
 */

-(void)newDocument:(id)sender // IN/UNUSED
{
   char *argv[2] = { NULL };
   char path[PATH_MAX];
   uint32_t pathSize = PATH_MAX;
   argv[0] = path;
   int rv = _NSGetExecutablePath(path, &pathSize);
   if (rv == 0) {
      g_spawn_async(NULL, argv, NULL, (GSpawnFlags)0, NULL, NULL, NULL, NULL);
   }
}


@end // @implementation CdkAppController
