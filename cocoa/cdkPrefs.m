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
 * cdkPrefs.m --
 *
 *      Implementation of CdkPrefs.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import "cdkDesktopSize.h"
#import "cdkPrefs.h"
#include "util.hh"


NSString *const CDK_PREFS_AUTO_CONNECT          = @"autoConnect";
NSString *const CDK_PREFS_DESKTOP_SIZE          = @"desktopSize";
NSString *const CDK_PREFS_CUSTOM_DESKTOP_HEIGHT = @"customDesktopHeight";
NSString *const CDK_PREFS_CUSTOM_DESKTOP_WIDTH  = @"customDesktopWidth";
NSString *const CDK_PREFS_DOMAIN                = @"domain";
NSString *const CDK_PREFS_RDP_SETTINGS          = @"rdpSettings";
NSString *const CDK_PREFS_RECENT_BROKERS        = @"recentBrokers";
NSString *const CDK_PREFS_SHOW_BROKER_OPTIONS   = @"showBrokerOptions";
NSString *const CDK_PREFS_USER                  = @"user";


static unsigned int const BROKERS_MRU_LEN = 10;
static int const MIN_DESKTOP_WIDTH = 640;
static int const MIN_DESKTOP_HEIGHT = 480;


@interface CdkPrefs ()


+(BOOL)validDesktopSize:(CdkPrefsDesktopSize)desktopSize;


@end // @interface CdkPrefs ()


@implementation CdkPrefs


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkPrefs sharedPrefs] --
 *
 *      Gets the shared CdkPrefs instance, instantiating it if
 *      necessary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Creates gPrefs object.
 *
 *-----------------------------------------------------------------------------
 */

+(CdkPrefs *)sharedPrefs
{
   static CdkPrefs *gPrefs = nil;
   if (gPrefs == nil) {
      gPrefs = [[CdkPrefs alloc] init];
   }
   return gPrefs;
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkPrefs initialize] --
 *
 *      Initializes our default prefs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(void)initialize
{
   NSMutableDictionary *defaults = [NSMutableDictionary dictionary];

   [defaults setObject:NSUserName() forKey:CDK_PREFS_USER];
   [defaults setObject:[NSNumber numberWithInt:CDK_PREFS_LARGE_WINDOW]
                forKey:CDK_PREFS_DESKTOP_SIZE];
   [defaults setObject:[NSNumber numberWithInt:MIN_DESKTOP_HEIGHT]
                forKey:CDK_PREFS_CUSTOM_DESKTOP_HEIGHT];
   [defaults setObject:[NSNumber numberWithInt:MIN_DESKTOP_WIDTH]
                forKey:CDK_PREFS_CUSTOM_DESKTOP_WIDTH];
   [defaults setObject:[NSNumber numberWithBool:NO]
	     forKey:CDK_PREFS_SHOW_BROKER_OPTIONS];

   /*
    * The RDP defaults are stored as a file to allow admins to tweak
    * the defaults.
    */
   NSString *templateFile = [[NSBundle mainBundle]
                               pathForResource:@"vmware-view" ofType:@"rdp"];

   NSError *nserror = nil;
   NSData *data = [NSData dataWithContentsOfFile:templateFile options:0
                          error:&nserror];
   if (!data) {
       cdk::Util::UserWarning(_("Could not load RDC template data: %s.\n"),
                              [[nserror localizedDescription] UTF8String]);
   } else {
      NSString *error = nil;
      [defaults setObject:
                   [NSPropertyListSerialization propertyListFromData:data
                                                mutabilityOption:0
                                                format:NULL
                                                errorDescription:&error]
                forKey:CDK_PREFS_RDP_SETTINGS];
      if (error) {
         cdk::Util::UserWarning(_("Could not parse RDC template data: %s.\n"),
                                [error UTF8String]);
      }
   }

   [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs autoConnect] --
 *
 *      Auto Connect: whether to connect to the default broker at
 *      startup.
 *
 * Results:
 *      YES if autoConnect is enabled.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)autoConnect
{
   return [[NSUserDefaults standardUserDefaults]
	     boolForKey:CDK_PREFS_AUTO_CONNECT];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs setAutoConnect:] --
 *
 *      Setter for autoConnect prefs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setAutoConnect:(BOOL)newAutoConnect // IN
{
   [[NSUserDefaults standardUserDefaults] setBool:newAutoConnect
					  forKey:CDK_PREFS_AUTO_CONNECT];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs desktopSize] --
 *
 *      The default size type.
 *
 * Results:
 *      The height (in pixels) to launch remote desktops in, or -1 for
 *      full screen.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(CdkPrefsDesktopSize)desktopSize
{
   CdkPrefsDesktopSize size =
      (CdkPrefsDesktopSize)[[NSUserDefaults standardUserDefaults]
                              integerForKey:CDK_PREFS_DESKTOP_SIZE];
   if (![CdkPrefs validDesktopSize:size]) {
      Warning("Invalid size preference: %u; defaulting to large window.\n", size);
      size = CDK_PREFS_LARGE_WINDOW;
   }
   if (size == CDK_PREFS_CUSTOM_SIZE && ![self customDesktopSize]) {
      Warning("Size set to custom, but no custom size available; defaulting "
              "to large window.\n");
      size = CDK_PREFS_LARGE_WINDOW;
   }
   return size;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs setDesktopSize:] --
 *
 *      Save the desktop height.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setDesktopSize:(CdkPrefsDesktopSize)newSize // IN
{
   ASSERT([CdkPrefs validDesktopSize:newSize]);
   if (newSize == CDK_PREFS_CUSTOM_SIZE) {
      ASSERT([self customDesktopSize]);
   }
   [[NSUserDefaults standardUserDefaults] setInteger:newSize
                                              forKey:CDK_PREFS_DESKTOP_SIZE];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs customDesktopSize]
 *
 *      Get the custom desktop size, or nil if no size has been saved.
 *
 * Results:
 *      The width (in pixels) to launch remote desktops in, or -1 for
 *      full screen.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(CdkDesktopSize *)customDesktopSize
{
   int width;
   int height;
   width = [[NSUserDefaults standardUserDefaults]
                                integerForKey:CDK_PREFS_CUSTOM_DESKTOP_WIDTH];
   height = [[NSUserDefaults standardUserDefaults]
                                 integerForKey:CDK_PREFS_CUSTOM_DESKTOP_HEIGHT];
   // Just ignore invalid sizes.
   if (width >= MIN_DESKTOP_WIDTH && height >= MIN_DESKTOP_HEIGHT) {
      return [CdkDesktopSize desktopSizeWithWidth:width height:height];
   }
   Warning("Invalid desktop size found: %dx%d.\n", width, height);
   return nil;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs setDesktopWidth:] --
 *
 *      Saves the desktop width.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Also sets the preferred desktop size to custom.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setCustomDesktopSize:(CdkDesktopSize *)newSize // IN
{
   [[NSUserDefaults standardUserDefaults]
      setInteger:[newSize width] forKey:CDK_PREFS_CUSTOM_DESKTOP_WIDTH];
   [[NSUserDefaults standardUserDefaults]
      setInteger:[newSize height] forKey:CDK_PREFS_CUSTOM_DESKTOP_HEIGHT];
   [self setDesktopSize:CDK_PREFS_CUSTOM_SIZE];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs domain] --
 *
 *      Get the default domain used to authenticate with.
 *
 * Results:
 *      The most recently used ActiveDirectory domain.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)domain
{
   return [[NSUserDefaults standardUserDefaults]
	     stringForKey:CDK_PREFS_DOMAIN];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs setDomain:] --
 *
 *      Store the ActiveDirectory domain used to authenticate.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setDomain:(NSString *)newDomain // IN
{
   [[NSUserDefaults standardUserDefaults] setObject:newDomain
					  forKey:CDK_PREFS_DOMAIN];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs rdpSettings] --
 *
 *      Get the default settings for RDP connections.
 *
 * Results:
 *      A dictionary which, if saved to a file, can be used to launch
 *      MS RDC 2.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSDictionary *)rdpSettings
{
   return [[NSUserDefaults standardUserDefaults]
             dictionaryForKey:CDK_PREFS_RDP_SETTINGS];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs recentBrokers] --
 *
 *      Get the list of recently used broker servers.
 *
 * Results:
 *      An array of NSString *, or nil.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSArray *)recentBrokers
{
   return [[NSUserDefaults standardUserDefaults]
	     stringArrayForKey:CDK_PREFS_RECENT_BROKERS];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs addRecentBroker:] --
 *
 *      Add a broker to the list of recently used brokers, and save
 *      that list.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)addRecentBroker:(NSString *)newBroker // IN
{
   if (![newBroker length]) {
      return;
   }
   NSMutableArray *newBrokers = [NSMutableArray arrayWithArray:[self recentBrokers]];

   [newBrokers removeObject:newBroker];
   [newBrokers insertObject:newBroker atIndex:0];
   if ([newBrokers count] > BROKERS_MRU_LEN) {
      [newBrokers removeObjectsInRange:
		     NSMakeRange(BROKERS_MRU_LEN,
				 [newBrokers count] - BROKERS_MRU_LEN)];
   }
   [self willChangeValueForKey:@"recentBrokers"];
   [[NSUserDefaults standardUserDefaults] setObject:newBrokers
					  forKey:CDK_PREFS_RECENT_BROKERS];
   [self didChangeValueForKey:@"recentBrokers"];
}



/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs showBrokerOptions] --
 *
 *      Whether the advanced options are displayed on the broker page.
 *
 * Results:
 *      YES if advanced broker options should be shown.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)showBrokerOptions
{
   return [[NSUserDefaults standardUserDefaults]
	     boolForKey:CDK_PREFS_SHOW_BROKER_OPTIONS];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs setShowBrokerOptions:] --
 *
 *      Sets whether the advanced options should be displayed on the
 *      broker page.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setShowBrokerOptions:(BOOL)newShowOptions // IN
{
   [[NSUserDefaults standardUserDefaults] setBool:newShowOptions
					  forKey:CDK_PREFS_SHOW_BROKER_OPTIONS];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs user] --
 *
 *      Accessor for the default user to log in as.
 *
 * Results:
 *      String containing the user name.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(NSString *)user
{
   return [[NSUserDefaults standardUserDefaults] stringForKey:CDK_PREFS_USER];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs setUser:] --
 *
 *      Set the user to use when authenticating.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)setUser:(NSString *)newUser // IN
{
   [[NSUserDefaults standardUserDefaults] setObject:newUser
					  forKey:CDK_PREFS_USER];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkPrefs validDesktopSize:] --
 *
 *      Determine whether a given size is valid.
 *
 * Results:
 *      YES if the size is valid for this client.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(BOOL)validDesktopSize:(CdkPrefsDesktopSize)desktopSize // IN
{
   // ALL_SCREENS is not implemented on Mac (yet).
   return desktopSize >= CDK_PREFS_FULL_SCREEN &&
      desktopSize <= CDK_PREFS_CUSTOM_SIZE;
}


@end // @implementation CdkPrefs
