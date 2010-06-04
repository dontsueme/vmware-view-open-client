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
 * cdkDesktop.m --
 *
 *      Implementation CdkDesktop.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import <boost/bind.hpp>


#import "cdkDesktop.h"
#import "cdkDesktopSize.h"
#import "cdkProcHelper.h"
#import "cdkString.h"
#import "desktop.hh"


@interface CdkDesktop (Private)
-(cdk::Desktop *)adaptedDesktop;
-(BOOL)busy;
@end // @interface CdkDesktop (Private)


static NSString *const KEY_PATH_CAN_CONNECT = @"canConnect";
static NSString *const KEY_PATH_HAS_SESSION = @"hasSession";
static NSString *const KEY_PATH_CAN_RESET = @"canReset";
static NSString *const KEY_PATH_CHECKED_OUT = @"checkedOut";
static NSString *const KEY_PATH_STATUS = @"status";
static NSString *const KEY_PATH_STATUS_TEXT = @"statusText";


/*
 *-----------------------------------------------------------------------------
 *
 * OnDesktopStateChanged --
 *
 *      Callback for a desktop's state change signal; notify key-value
 *      coding that the canConnect property changed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
OnDesktopStateChanged(CdkDesktop *self)
{
   NSString *keys[] = {
      KEY_PATH_CAN_CONNECT,
      KEY_PATH_HAS_SESSION,
      KEY_PATH_CAN_RESET,
      KEY_PATH_CHECKED_OUT,
      KEY_PATH_STATUS,
      KEY_PATH_STATUS_TEXT,
      nil
   };
   for (NSString **key = keys; *key; key++) {
      [self willChangeValueForKey:*key];
      [self didChangeValueForKey:*key];
   }
}


@implementation CdkDesktop


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkDesktop desktopWithDesktop:] --
 *
 *      Creates and returns a desktop wrapper object.
 *
 * Results:
 *      A new wrapper object.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktop *)desktopWithDesktop:(cdk::Desktop *)desktop // IN
{
   return [[[CdkDesktop alloc] initWithDesktop:desktop] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop initWithDesktop:] --
 *
 *      Initializer; sets the C++ object we are wrapping.
 *
 * Results:
 *      self.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)initWithDesktop:(cdk::Desktop *)desktop;
{
   if ((self = [super init])) {
      mDesktop = desktop;
      mStateChangedCnx = new boost::signals::connection(
         mDesktop->changed.connect(boost::bind(OnDesktopStateChanged, self)));
   }
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop dealloc] --
 *
 *      Release our resources.
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
   // We don't control the lifetime of mDesktop, so don't delete it.

   mStateChangedCnx->disconnect();
   delete mStateChangedCnx;

   [uiProcHelper release];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop (Private) adaptedDesktop] --
 *
 *      Getter for mDesktop.
 *
 * Results:
 *      The wrapped cdk::Desktop object.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(cdk::Desktop *)adaptedDesktop
{
   return mDesktop;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop name] --
 *
 *      Getter for desktop name.
 *
 * Results:
 *      A NSString of the desktop's name.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)name
{
   return [NSString stringWithUtilString:mDesktop->GetName()];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop protocol] --
 *
 *      Gets the selected protocol for this desktop.
 *
 * Results:
 *      An NSString.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)protocol
{
   return [NSString stringWithUtilString:mDesktop->GetProtocol()];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop setProtocol:] --
 *
 *      Sets the protocol for this desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setProtocol:(NSString *)proto
{
   mDesktop->SetProtocol([NSString utilStringWithString:proto]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop busy] --
 *
 *      Gets whether the desktop is busy (their state is something
 *      other than disconnected).
 *
 * Results:
 *      YES the desktop is "busy."
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)busy
{
   return mDesktop->GetConnectionState() != cdk::Desktop::STATE_DISCONNECTED;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop canConnect] --
 *
 *      Getter for canConnect property.
 *
 * Results:
 *      YES if the desktop is in a state that allows a connection.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)canConnect
{
   return mDesktop->CanConnect();
}



/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop hasSession] --
 *
 *      Getter for hasSession property.
 *
 * Results:
 *      YES if this desktop has a running session.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)hasSession
{
   return ![self busy] && !mDesktop->GetSessionID().empty();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop canReset] --
 *
 *      Getter for canReset property.
 *
 * Results:
 *      YES if the user is allowed to reset this desktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)canReset
{
   return ![self busy] && mDesktop->CanReset();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop checkedOut] --
 *
 *      Getter for checkedOut property.
 *
 * Results:
 *      YES if this desktop is currently checked out.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)checkedOut
{
   return ![self busy] &&
      mDesktop->GetOfflineState() == cdk::BrokerXml::OFFLINE_CHECKED_OUT;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop status] --
 *
 *      Return the desktop's status.
 *
 * Results:
 *      Desktop's status (really a cdk::Desktop::Status).
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(int)status
{
   return mDesktop->GetStatus();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop statusText] --
 *
 *      Get the appropriate status text for this desktop.
 *
 * Results:
 *      The text corresponding with this object's status.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)statusText
{
   return [NSString stringWithUtilString:mDesktop->GetStatusMsg(false)];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop copyWithZone:] --
 *
 *      Implement NSCopying by retaining ourself - we're simply a
 *      wrapper around the C++ object, and don't really own that
 *      reference anyway.
 *
 * Results:
 *      This object.
 *
 * Side effects:
 *      Self is retained.
 *
 *-----------------------------------------------------------------------------
 */

-(id)copyWithZone:(NSZone *)zone // IN
{
   return [self retain];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktop disconnect] --
 *
 *      Disconnect from the remote desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop is disconnected.
 *
 *-----------------------------------------------------------------------------
 */

-(void)disconnect
{
   mDesktop->Disconnect();
}


@end // @implementation CdkDesktop
