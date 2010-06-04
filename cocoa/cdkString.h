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
 * cdkString.h --
 *
 *      Conversions between cdk::Util::string and NSString.
 */

#import <Cocoa/Cocoa.h>


#import "util.hh"


#define NS_(s) [NSString stringWithUTF8String:_(s)]

@interface NSString (CdkString)


+(NSString *)stringWithUtilString:(const cdk::Util::string &)utilString;
+(NSString *)stringWithUTF8Format:(const char *)format, ...;
+(cdk::Util::string)utilStringWithString:(NSString *)string;

-(id)initWithUtilString:(const cdk::Util::string &)utilString;
-(NSString *)stringByAppendingPathComponents:(NSString *)string, ...;

@end // @interface NSString (CdkString)
