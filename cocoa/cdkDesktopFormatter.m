/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
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
 * cdkDesktopFormatter.m --
 *
 *      Implementation of CdkDesktopFormatter.
 */

extern "C" {
#include "vm_basic_types.h"
}


#import "cdkDesktop.h"
#import "cdkDesktopFormatter.h"
#import "cdkDesktopsViewController.h"


#import "util.hh"


@implementation CdkDesktopFormatter


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopFormatter desktopFormatter] --
 *
 *      Create and initialize a new desktop formatter.
 *
 * Results:
 *      A new CdkDesktopFormatter.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktopFormatter *)desktopFormatter
{
   return [[[CdkDesktopFormatter alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopFormatter stringForObjectValue:] --
 *
 *      String representation of this object.  If the object is a
 *      CdkDesktop, this is its name.
 *
 * Results:
 *      A string version of the object.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)stringForObjectValue:(id)anObject // IN
{
   if (![anObject isKindOfClass:[CdkDesktop class]]) {
      return [anObject description];
   }
   return [(CdkDesktop *)anObject name];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopFormatter attributedStringForObjectValue:withDefaultAttributes:] --
 *
 *      Create an attributed string for this object.  If it's a
 *      CdkDesktop, we return a two-lined string containing the
 *      desktop's name and its status text.
 *
 * Results:
 *      An attributed string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSAttributedString *)attributedStringForObjectValue:(id)anObject               // IN
                                withDefaultAttributes:(NSDictionary *)attributes // IN
{
   if (![anObject isKindOfClass:[CdkDesktop class]]) {
      return [[[NSAttributedString alloc] initWithString:[anObject description]
                                              attributes:attributes] autorelease];
   }

   CdkDesktop *desktop = (CdkDesktop *)anObject;

   NSMutableDictionary *attrs =
      [NSMutableDictionary dictionaryWithDictionary:attributes];

   [attrs setObject:[NSFont boldSystemFontOfSize:[NSFont systemFontSize]]
             forKey:NSFontAttributeName];

   NSMutableAttributedString *ret =
      [[[NSMutableAttributedString alloc] initWithString:[desktop name]
                                              attributes:attrs] autorelease];

   [attrs setObject:[NSFont labelFontOfSize:[NSFont smallSystemFontSize]]
             forKey:NSFontAttributeName];

   [ret appendAttributedString:
           [[[NSAttributedString alloc] initWithString:@"\n"
                                            attributes:attrs] autorelease]];

   [ret appendAttributedString:
           [[[NSAttributedString alloc] initWithString:[desktop statusText]
                                            attributes:attrs] autorelease]];

   return ret;
}


@end // @implementation CdkDesktopFormatter
