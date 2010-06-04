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
 * cdkBroker.h --
 *
 *      An Objective-C wrapper for a cdk::Broker object.
 */

#import <Cocoa/Cocoa.h>
#import <openssl/x509.h>


namespace cdk {
   class Broker;
   class BrokerAdapter;
};


@class CdkBroker;
@class CdkBrokerAddress;
@class CdkDesktop;


@protocol CdkBrokerDelegate


-(void)brokerDidRequestBroker:(CdkBroker *)broker;

-(void)broker:(CdkBroker *)broker
didRequestPasscode:(NSString *)username
userSelectable:(BOOL)userSelectable;

-(void)broker:(CdkBroker *)broker
didRequestNextTokencode:(NSString *)username;

-(void)broker:(CdkBroker *)broker
didRequestPinChange:(NSString *)pin
      message:(NSString *)message
userSelectable:(BOOL)userSelectable;

-(void)broker:(CdkBroker *)broker
didRequestDisclaimer:(NSString *)disclaimer;

-(void)broker:(CdkBroker *)broker
didRequestCertificateWithIssuers:(NSArray *)issuers;

-(void)broker:(CdkBroker *)broker
didRequestPassword:(NSString *)username
     readOnly:(BOOL)readOnly
      domains:(NSArray *)domains
suggestedDomain:(NSString *)domain;

-(void)broker:(CdkBroker *)broker
didRequestPasswordChange:(NSString *)username
       domain:(NSString *)domain;

-(void)brokerDidRequestDesktop:(CdkBroker *)broker;

-(void)broker:(CdkBroker *)broker
didRequestLaunchDesktop:(CdkDesktop *)desktop;

-(void)brokerDidDisconnect:(CdkBroker *)broker;

-(void)broker:(CdkBroker *)broker
didDisconnectTunnelWithReason:(NSString *)reason;

-(void)brokerDidRequestUpdateDesktops:(CdkBroker *)broker;


@end // @protocol CdkBrokerDelegate


@interface CdkBroker : NSObject
{
   cdk::Broker *mBroker;
   cdk::BrokerAdapter *mAdapter;
   id<CdkBrokerDelegate> delegate;
}


@property(readonly) CdkBrokerAddress *address;
@property(readonly) NSArray *desktops;
@property(assign) id<CdkBrokerDelegate> delegate;


+(CdkBroker *)broker;

-(void)connectToAddress:(CdkBrokerAddress *)address
            defaultUser:(NSString *)defaultUser
          defaultDomain:(NSString *)defaultDomain;

-(int)cancelRequests;
-(void)setCookieFile:(NSString *)cookieFile;

-(void)reset;
-(void)acceptDisclaimer;
-(void)submitCertificateFromIdentity:(SecIdentityRef)identity;
-(void)submitUsername:(NSString *)username
             passcode:(NSString *)passcode;
-(void)submitNextTokencode:(NSString *)tokencode;
-(void)submitPin:(NSString *)pin1
             pin:(NSString *)pin2;
-(void)submitUsername:(NSString *)username
             password:(NSString *)password
               domain:(NSString *)domain;
-(void)submitOldPassword:(NSString *)oldPassword
             newPassword:(NSString *)newPassword
                 confirm:(NSString *)confirm;
-(void)loadDesktops;
-(void)connectDesktop:(CdkDesktop *)desktop;
-(void)reconnectDesktop;
-(void)resetDesktop:(CdkDesktop *)desktop
               quit:(BOOL)quit;
-(void)killSession:(CdkDesktop *)desktop;
-(void)rollbackDesktop:(CdkDesktop *)desktop;
-(void)logout;


@end // @interface CdkBroker
