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
 * **********************************************************/

/*
 * cdkDisclaimerViewController.m --
 *
 *      Implementation of CdkDisclaimerViewController.
 */

#import "cdkDisclaimer.h"
#import "cdkDisclaimerViewController.h"


@implementation CdkDisclaimerViewController


@synthesize disclaimer;


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkDisclaimerViewController disclaimerViewController] --
 *
 *      Creates and returns a disclaimer view controller.
 *
 * Results:
 *      A new disclaimer view controller.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDisclaimerViewController *)disclaimerViewController
{
   return [[[CdkDisclaimerViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDisclaimerViewController init] --
 *
 *      Constructor.
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
   if ((self = [super initWithNibName:@"DisclaimerView" bundle:nil])) {
      disclaimer = [[CdkDisclaimer alloc] init];
      [self setRepresentedObject:disclaimer];
   }
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDisclaimerViewController dealloc] --
 *
 *      Release our disclaimer.
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
   [disclaimer dealloc];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDisclaimerViewController continueEnabled] --
 *
 *      Determine whether the user can continue.
 *
 * Results:
 *      YES
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)continueEnabled
{
   return YES;
}


@end // @implementation CdkDisclaimerViewController
