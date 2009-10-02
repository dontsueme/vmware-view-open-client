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
 * cdkDisclaimer.m --
 *
 *      Implementation of a CdkDisclaimer.
 */

#import "cdkDisclaimer.h"


@implementation CdkDisclaimer


@synthesize disclaimer;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDisclaimer init] --
 *
 *      Initialize the disclaimer with an empty string.
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
   if ((self = [super init])) {
      disclaimer = [[NSString alloc] init];
   }
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDisclaimer dealloc] --
 *
 *      Release our disclaimer string.
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
   [disclaimer release];
   [super dealloc];
}


@end // @implementation CdkDisclaimer
