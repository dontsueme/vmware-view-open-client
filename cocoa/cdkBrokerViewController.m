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
 * cdkBrokerViewController.m --
 *
 *      Implementation of CdkBrokerViewController.
 */

#import "cdkBrokerAddress.h"
#import "cdkBrokerViewController.h"
#import "cdkPrefs.h"


@implementation CdkBrokerViewController


@synthesize brokerAddress;


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkBrokerViewController brokerViewController] --
 *
 *      Creates and returns a broker view controller.
 *
 * Results:
 *      A new broker view controller.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkBrokerViewController *)brokerViewController
{
   return [[[CdkBrokerViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerViewController init] --
 *
 *      Initialize a broker view controller by specifying the nib to
 *      load and creating a broker address object.
 *
 * Results:
 *      self or nil if initialization failed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   if (!(self = [super initWithNibName:@"BrokerView" bundle:nil])) {
      return nil;
   }

   brokerAddress = [[CdkBrokerAddress alloc] init];
   [self setRepresentedObject:brokerAddress];

   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerViewController dealloc] --
 *
 *      Deallocate the broker address.
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
   [brokerAddress release];
   [super dealloc];
}



/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkBrokerViewController keyPathsForValuesAffectingContinueEnabled] --
 *
 *      Bind changes in brokerAddress.hostname to
 *      self.continueEnabled.
 *
 * Results:
 *      A set of property names.
 *
 * Side effects:
 *      If hostname changes, continueEnabled will be updated.
 *
 *-----------------------------------------------------------------------------
 */


+(NSSet *)keyPathsForValuesAffectingContinueEnabled
{
   return [NSSet setWithObjects:@"brokerAddress.hostname", nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerViewController continueEnabled] --
 *
 *      Validate this form.  Check whether the broker address has a
 *      non-empty hostname.
 *
 * Results:
 *      YES if the hostname was parseable.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)continueEnabled
{
   return [[brokerAddress hostname] length] > 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerViewController savePrefs] --
 *
 *      Add our broker to the list of recently used brokers.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)savePrefs
{
   [[CdkPrefs sharedPrefs] addRecentBroker:[brokerAddress label]];
}


@end // @implementation CdkBrokerViewController
