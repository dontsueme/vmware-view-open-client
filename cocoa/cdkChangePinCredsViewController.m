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
 * cdkChangePinCredsViewController.m --
 *
 *      Implementation of CdkChangePinCredsViewController.
 */

#import "cdkCreds.h"
#import "cdkChangePinCredsViewController.h"


@interface CdkChangePinCredsViewController ()
-(void)resizeForLabel;
@end

@implementation CdkChangePinCredsViewController


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkChangePinCredsViewController changePinCredsViewController] --
 *
 *      Create an autoreleased password change view controller.
 *
 * Results:
 *      A new view controller, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkChangePinCredsViewController *)changePinCredsViewController
{
   return [[[CdkChangePinCredsViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangePinCredsViewController init] --
 *
 *      Initialize a password change view controller.
 *
 * Results:
 *      self on success or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   if (!(self = [super initWithNibName:@"ChangePinCredsView"
                                bundle:nil])) {
      return nil;
   }
   [self setRepresentedObject:[CdkCreds creds]];
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangePinCredsViewController dealloc] --
 *
 *      Stop observing changes on our model.
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
   [[self representedObject] removeObserver:self forKeyPath:@"label"];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangePinCredsViewController awakeFromNib] --
 *
 *      We watch for label changes, and resize our view accordingly,
 *      but we can't do this until we have a label - which is now.
 *      Set up the observer, and do an initial resize for our current label.
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
   [[self representedObject] addObserver:self
                              forKeyPath:@"label"
                                 options:0
                                 context:nil];
   [self resizeForLabel];
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkChangePinCredsViewController keyPathsForValuesAffectingContinueEnabled] --
 *
 *      Determine which keys affect the continue button.
 *
 * Results:
 *      A set of property names.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(NSSet *)keyPathsForValuesAffectingContinueEnabled
{
   return [NSSet setWithObjects:@"representedObject.secret",
                 @"representedObject.confirm", nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangePinCredsViewController continueEnabled] --
 *
 *      Determine whether continue button should be enabled.
 *
 * Results:
 *      YES if the continue button is enabled, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)continueEnabled
{
   CdkCreds *creds = [self representedObject];
   return [creds validChangePinCreds];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangePinCredsViewController observeValueForKeyPath:ofObject:change:context:] --
 *
 *      Key value observer callback for changes to creds.label; update
 *      our size based on the new label.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Resizes this view's frame.
 *
 *-----------------------------------------------------------------------------
 */

-(void)observeValueForKeyPath:(NSString *)keyPath    // IN/UNUSED
                     ofObject:(id)ofObject           // IN/UNUSED
                       change:(NSDictionary *)change // IN/UNUSED
                      context:(void *)context        // IN/UNUSED
{
   [self resizeForLabel];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangePinCredsViewController resizeForLabel] --
 *
 *      Give the label "infinite" height for its current width to see
 *      what height it requires, and then resize our view to fit that
 *      height.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Resizes view.
 *
 *-----------------------------------------------------------------------------
 */

- (void)resizeForLabel
{
   NSSize viewSize = [[self view] frame].size;
   NSSize curSize = [label frame].size;
   NSRect bounds = [label frame];
   bounds.size.height = 10000;
   NSSize newSize = [[label cell] cellSizeForBounds:bounds];

   NSRect viewFrame = [[self view] frame];
   float dh = newSize.height - curSize.height;
   viewFrame.size.height += dh;
   [[self view] setFrame:viewFrame];
}


@end // @implementation CdkChangePinCredsViewController
