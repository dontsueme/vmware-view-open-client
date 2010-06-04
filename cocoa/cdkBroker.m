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
 * cdkBroker.m --
 *
 *      Implementation of CdkBroker object.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import "brokerAdapter.hh"
#import "cdkBroker.h"
#import "cdkBrokerAddress.h"
#import "cdkDesktop.h"
#import "cdkKeychain.h"
#import "cdkString.h"


@interface CdkDesktop (Friend)
-(cdk::Desktop *)adaptedDesktop;
@end // @interface CdkDesktop (Friend)


@implementation CdkBroker


@synthesize delegate;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker brokerWithAddress:defaultUser:defaultDomain:delegate:] --
 *
 *      Create and return an autoreleased CdkBroker object.
 *
 * Results:
 *      A new CdkBroker, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkBroker *)broker
{
   return [[[CdkBroker alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker init] --
 *
 *      Initialize an object by creating an adapter delegate and the
 *      broker object we will be wrapping.
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
   if (!(self = [super init])) {
      return nil;
   }

   mAdapter = new cdk::BrokerAdapter();
   mAdapter->SetBroker(self);

   mBroker = new cdk::Broker();
   mBroker->SetDelegate(mAdapter);

   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker dealloc] --
 *
 *      Free our resources.
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
   delete mBroker;
   delete mAdapter;

   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker address] --
 *
 *      Return the address our broker is connected to.
 *
 * Results:
 *      A CdkBrokerAddress.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(CdkBrokerAddress *)address
{
   return [CdkBrokerAddress
             addressWithHostname:[NSString stringWithUtilString:mBroker->GetHostname()]
                            port:mBroker->GetPort()
                          secure:mBroker->GetSecure()];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker desktops] --
 *
 *      Get the list of desktops available to the user.
 *
 * Results:
 *      An NSArray of CdkDesktops.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSArray *)desktops
{
   NSMutableArray *a = [NSMutableArray arrayWithCapacity:mBroker->mDesktops.size()];
   for (std::vector<cdk::Desktop *>::iterator i = mBroker->mDesktops.begin();
	i != mBroker->mDesktops.end(); i++) {
      CdkDesktop *desktop = [[CdkDesktop alloc] initWithDesktop:*i];
      [a addObject:desktop];
      [desktop release];
   }
   return a;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker connectToAddress:defaultUser:defaultDomain:] --
 *
 *      Initiates the authentication process to a given address.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The broker connects and attempts to log in to the passed-in broker.
 *
 *-----------------------------------------------------------------------------
 */

-(void)connectToAddress:(CdkBrokerAddress *)address
            defaultUser:(NSString *)defaultUser
          defaultDomain:(NSString *)defaultDomain
{
   mBroker->Initialize([NSString utilStringWithString:[address hostname]],
                       [address port],
                       [address secure],
                       [NSString utilStringWithString:defaultUser],
                       [NSString utilStringWithString:defaultDomain]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker cancelRequests] --
 *
 *      Cancel all current requests.
 *
 * Results:
 *      THe number of requests that were canceled.
 *
 * Side effects:
 *      Outstanding RPCs are canceled.
 *
 *-----------------------------------------------------------------------------
 */

-(int)cancelRequests
{
   return mBroker->CancelRequests();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker setCookieFile:] --
 *
 *      Sets the cookie file for this broker to use.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setCookieFile:(NSString *)cookieFile // IN
{
   mBroker->SetCookieFile([NSString utilStringWithString:cookieFile]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker reset] --
 *
 *      Reset the broker.  Erases all state and allows the user to
 *      connect again.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)reset
{
   mBroker->Reset();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker acceptDisclaimer] --
 *
 *      Notify the broker that the user has accepted the disclaimer.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)acceptDisclaimer
{
   mBroker->AcceptDisclaimer();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker submitCertificateFromIdentity:] --
 *
 *      Authenticate to the broker using a given identity.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)submitCertificateFromIdentity:(SecIdentityRef)ident // IN
{
   CdkKeychain *keychain = [CdkKeychain sharedKeychain];
   mBroker->SubmitCertificate(
      ident ? [keychain certificateWithIdentity:ident] : NULL,
      ident ? [keychain privateKeyWithIdentity:ident] : NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker submitUsername:passcode:] --
 *
 *      Authenticate to the broker using a given username and RSA
 *      passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)submitUsername:(NSString *)username // IN
             passcode:(NSString *)passcode // IN
{
   mBroker->SubmitPasscode([NSString utilStringWithString:username],
                           [NSString utilStringWithString:passcode]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker nextTokencode:] --
 *
 *      Authenticate to the broker using a given RSA tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)submitNextTokencode:(NSString *)tokencode // IN
{
   mBroker->SubmitNextTokencode([NSString utilStringWithString:tokencode]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker submitPin:pin:] --
 *
 *      Authenticate to the broker using given RSA pins.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)submitPin:(NSString *)pin1
             pin:(NSString *)pin2
{
   mBroker->SubmitPins([NSString utilStringWithString:pin1],
                       [NSString utilStringWithString:pin2]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker submitUsername:password:domain:] --
 *
 *      Authenticate to the broker using windows credentials.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)submitUsername:(NSString *)username // IN
             password:(NSString *)password // IN
               domain:(NSString *)domain   // IN
{
   mBroker->SubmitPassword([NSString utilStringWithString:username],
                           [NSString utilStringWithString:password],
                           [NSString utilStringWithString:domain]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker submitOldPassword:newPassword:confirm:] --
 *
 *      Provide a new password for the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)submitOldPassword:(NSString *)oldPassword // IN
             newPassword:(NSString *)newPassword // IN
                 confirm:(NSString *)confirm     // IN
{
   mBroker->ChangePassword([NSString utilStringWithString:oldPassword],
                           [NSString utilStringWithString:newPassword],
                           [NSString utilStringWithString:confirm]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker loadDesktops] --
 *
 *      Request a desktop from the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)loadDesktops
{
   mBroker->LoadDesktops();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker connectDesktop:] --
 *
 *      Connect the user to the desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)connectDesktop:(CdkDesktop *)desktop // IN
{
   mBroker->ConnectDesktop([desktop adaptedDesktop]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker reconnectDesktop] --
 *
 *      Reconnect the user to the desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)reconnectDesktop
{
   mBroker->ReconnectDesktop();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker resetDesktop:quit:] --
 *
 *      Reset (reboot) a desktop and then maybe exit when finished.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)resetDesktop:(CdkDesktop *)desktop
               quit:(BOOL)quit
{
   mBroker->ResetDesktop([desktop adaptedDesktop], quit);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker killSession:] --
 *
 *      Forcefully log out the user from a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)killSession:(CdkDesktop *)desktop // IN
{
   mBroker->KillSession([desktop adaptedDesktop]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker rollbackDesktop:] --
 *
 *      Initiate a desktop roll back.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)rollbackDesktop:(CdkDesktop *)desktop // IN
{
   mBroker->RollbackDesktop([desktop adaptedDesktop]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBroker logout] --
 *
 *      Log the user out from this broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)logout
{
   mBroker->Logout();
}


@end // @implementation CdkBroker
