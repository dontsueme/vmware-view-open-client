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
 * cdkPrefs.h --
 *
 *      Global constants for our default settings.
 */

#import <Cocoa/Cocoa.h>


@class CdkDesktopSize;


typedef enum {
    CDK_PREFS_ALL_SCREENS,
    CDK_PREFS_FULL_SCREEN,
    CDK_PREFS_LARGE_WINDOW,
    CDK_PREFS_SMALL_WINDOW,
    CDK_PREFS_CUSTOM_SIZE
} CdkPrefsDesktopSize;


extern NSString *const CDK_PREFS_AUTO_CONNECT;
extern NSString *const CDK_PREFS_DESKTOP_SIZE;
extern NSString *const CDK_PREFS_CUSTOM_DESKTOP_HEIGHT;
extern NSString *const CDK_PREFS_CUSTOM_DESKTOP_WIDTH;
extern NSString *const CDK_PREFS_DOMAIN;
extern NSString *const CDK_PREFS_RDP_SETTINGS;
extern NSString *const CDK_PREFS_RECENT_BROKERS;
extern NSString *const CDK_PREFS_SHOW_BROKER_OPTIONS;
extern NSString *const CDK_PREFS_USER;


@interface CdkPrefs : NSObject {
}


@property BOOL autoConnect;
@property CdkPrefsDesktopSize desktopSize;
@property(copy) CdkDesktopSize *customDesktopSize;
@property(copy) NSString *domain;
@property(readonly) NSDictionary *rdpSettings;
@property(readonly) NSArray *recentBrokers;
@property BOOL showBrokerOptions;
@property(copy) NSString *user;


+(CdkPrefs *)sharedPrefs;
-(void)addRecentBroker:(NSString *)newBroker;


@end // @interface CdkPrefs
