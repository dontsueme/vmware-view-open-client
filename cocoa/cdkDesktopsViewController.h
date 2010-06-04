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
 * cdkDesktopsViewController.h --
 *
 *      Controller for desktop selector.
 */

#import "cdkViewController.h"


@class CdkDesktop;
@class CdkDesktopSize;
@class CdkPrefs;


@interface CdkDesktopsViewController : CdkViewController
{
   NSArray *desktops;
   IBOutlet NSArrayController *desktopsController;

   IBOutlet NSPopUpButton *sizes;
   IBOutlet NSMenuItem *allScreens;
   IBOutlet NSMenuItem *fullScreen;
   IBOutlet NSMenuItem *largeWindow;
   IBOutlet NSMenuItem *smallWindow;
   IBOutlet NSMenuItem *customSize;
   IBOutlet NSTableView *desktopsView;
   IBOutlet NSCell *desktopCell;
}


@property(copy) NSArray *desktops;
@property(readonly) CdkDesktop *desktop;


+(CdkDesktopsViewController *)desktopsViewController;

-(void)updateDesktops;

-(IBAction)onConnect:(id)sender;
-(IBAction)onLogOff:(id)sender;
-(IBAction)onReset:(id)sender;
-(IBAction)onRollback:(id)sender;
-(IBAction)onSelectSize:(id)sender;
-(IBAction)onShowCustomSheet:(id)sender;


@end // @interface CdkDesktopsViewController
