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
 * cdkWaitingViewController.m --
 *
 *      Implementation of CdkWaitingViewController.
 */

#import "cdkWaitingViewController.h"


@implementation CdkWaitingViewController


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkWaitingViewController waitingViewController] --
 *
 *      Creates and returns a new autoreleased waiting view
 *      controller.
 *
 * Results:
 *      A new view controller, or nil if none could be created.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkWaitingViewController *)waitingViewController
{
   return [[[CdkWaitingViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWaitingViewController init] --
 *
 *      Initializes a waiting view controller.
 *
 * Results:
 *      self or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   return [super initWithNibName:@"WaitingView" bundle:nil];
}


@end // @implementation CdkWaitingViewController
