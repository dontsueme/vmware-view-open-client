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
 * cdkWinCredsViewController.m --
 *
 *      Implementation of CdkWinCredsViewController.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import "cdkBroker.h"
#import "cdkBrokerAddress.h"
#import "cdkCreds.h"
#import "cdkPrefs.h"
#import "cdkWinCredsViewController.h"
#import "cdkWindowController.h"


extern "C" {
#include "vm_assert.h"
}


@implementation CdkWinCredsViewController


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkWinCredsViewController winCredsViewController] --
 *
 *      Creates and returns a win creds view controller.
 *
 * Results:
 *      A new win creds view controller.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkWinCredsViewController *)winCredsViewController
{
   return [[[CdkWinCredsViewController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWinCredsViewController init] --
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
   if (!(self = [super initWithNibName:@"WinCredsView"
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
                 @"representedObject.domain",
                 @"representedObject.domains",
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
   return [creds validWinCreds];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWinCredsViewController savePrefs] --
 *
 *      Save the username and domain to defaults.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Defaults are updated.
 *
 *-----------------------------------------------------------------------------
 */

-(void)savePrefs
{
   CdkCreds *creds = [self representedObject];

   [[CdkPrefs sharedPrefs] setUser:[creds username]];
   [[CdkPrefs sharedPrefs] setDomain:[creds domain]];

   CdkBrokerAddress *address = [[windowController broker] address];

   const char *hostname = [[address hostname] UTF8String];
   const char *username = [[creds username] UTF8String];
   const char *secret = [[creds secret] UTF8String];
   UInt32 pwordLen = 0;
   void *pwordData = NULL;
   BOOL passwordsDiffer = YES;

   SecKeychainItemRef item;
   OSStatus rv = SecKeychainFindInternetPassword(
      NULL,
      strlen(hostname), hostname,
      0, NULL, // security domain
      strlen(username), username,
      0, NULL, // path
      [address port],
      [address secure] ? kSecProtocolTypeHTTPS : kSecProtocolTypeHTTP,
      kSecAuthenticationTypeDefault, &pwordLen, &pwordData, &item);

   /*
    * Delete the old password if we're not saving the password or it
    * differs from the current password.
    */
   if (rv == noErr) {
      passwordsDiffer = strncmp((const char *)pwordData, secret, pwordLen);
      if (![creds savePassword] || passwordsDiffer) {
         SecKeychainItemDelete(item);
      }
      SecKeychainItemFreeContent(NULL, pwordData);
   }

   if ([creds savePassword] && passwordsDiffer) {
      rv = SecKeychainAddInternetPassword(
         NULL,
         strlen(hostname), hostname,
         0, NULL, // security domain
         strlen(username), username,
         0, NULL, // path
         [address port],
         [address secure] ? kSecProtocolTypeHTTPS : kSecProtocolTypeHTTP,
         kSecAuthenticationTypeDefault,
         strlen(secret), secret, NULL);
      if (rv != noErr) {
         Warning("Could not save password: %d: %s (%s)\n", (int)rv,
                 GetMacOSStatusErrorString(rv),
                 GetMacOSStatusCommentString(rv));
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWinCredsViewController updateFocus] --
 *
 *      Make the password field the first responder if they already
 *      entered a username.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)updateFocus
{
   CdkCreds *creds = [self representedObject];
   if ([[creds username] length]) {
      [[passwordField window] makeFirstResponder:passwordField];
   }
}


@end // @implementation CdkWinCredsViewController
