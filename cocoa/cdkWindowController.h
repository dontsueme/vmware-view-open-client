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
 * cdkWindowController.h --
 *
 *      Main window controller for View Client.
 */

#import <Cocoa/Cocoa.h>


#import "cdkBroker.h"
#import "cdkProcHelper.h"


namespace cdk {
   class RestartMonitor;
} // namespace cdk


@class CdkDesktop;
@class CdkRdc;
@class CdkViewController;


@interface CdkWindowController
   : NSWindowController <CdkBrokerDelegate,
                         CdkProcHelperDelegate>
{
   NSMutableArray *viewControllers;
   CdkViewController *viewController;
   NSString *clientName;
   NSString *busyText;
   NSString *domainPassword;
   BOOL busy;
   BOOL triedKeychainPassword;

   IBOutlet NSBox *box;
   IBOutlet NSButton *goBackButton;
   IBOutlet NSImageView *banner;
   IBOutlet NSMenuItem *aboutMenu;
   IBOutlet NSMenuItem *hideMenu;
   IBOutlet NSMenuItem *quitMenu;
   IBOutlet NSMenuItem *helpMenu;

   CdkBroker *broker;
   CdkDesktop *desktop;
   CdkRdc *rdc;

   cdk::RestartMonitor *mRdcMonitor;
}


@property(copy) NSString *busyText;
@property(readonly) BOOL busy;
@property(readonly) BOOL triedKeychainPassword;
@property(readonly) BOOL goBackEnabled;
@property(readonly, assign) CdkViewController *viewController;
@property(readonly, retain) CdkBroker *broker;


+(CdkWindowController *)windowController;

-(IBAction)onContinue:(id)sender;
-(IBAction)onGoBack:(id)sender;

-(void)alertWithStyle:(NSAlertStyle)style
          messageText:(NSString *)message
informativeTextWithFormat:(NSString *)details
            arguments:(va_list)args;


@end // @interface CdkWindowController
