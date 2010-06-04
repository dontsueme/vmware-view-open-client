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
 * cdkDesktopSizesWindowController.m --
 *
 *      Implementation of CdkDesktopSizesWindowController.
 */

#import "cdkDesktopSize.h"
#import "cdkDesktopSizesWindowController.h"
#import "cdkPrefs.h"


@implementation CdkDesktopSizesWindowController


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkDesktopSizesWindowController sharedDesktopSizesWindowController] --
 *
 *      Gets the shared CdkDesktopSizesWindowController instance,
 *      instantiating it if necessary.
 *
 * Results:
 *      A window controller, or nil on error.
 *
 * Side effects:
 *      Creates a window controller.
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktopSizesWindowController *)sharedDesktopSizesWindowController
{
   static CdkDesktopSizesWindowController *sharedController = nil;
   if (!sharedController) {
      sharedController = [[CdkDesktopSizesWindowController alloc] init];
   }
   return sharedController;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController init] --
 *
 *      Initializes a window controller.  Allocates an array of
 *      desktop sizes, and pre-selects the prefered size.
 *
 * Results:
 *      self or nil.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   if (!(self = [super initWithWindowNibName:@"DesktopSizesWindow"])) {
      return nil;
   }
#define MAKE_SIZE(w, h) [CdkDesktopSize desktopSizeWithWidth:w	height:h]
   sizes = [[NSArray alloc] initWithObjects:
                            MAKE_SIZE(640, 480),
                            MAKE_SIZE(800, 600),
                            MAKE_SIZE(1024, 768),
                            MAKE_SIZE(1280, 854),
                            MAKE_SIZE(1280, 1024),
                            MAKE_SIZE(1440, 900),
                            MAKE_SIZE(1600, 1200),
                            MAKE_SIZE(1680, 1050),
                            MAKE_SIZE(1920, 1200),
                            MAKE_SIZE(2560, 1600),
                            nil];
   NSUInteger selected = [sizes indexOfObject:[[CdkPrefs sharedPrefs]
                                                 customDesktopSize]];
   selectedSize = selected == NSNotFound ? 0 : selected;
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController dealloc] --
 *
 *      Free the sizes array.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)dealloc
{
   [sizes release];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController awakeFromNib] --
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
   [slider setNumberOfTickMarks:[sizes count]];
   [slider setMaxValue:[sizes count] - 1];
   [slider setDoubleValue:selectedSize];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController keyPathsForValuesAffectingSize] --
 *
 *      Determines which keys affect the value of size.
 *
 * Results:
 *      A set of property names.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(NSSet *)keyPathsForValuesAffectingSize
{
   return [NSSet setWithObjects:@"selectedSize", nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController size] --
 *
 *      Gets the selected size.
 *
 * Results:
 *      The screen size.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(CdkDesktopSize *)size
{
   return [sizes objectAtIndex:(int)selectedSize];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController onCancel:] --
 *
 *      Click handler for cancel button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tells the delegate to end our sheet session.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onCancel:(id)sender // IN
{
   [NSApp endSheet:[self window] returnCode:NSCancelButton];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSizesWindowController onSelect:] --
 *
 *      Click handler for Select button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tells the delegate to end our sheet session.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onSelect:(id)sender // IN
{
   [NSApp endSheet:[self window] returnCode:NSOKButton];
}


@end // @implementation CdkDesktopSizesWindowController
