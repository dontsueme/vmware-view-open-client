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
 * cdkDesktopCell.m --
 *
 *      Implementation of CdkDesktopCell.
 */

#import "cdkDesktop.h"
#import "cdkDesktopCell.h"


#include "desktop.hh"
#include "util.hh"


@implementation CdkDesktopCell


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkDesktopCell desktopCell] --
 *
 *      Creates and returns a new desktop cell.
 *
 * Results:
 *      A new cell.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkDesktopCell *)desktopCell
{
   return [[[CdkDesktopCell alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell dealloc] --
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
   [desktopLocalRollback release];
   [desktopLocalDisabled release];
   [desktopCheckinPause release];
   [desktopCheckoutPause release];
   [desktopLocal release];
   [desktopRemoteDisabled release];
   [desktopRemote release];
   [imageCell release];
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell setControlView:] --
 *
 *      Override our super class to also set the control view on our
 *      image cell.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setControlView:(NSView *)controlView // IN
{
   [super setControlView:controlView];
   [imageCell setControlView:controlView];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell setBackgroundStyle:] --
 *
 *      Override our super class to also set the style on our image
 *      cell.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setBackgroundStyle:(NSBackgroundStyle)style // IN
{
   [super setBackgroundStyle:style];
   [imageCell setBackgroundStyle:style];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell imageFrameForInteriorFrame:] --
 *
 *      Offset the image's frame from our frame.
 *
 * Results:
 *      Bounds the icon should be drawn in.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSRect)imageFrameForInteriorFrame:(NSRect)frame // IN
{
   frame.origin.x += VM_SPACING / 2;
   frame.origin.y += VM_SPACING;
   frame.size.height -= 2 * VM_SPACING;
   frame.size.width = frame.size.height;
   return frame;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell textFrameForInteriorFrame:] --
 *
 *      Calculate where the text should be drawn.
 *
 * Results:
 *      Bounds the text should be drawn in.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSRect)textFrameForInteriorFrame:(NSRect)frame // IN
{
   NSRect imageFrame = [self imageFrameForInteriorFrame:frame];
   NSRect res = frame;
   res.origin.x = NSMaxX(imageFrame) + VM_SPACING / 2;
   res.size.width = NSMaxX(frame) - NSMinX(res);
   NSSize naturalSize = [super cellSize];
   // Centered vertically
   res.origin.y = floor(NSMidY(imageFrame) - naturalSize.height / 2);
   res.size.height = naturalSize.height;
   return res;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell statusImage] --
 *
 *      Get the appropriate icon for the desktop's state.
 *
 * Results:
 *      An icon or nil.
 *
 * Side effects:
 *      Icons may be loaded from Resources.
 *
 *-----------------------------------------------------------------------------
 */

-(NSImage *)statusImage
{
   if (![[self objectValue] isKindOfClass:[CdkDesktop class]]) {
      return nil;
   }

#define RETURN_ICON(v, f)                          \
   G_STMT_START {                               \
      if (!v) {                                 \
         v = [[NSImage imageNamed:f] retain];   \
      }                                         \
      return v;                                 \
   } G_STMT_END

   switch ([(CdkDesktop *)[self objectValue] status]) {
   case cdk::Desktop::STATUS_ROLLING_BACK:
   case cdk::Desktop::STATUS_SERVER_ROLLBACK:
   case cdk::Desktop::STATUS_HANDLING_SERVER_ROLLBACK:
      RETURN_ICON(desktopLocalRollback, @"desktop_local_rollback_32x.png");
   case cdk::Desktop::STATUS_CHECKED_OUT_DISABLED:
      RETURN_ICON(desktopLocalDisabled, @"desktop_local32xdisabled.png");
   case cdk::Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN:
      // XXX: Need to add an icon for when we are offline.
      RETURN_ICON(desktopCheckinPause, @"desktop_checkin_pause32x.png");
   case cdk::Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT:
      // XXX: Need to add an icon for when we are offline.
      RETURN_ICON(desktopCheckoutPause, @"desktop_checkout_pause32x.png");
   case cdk::Desktop::STATUS_AVAILABLE_LOCAL:
      RETURN_ICON(desktopLocal, @"desktop_local32x.png");
   case cdk::Desktop::STATUS_LOGGED_ON:
   case cdk::Desktop::STATUS_AVAILABLE_REMOTE:
      if (YES /* XXX: online */) {
         RETURN_ICON(desktopRemote, @"desktop_remote32x.png");
      }
      break;
   default:
      break;
   }

   RETURN_ICON(desktopRemoteDisabled, @"desktop_remote32x_disabled.png");
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell drawInteriorWithFrame:inView:] --
 *
 *      Actually draw the cell!  In reality, we rely entirely on our
 *      image cell and our parent to do the actual drawing.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May create imageCell.
 *
 *-----------------------------------------------------------------------------
 */

-(void)drawInteriorWithFrame:(NSRect)frame         // IN
                      inView:(NSView *)controlView // IN
{
   if (!imageCell) {
      imageCell = [[NSImageCell alloc] init];
      [imageCell setControlView:[self controlView]];
      [imageCell setBackgroundStyle:[self backgroundStyle]];
   }

   [imageCell setImage:[self statusImage]];
   [imageCell drawWithFrame:[self imageFrameForInteriorFrame:frame]
                     inView:controlView];
   [super drawInteriorWithFrame:[self textFrameForInteriorFrame:frame]
                         inView:controlView];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkDesktopCell copyWithZone:] --
 *
 *      Copy this object; we'll lazily make a new image cell, but we
 *      may as well just retain the images since we don't modify them.
 *
 * Results:
 *      A new object.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)copyWithZone:(NSZone *)zone // IN
{
   CdkDesktopCell *cell = [super copyWithZone:zone];
   if (cell) {
      cell->imageCell = nil;

      cell->desktopLocalRollback = [desktopLocalRollback retain];
      cell->desktopLocalDisabled = [desktopLocalDisabled retain];
      cell->desktopCheckinPause = [desktopCheckinPause retain];
      cell->desktopCheckoutPause = [desktopCheckoutPause retain];
      cell->desktopLocal = [desktopLocal retain];
      cell->desktopRemoteDisabled = [desktopRemoteDisabled retain];
      cell->desktopRemote = [desktopRemote retain];
   }
   return cell;
}


@end // @implementation CdkDesktopCell
