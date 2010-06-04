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
 * cdkDesktopCell.h --
 *
 *      A NSCell for displaying a desktop's status icon.
 */

#import <Cocoa/Cocoa.h>


@interface CdkDesktopCell : NSTextFieldCell
{
   NSImageCell *imageCell;

   NSImage *desktopLocalRollback;
   NSImage *desktopLocalDisabled;
   NSImage *desktopCheckinPause;
   NSImage *desktopCheckoutPause;
   NSImage *desktopLocal;
   NSImage *desktopRemoteDisabled;
   NSImage *desktopRemote;
}


+(CdkDesktopCell *)desktopCell;


@end // @interface CdkDesktopCell
