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
 * cdkBrokerAddress.m --
 *
 *      Implementation of CdkBrokerAddress.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#include "baseXml.hh"
#import "cdkBrokerAddress.h"
#include "cdkString.h"
#include "util.hh"


#define PORT_HTTP cdk::BaseXml::PORT_HTTP
#define PORT_HTTP_SSL cdk::BaseXml::PORT_HTTP_SSL


@interface CdkBrokerAddress (Private)
-(void)updateLabel;
-(void)parseLabel;
@end // @interface CdkBrokerAddress (Private)


@implementation CdkBrokerAddress


@synthesize hostname;
@synthesize label;
@synthesize port;
@synthesize secure;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress addressWithHostname:port:secure:] --
 *
 *      Creates a new address representing the given host, port, and
 *      protocol.
 *
 * Results:
 *      An autoreleased object, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkBrokerAddress *)addressWithHostname:(NSString *)hostname // IN
                                    port:(int)port            // IN
                                  secure:(BOOL)secure         // IN
{
   return [[[CdkBrokerAddress alloc] initWithHostname:hostname
                                                 port:port
                                               secure:secure] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress initWithHostname:port:secure:] --
 *
 *      Initializes an address to the given host, port, and protocol.
 *
 * Results:
 *      self or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)initWithHostname:(NSString *)aHostname // IN
                 port:(int)aPort            // IN
               secure:(BOOL)aSecure         // IN
{
   if (!(self = [super init])) {
      return nil;
   }
   [self setHostname:aHostname];
   [self setPort:aPort];
   [self setSecure:aSecure];
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress init] --
 *
 *      Constructor.
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
   return [self initWithHostname:nil port:PORT_HTTP_SSL secure:YES];
}




/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress setHostname:] --
 *
 *      Hostname setter.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Updates label to reflect new hostname.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setHostname:(NSString *)aHostname // IN
{
   /*
    * Check that the hostname is ASCII-only - that it has the same
    * number of chars and bytes (no multibye chars).
    */
   ASSERT([aHostname length] ==
	  [aHostname lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
   if ([hostname isEqualToString:aHostname]) {
      return;
   }
   [hostname release];
   hostname = [aHostname copy];
   [self updateLabel];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress setLabel:] --
 *
 *      Label setter.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Parses label.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setLabel:(NSString *)aLabel // IN
{
   if ([label isEqualToString:aLabel]) {
      return;
   }
   [label release];
   label = [aLabel copy];
   [self parseLabel];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress setPort:] --
 *
 *      Port setter.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Updates the label to include the new port.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setPort:(int)aPort // IN
{
   if (aPort != port) {
      port = aPort;
      [self updateLabel];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress setSecure:] --
 *
 *      Set whether to use SSL.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Updates label and port.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setSecure:(BOOL)aSecure // IN
{
   if (aSecure != secure) {
      secure = aSecure;
      /*
       * Use the new protocol's default port if we had been using the
       * old protocol's default port.
       */
      if (port == (secure ? PORT_HTTP : PORT_HTTP_SSL)) {
	 // Setting the port updates the label, too.
	 [self setPort:secure ? PORT_HTTP_SSL : PORT_HTTP];
      } else {
	 [self updateLabel];
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress (Private) updateLabel] --
 *
 *      Change the displayed label to reflect the current hostname,
 *      port, and protocol.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      label is updated.
 *
 *-----------------------------------------------------------------------------
 */

-(void)updateLabel
{
   cdk::Util::string cppLabel =
      cdk::Util::GetHostLabel([NSString utilStringWithString:hostname], port,
			      secure);
   NSString *s = [NSString stringWithUtilString:cppLabel];

   if ([label isEqualToString:s]) {
      return;
   }
   [self willChangeValueForKey:@"label"];
   [label release];
   label = [s retain];
   [self didChangeValueForKey:@"label"];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress (Private) parseLabel] --
 *
 *      Parse the label into protocol, hostname, and port.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      secure, hostname, and port may be modified.
 *
 *-----------------------------------------------------------------------------
 */

-(void)parseLabel
{
   bool tmpSecure;
   unsigned short tmpPort;
   cdk::Util::string cppHost =
      cdk::Util::ParseHostLabel([NSString utilStringWithString:label],
                                &tmpPort, &tmpSecure);
   NSString *s = [NSString stringWithUtilString:cppHost];
   if (![hostname isEqualToString:s]) {
      [self willChangeValueForKey:@"hostname"];
      [hostname release];
      hostname = [s retain];
      [self didChangeValueForKey:@"hostname"];
   }
   // If the hostname didn't parse, ignore the new port/secure.
   if ([s length]) {
      if (secure != tmpSecure) {
	 [self willChangeValueForKey:@"secure"];
	 secure = tmpSecure;
	 [self didChangeValueForKey:@"secure"];
      }
      if (port != tmpPort) {
	 [self willChangeValueForKey:@"port"];
	 port = tmpPort;
	 [self didChangeValueForKey:@"port"];
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkBrokerAddress description] --
 *
 *      A string representation of this server.
 *
 * Results:
 *      A string representing this server.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)description
{
   return [secure ? @"https://" : @"" stringByAppendingString:label];
}


@end // @implementation CdkBrokerAddress
