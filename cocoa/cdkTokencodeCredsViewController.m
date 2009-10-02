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
 * cdkTokencodeCredsViewController.m --
 *
 *      Implementation of CdkTokencodeCredsViewController.
 */

#import "cdkCreds.h"
#import "cdkTokencodeCredsViewController.h"


@implementation CdkTokencodeCredsViewController


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkTokencodeCredsViewController tokencodeCredsViewController] --
 *
 *      Creates and returns a tokencode creds view controller.
 *
 * Results:
 *      A new tokencode creds view controller.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkTokencodeCredsViewController *)tokencodeCredsViewController
{
   return [[[CdkTokencodeCredsViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkTokencodeCredsViewController init] --
 *
 *      Initialize this view's model.
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
   if (!(self = [super initWithNibName:@"TokencodeCredsView"
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
   return [NSSet setWithObjects:@"representedObject.username",
                 @"representedObject.secret", nil];
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
   return [creds validTokencodeCreds];
}


@end // @implementation CdkTokencodeCredsViewController
