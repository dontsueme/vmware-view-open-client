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
 * cdkDesktopsViewController.m --
 *
 *      Implementation of CdkDesktopsViewController.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import "cdkBroker.h"
#import "cdkDesktop.h"
#import "cdkDesktopFormatter.h"
#import "cdkDesktopSize.h"
#import "cdkDesktopSizesWindowController.h"
#import "cdkDesktopsViewController.h"
#import "cdkPrefs.h"
#import "cdkString.h"
#import "cdkWindowController.h"


#include "util.hh"


#define ICON_SIZE 32


@interface CdkDesktopsViewController ()
-(void)selectPreferedSize;
-(void)updateCustomSize;
-(id)itemForSize:(CdkPrefsDesktopSize)size;
-(CdkPrefsDesktopSize)sizeForItem:(id)item;
@end // @interface CdkDesktopsViewController


static NSString const *KEY_PATH_CUSTOM_DESKTOP_SIZE = @"customDesktopSize";
static NSString const *KEY_PATH_CONTINE_ENABLED_DEP =
   @"desktopsController.selection.canConnect";


@implementation CdkDesktopsViewController


@synthesize desktops;


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkDesktopsViewController desktopsViewController] --
 *
 *      Creates and returns a desktops view controller.
 *
 * Results:
 *      A new desktops view controller.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktopsViewController *)desktopsViewController
{
   return [[[CdkDesktopsViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController init] --
 *
 *      Initialize this object by creating an empty array of desktops.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   self = [super initWithNibName:@"DesktopsView" bundle:nil];
   if (!self) {
      return nil;
   }
   [self setDesktops:[NSArray array]];
   [[CdkPrefs sharedPrefs] addObserver:self
                            forKeyPath:KEY_PATH_CUSTOM_DESKTOP_SIZE
                               options:0
                               context:nil];
   [self addObserver:self
          forKeyPath:KEY_PATH_CONTINE_ENABLED_DEP
             options:0
             context:nil];
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController dealloc] --
 *
 *      Free our list of desktops.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      desktops are released.
 *
 *-----------------------------------------------------------------------------
 */

-(void)dealloc
{
   [self removeObserver:self
             forKeyPath:KEY_PATH_CONTINE_ENABLED_DEP];
   [[CdkPrefs sharedPrefs] removeObserver:self
                               forKeyPath:KEY_PATH_CUSTOM_DESKTOP_SIZE];
   [self setDesktops:nil];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController awakeFromNib:] --
 *
 *      Initialize the UI.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)awakeFromNib
{
   [self updateCustomSize];
   [self selectPreferedSize];
   [desktopsView setRowHeight:ICON_SIZE + VM_SPACING];
   [desktopCell setFormatter:[CdkDesktopFormatter desktopFormatter]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController updateCustomSize] --
 *
 *      Updates the custom size menu item with the custom size, hiding
 *      it if there is none.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)updateCustomSize
{
   CdkDesktopSize *size = [[CdkPrefs sharedPrefs] customDesktopSize];
   [customSize setTitle:size ? [size description] : @""];
   [customSize setHidden:size == nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController itemForSize:] --
 *
 *      Get the menu item corresponding to a given size.
 *
 * Results:
 *      A menuitem.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)itemForSize:(CdkPrefsDesktopSize)size // IN
{
   switch (size) {
   case CDK_PREFS_ALL_SCREENS:
      return allScreens;
   case CDK_PREFS_FULL_SCREEN:
      return fullScreen;
   case CDK_PREFS_LARGE_WINDOW:
      return largeWindow;
   case CDK_PREFS_SMALL_WINDOW:
      return smallWindow;
   case CDK_PREFS_CUSTOM_SIZE:
      return customSize;
   default:
      break;
   }
   NOT_IMPLEMENTED();
   return nil;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController selectPreferedSize] --
 *
 *      Updates the display pop up with the current prefered size.
 *
 * Results:
*       None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)selectPreferedSize
{
   [sizes selectItem:[self itemForSize:[[CdkPrefs sharedPrefs] desktopSize]]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[updateCustomSize observeValueForKeyPath:ofObjects:change:context:] --
 *
 *      Callback for prefs' custom size changes.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Updates custom size menu item.
 *
 *-----------------------------------------------------------------------------
 */

-(void)observeValueForKeyPath:(NSString *)keyPath    // IN
                     ofObject:(id)object             // IN/UNUSED
                       change:(NSDictionary *)change // IN/UNUSED
                      context:(void *)context        // IN/UNUSED
{
   if ([keyPath isEqualToString:KEY_PATH_CUSTOM_DESKTOP_SIZE]) {
      [self updateCustomSize];
   } else if ([keyPath isEqualToString:KEY_PATH_CONTINE_ENABLED_DEP]) {
      /*
       * XXX: For unknown reasons, we can't just have
       * keyPathsForValuesAffecting{ContinueEnabled,Desktop} which
       * returns this key path, we have to observe it manually.  It's
       * not clear why, but it seems to simply ignore ones that depend
       * on our array controller.
       *
       * If we say that desktop changes here, instead of
       * continueEnabled, that doesn't work either, because when we
       * call willChangeValueForKey:, things try to look up desktop to
       * remove their observers.  Since the value already changed,
       * we're returning the new object, which they are not yet
       * observing.
       *
       * So, we just notify about continueEnabled instead.
       */
      [self willChangeValueForKey:@"continueEnabled"];
      [self didChangeValueForKey:@"continueEnabled"];
   } else {
      [super observeValueForKeyPath:keyPath
                           ofObject:object
                             change:change
                            context:context];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController continueEnabled] --
 *
 *      Enable the continue button based on the user's selection.
 *
 * Results:
 *      YES if the user has selected a desktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)continueEnabled
{
   return [[self desktop] canConnect];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController desktop] --
 *
 *      Getter for selected desktop.
 *
 * Results:
 *      The currently selected CdkDesktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(CdkDesktop *)desktop
{
   if (!desktopsController) {
      return nil;
   }
   NSUInteger idx = [desktopsController selectionIndex];
   return idx == NSNotFound ? nil : [desktops objectAtIndex:idx];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController sizeForItem:] --
 *
 *      Get the size corresponding to a given menu item.
 *
 * Results:
 *      The desktop size represented by the menu item.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(CdkPrefsDesktopSize)sizeForItem:(id)item // IN
{
   if (item == allScreens) {
      return CDK_PREFS_ALL_SCREENS;
   } else if (item == fullScreen) {
      return CDK_PREFS_FULL_SCREEN;
   } else if (item == largeWindow) {
      return CDK_PREFS_LARGE_WINDOW;
   } else if (item == smallWindow) {
      return CDK_PREFS_SMALL_WINDOW;
   } else if (item == customSize) {
      return CDK_PREFS_CUSTOM_SIZE;
   }
   NOT_IMPLEMENTED();
   return CDK_PREFS_FULL_SCREEN;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController updateDesktops] --
 *
 *      Refresh the desktop list.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop list refreshed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)updateDesktops
{
   [desktopsView reloadData];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController onConnect:] --
 *
 *      Handler for the user hitting the connect menu item.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Continue button action is invoked.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onConnect:(id)sender // IN
{
   [windowController onContinue:sender];
}

/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController onLogOff:] --
 *
 *      Action for log off menu item.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tries to kill the active session.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onLogOff:(id)sender // IN
{
   [[windowController broker] killSession:[self desktop]];
}

/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController onReset:] --
 *
 *      Action for reset menu item.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop is reset.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onReset:(id)sender // IN
{
   [[windowController broker] resetDesktop:[self desktop] quit:NO];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController onRollback:] --
 *
 *      Action for roll back menu item.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop is rolled back.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onRollback:(id)sender // IN
{
   [[windowController broker] rollbackDesktop:[self desktop]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController onSelectSize:] --
 *
 *      Action for window size menu items.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)onSelectSize:(id)sender // IN
{
   [[CdkPrefs sharedPrefs] setDesktopSize:[self sizeForItem:sender]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController onShowCustomSheet:] --
 *
 *      Click handler for "Custom..." item.  Prompt the user for a
 *      custom size.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Custom size sheet is displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onShowCustomSheet:(id)sender // IN/UNUSED
{
   CdkDesktopSizesWindowController *wc =
      [CdkDesktopSizesWindowController sharedDesktopSizesWindowController];
   [NSApp beginSheet:[wc window]
      modalForWindow:[[self view] window]
       modalDelegate:self
      didEndSelector:@selector(didEndSheet:returnCode:contextInfo:)
         contextInfo:wc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopsViewController didEndSheet:returnCode:contextInfo:] --
 *
 *      Callback for when the user selects a custom desktop size.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Custom size sheet is hidden.
 *
 *-----------------------------------------------------------------------------
 */

-(void)didEndSheet:(NSWindow *)sheet  // IN
        returnCode:(int)returnCode    // IN
       contextInfo:(void *)contextInf // IN
{
   if (returnCode == NSOKButton) {
      CdkDesktopSizesWindowController *wc =
         (CdkDesktopSizesWindowController *)contextInf;
      [[CdkPrefs sharedPrefs] setCustomDesktopSize:[wc size]];
   }
   /*
    * This both selects the custom size in the OK case, and reverts
    * back to the previous selection in the Cancel case.
    */
   [self selectPreferedSize];
   [sheet orderOut:self];
}


@end // @implementation CdkDesktopsViewController
