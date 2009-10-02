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
 *      Implementation for cdkString.h
 */


#import "cdkString.h"
#import "util.hh"


@implementation NSString (CdkString)


/*
 *-----------------------------------------------------------------------------
 *
 * +[NSString (CdkString) stringWithUtilString:] --
 *
 *      Create an NSString from a cdk::Util::string.
 *
 * Results:
 *      A new NSString.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(NSString *)stringWithUtilString:(const cdk::Util::string &)utilString // IN
{
   return [NSString stringWithUTF8String:utilString.c_str()];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkString initWithUtilString] --
 *
 *      Initialize a string with the contents of a cdk::Util::string.
 *
 * Results:
 *      The string object.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)initWithUtilString:(const cdk::Util::string &)utilString // IN
{
   return [self initWithUTF8String:utilString.c_str()];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[NSString (CdkString) utilString] --
 *
 *      Create a cdk::Util::string from an NSString.
 *
 * Results:
 *      A new cdk::Util::string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(cdk::Util::string)utilString
{
   return [self UTF8String];
}


@end // @implementation NSString (CdkString)
