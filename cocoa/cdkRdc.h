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
 * cdkRdc.h --
 *
 *      A ProcHelper wrapper for MS RDC 2.
 */

#import "cdkProcHelper.h"
#include "brokerXml.hh"


@class CdkDesktopSize;


@interface CdkRdc : CdkProcHelper
{
   NSString *tmpFile;
}


+(CdkRdc *)rdc;
+(BOOL)protocolAvailable;

-(void)startWithConnection:(const cdk::BrokerXml::DesktopConnection &)connection
                  password:(const char *)password
                      size:(CdkDesktopSize *)desktopSize
                    screen:(NSScreen *)screen;

@end // @interface CdkRdc
