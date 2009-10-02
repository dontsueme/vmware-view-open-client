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
 * cdkChangeWinCredsViewController.m --
 *
 *      Implementation of CdkChangeWinCredsViewController.
 */

#import "cdkCreds.h"
#import "cdkChangeWinCredsViewController.h"


@implementation CdkChangeWinCredsViewController


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkChangeWinCredsViewController changeWinCredsViewController] --
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

+(CdkChangeWinCredsViewController *)changeWinCredsViewController
{
   return [[[CdkChangeWinCredsViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangeWinCredsViewController init] --
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
   if (!(self = [super initWithNibName:@"ChangeWinCredsView"
                                bundle:nil])) {
      return nil;
   }
   [self setRepresentedObject:[CdkCreds creds]];
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkChangeWinCredsViewController keyPathsForValuesAffectingContinueEnabled] --
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
   return [NSSet setWithObjects:@"representedObject.oldSecret",
                 @"representedObject.secret",
                 @"representedObject.confirm",
                 nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkChangeWinCredsViewController continueEnabled] --
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
   return [creds validChangeWinCreds];
}


@end // @implementation CdkChangeWinCredsViewController
