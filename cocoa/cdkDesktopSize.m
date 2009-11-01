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
 * cdkDesktopSize.m --
 *
 *      Implementation of CdkDesktopSize.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#include "util.hh"


#import "cdkDesktopSize.h"


@implementation CdkDesktopSize

@synthesize width;
@synthesize height;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSize initWithWidth:height:] --
 *
 *      Initialize a desktop size.
 *
 * Results:
 *      self.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */


-(id)initWithWidth:(int)aWidth  // IN
	    height:(int)aHeight // IN
{
   if (!(self = [super init])) {
      return nil;
   }
   width = aWidth;
   height = aHeight;
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkDesktopSize desktopSizeWithWidth:height:] --
 *
 *      Static, autoreleased initializer for a CdkDesktopSize.
 *
 * Results:
 *      An autoreleased CdkDesktopSize object.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktopSize *)desktopSizeWithWidth:(int)aWidth  // IN
                                 height:(int)aHeight // IN
{
   return [[[CdkDesktopSize alloc]
	      initWithWidth:aWidth height:aHeight] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSize desktopSizeForFullScreen] --
 *
 *      Create an auto-released desktop size representing a full
 *      screen.
 *
 * Results:
 *      An auto-released CdkDesktopSize, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktopSize *)desktopSizeForFullScreen
{
   return [CdkDesktopSize desktopSizeWithWidth:cdk::Util::FULL_SCREEN
                                        height:cdk::Util::FULL_SCREEN];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSize fullScreen] --
 *
 *      Determine if this size represents a full screen.  Mac doesn't
 *      support multiple monitors yet, so just count that as full screen.
 *
 * Results:
 *      YES if this desktop should be displayed full screen.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)fullScreen
{
   return width == cdk::Util::FULL_SCREEN ||
      width == cdk::Util::ALL_SCREENS ||
      height == cdk::Util::FULL_SCREEN ||
      height == cdk::Util::ALL_SCREENS;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSize description] --
 *
 *      A textual description of this object.
 *
 * Results:
 *      A string such as "800 x 600".
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)description
{
   if ([self fullScreen]) {
      return [NSString stringWithString:@"Full Screen"];
   }
   return [NSString stringWithFormat:@"%d x %d", width, height];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSize isEqual:] --
 *
 *      Overrides -[NSObject isEqual:]; determines equivalence for two
 *      desktop sizes.
 *
 * Results:
 *      YES if height and width for both sizes are equal; NO otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)isEqual:(id)anObject // IN
{
   if ([anObject isKindOfClass:[CdkDesktopSize class]]) {
      CdkDesktopSize *size = (CdkDesktopSize *)anObject;
      return [self width] == [size width] &&
         [self height] == [size height];
   }
   return NO;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopSize copyWithZone:] --
 *
 *      Returns a copy of this desktop size object.
 *
 * Results:
 *      A desktop size object that equals this one.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)copyWithZone:(NSZone *)zone // IN
{
   return [[CdkDesktopSize allocWithZone:zone] initWithWidth:width
                                                      height:height];
}


@end // @implemntation CdkDesktopSize
