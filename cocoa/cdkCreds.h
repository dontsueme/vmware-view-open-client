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
 * cdkCreds.h --
 *
 *      Basic credentials - a username and a secret.
 */

#import <Cocoa/Cocoa.h>


#import "cdkChangePinCreds.h"
#import "cdkChangeWinCreds.h"
#import "cdkConfirmPinCreds.h"
#import "cdkPasscodeCreds.h"
#import "cdkTokencodeCreds.h"
#import "cdkWinCreds.h"


@interface CdkCreds : NSObject <CdkChangePinCreds, CdkChangeWinCreds,
                                CdkConfirmPinCreds, CdkPasscodeCreds,
                                CdkTokencodeCreds, CdkWinCreds>
{
   NSString *username;
   NSString *domain;
   NSArray *domains;
   NSString *secret;
   NSString *confirm;
   NSString *oldSecret;
   NSString *label;
   BOOL savePassword;
   BOOL userSelectable;
}


@property(copy) NSString *username;
@property(copy) NSString *domain;
@property(copy) NSArray *domains;
@property(copy) NSString *secret;
@property(copy) NSString *confirm;
@property(copy) NSString *oldSecret;
@property(copy) NSString *label;
@property BOOL savePassword;
@property BOOL userSelectable;
@property(readonly) BOOL upnUsername;


+(CdkCreds *)creds;


-(BOOL)validChangePinCreds;
-(BOOL)validChangeWinCreds;
-(BOOL)validConfirmPinCreds;
-(BOOL)validPasscodeCreds;
-(BOOL)validTokencodeCreds;
-(BOOL)validWinCreds;


@end // @interface CdkCreds
