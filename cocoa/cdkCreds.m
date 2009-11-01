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
 * cdkCreds.m --
 *
 *      Implementation of CdkCreds.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import "cdkCreds.h"


extern "C" {
#import "vm_assert.h"
}


@implementation CdkCreds


@synthesize username;
@synthesize domain;
@synthesize domains;
@synthesize secret;
@synthesize confirm;
@synthesize oldSecret;
@synthesize label;
@synthesize savePassword;
@synthesize userSelectable;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds creds] --
 *
 *      Creates and returns an autorelease CdkCreds object.
 *
 * Results:
 *      A new object, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkCreds *)creds
{
   return [[[CdkCreds alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds init] --
 *
 *      Initializes a CdkCreds object to default values.
 *
 * Results:
 *      A CdkCreds object, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   if (!(self = [super init])) {
      return nil;
   }

   [self setUsername:@""];
   [self setDomain:@""];
   [self setDomains:[NSArray array]];
   [self setSecret:@""];
   [self setConfirm:@""];
   [self setOldSecret:@""];
   [self setLabel:@""];
   [self setSavePassword:NO];
   [self setUserSelectable:NO];

   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds dealloc] --
 *
 *      Frees resources.
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
   [self setUsername:nil];
   // Must set domains to nil first, otherwise setDomain rejects domain.
   [self setDomains:nil];
   [self setDomain:nil];
   [self setSecret:nil];
   [self setConfirm:nil];
   [self setOldSecret:nil];
   [self setLabel:nil];
   [label release];

   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds setDomains] --
 *
 *      Setter for domains.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Sets the domain to the first item if it hasn't been set.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setDomains:(NSArray *)aDomains // IN
{
   [aDomains retain];
   [domains release];
   domains = [aDomains copy];
   [aDomains release];
   if ([domains count] && (![domain length] ||
                           [domains indexOfObject:domain] == NSNotFound)) {
      [self setDomain:[domains objectAtIndex:0]];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds setDomain:] --
 *
 *      Setter for domain; if domains is not empty, requires that
 *      domain is a member of domains.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setDomain:(NSString *)aDomain // IN
{
   /*
    * If we have no domains, don't require that the domain is in the
    * list.
    */
   if ([domains count] && [domains indexOfObject:aDomain] == NSNotFound) {
      Log("Domain \"%s\" is not a member of domains; not setting.\n",
          [aDomain UTF8String]);
      return;
   }
   [aDomain retain];
   [domain release];
   domain = [aDomain copy];
   [aDomain release];
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkCreds keyPathsForValuesAffectingUpnUsername] --
 *
 *      Indicate which properties upnUsername is dependent on.
 *
 * Results:
 *      A set containing property names.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(NSSet *)keyPathsForValuesAffectingUpnUsername
{
   return [NSSet setWithObjects:@"username", nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCrets upnUsername] --
 *
 *      Determines whether the username is a UPN username (contains an
 *      @).
 *
 * Results:
 *      YES if the username contains an @.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)upnUsername
{
   return [username length] &&
      [username rangeOfString:@"@"].location != NSNotFound;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds validChangePinCreds] --
 *
 *      Checks whether the values in this object represent a valid pin
 *      change state.
 *
 * Results:
 *      YES if the pin creds are valid, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)validChangePinCreds
{
   return [secret length] && [secret isEqualToString:confirm];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds validChangeWinCreds] --
 *
 *      Checks whether the values in this object represent a valid win
 *      change state.
 *
 * Results:
 *      YES if the win creds are valid, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)validChangeWinCreds
{
   return [username length] && [secret isEqualToString:confirm];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds validConfirmPinCreds] --
 *
 *      Checks whether the values in this object represent a valid pin
 *      confirmation state.
 *
 * Results:
 *      YES if the pin confirmation creds are valid, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)validConfirmPinCreds
{
   return [self validChangePinCreds];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds validPasscodeCreds] --
 *
 *      Checks whether the values in this object represent a valid
 *      passcode creds state.
 *
 * Results:
 *      YES if the passcode creds are valid, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)validPasscodeCreds
{
   return [username length] && [secret length];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds validTokencodeCreds] --
 *
 *      Checks whether the values in this object represent a valid
 *      tokencode creds state.
 *
 * Results:
 *      YES if the tokencode creds are valid, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)validTokencodeCreds
{
   return [self validPasscodeCreds];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkCreds validWinCreds] --
 *
 *      Checks whether the vlaues in this object represent a valid win
 *      creds state.
 *
 * Results:
 *      YES if the win creds are valid, NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)validWinCreds
{
   return [username length] && [self upnUsername] ||
      ([domain length] && [domains indexOfObject:domain] != NSNotFound);
}


@end // @interface CdkCreds
