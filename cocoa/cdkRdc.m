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
 * cdkRdc.m --
 *
 *      Implementation of CdkRdc.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#include <glib.h>


#import "cdkDesktopSize.h"
#import "cdkPrefs.h"
#import "cdkRdc.h"
#import "cdkString.h"
#include "brokerXml.hh"


static NSString *const RDC_BUNDLE_NAME = @"com.microsoft.rdc";
static NSString *const RDC_BINARY_NAME = @"Remote Desktop Connection";
static NSString *const RDC_KEYCHAIN_FMT =
   @"Remote Desktop Connection 2 Password for %@";

static NSString *const RDC_KEY_CONNECTION_STRING   = @"ConnectionString";
static NSString *const RDC_KEY_DESKTOP_HEIGHT      = @"DesktopHeight";
static NSString *const RDC_KEY_DESKTOP_SIZE        = @"DesktopSize";
static NSString *const RDC_KEY_DESKTOP_WIDTH       = @"DesktopWidth";
static NSString *const RDC_KEY_DISPLAY             = @"Display";
static NSString *const RDC_KEY_DOMAIN              = @"Domain";
static NSString *const RDC_KEY_USER_NAME           = @"UserName";
static NSString *const RDC_VAL_DESKTOP_FULL_SCREEN = @"DesktopFullScreen";


@implementation CdkRdc


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkRdc rdc] --
 *
 *      Create, initialize, and autorelease a new RDC object.
 *
 * Results:
 *      A new RDC object, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkRdc *)rdc
{
   return [[[CdkRdc alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkRdc rdcPath] --
 *
 *      Find RDC 2 on this system.
 *
 * Results:
 *      A string pointing to the RDC binary, or nil if it's not installed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(NSString *)rdcPath
{
   NSString *rdcPath =
      [[NSWorkspace sharedWorkspace] absolutePathForAppBundleWithIdentifier:
                                        RDC_BUNDLE_NAME];
   rdcPath = [rdcPath stringByAppendingPathComponents:
                         @"Contents", @"MacOS", RDC_BINARY_NAME, nil];
   return [[NSFileManager defaultManager] fileExistsAtPath:rdcPath]
      ? rdcPath : nil;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkRdc protocolAvailable] --
 *
 *      Determines whether the user is able to display desktops using
 *      this protocol.
 *
 * Results:
 *      YES if RDC is available.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(BOOL)protocolAvailable
{
   return [CdkRdc rdcPath] != nil;
}


/*
 *-----------------------------------------------------------------------------
 *
 * [CdkRdc unlink] --
 *
 *      Unlink and free our tmp file.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)unlink
{
   if (tmpFile) {
      unlink([tmpFile UTF8String]);
      [tmpFile release];
      tmpFile = nil;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * [CdkRdc createTempFile] --
 *
 *      Create a temporary file for storing the RDP connection
 *      information.
 *
 * Results:
 *      true if a temporary file was created.
 *
 * Side effects:
 *      tmpFile should be unlinked by the caller.
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)createTempFile
{
   ASSERT(!tmpFile);
   GError *gerror = NULL;
   char *file = NULL;
   int fd = g_file_open_tmp("vmware-view-rdc-XXXXXX", &file, &gerror);
   if (gerror) {
      Warning("Could not create temporary file for RDC configuration: %s\n",
              gerror->message);
      g_error_free(gerror);
      g_free(file);
      return NO;
   }
   tmpFile = [[NSString stringWithUTF8String:file] retain];
   g_free(file);
   // Just using that to get a temp file, so close it.
   close(fd);
   return YES;
}


/*
 *-----------------------------------------------------------------------------
 *
 * [CdkRdc createRdcFileForConnection:size:] --
 *
 *      Create an rdc file for connecting to the given host to our
 *      temporary file.  This is based on the template stored in the
 *      user's defaults, which is itself based on the template we ship
 *      as a resource.
 *
 * Results:
 *      true if the file was successfully saved.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)createRdcFileForConnection:(const cdk::BrokerXml::DesktopConnection &)connection // IN
                             size:(CdkDesktopSize *)desktopSize                         // IN
                           screen:(NSScreen *)screen                                    // IN
{
   ASSERT(tmpFile);

   NSMutableDictionary *dict =
      [NSMutableDictionary
         dictionaryWithDictionary:[[CdkPrefs sharedPrefs] rdpSettings]];

   [dict setObject:[NSString stringWithFormat:@"%@:%hu",
                             [NSString stringWithUtilString:connection.address],
                             connection.port]
            forKey:RDC_KEY_CONNECTION_STRING];

   id sizeVal = nil;
   if ([desktopSize fullScreen]) {
      sizeVal = RDC_VAL_DESKTOP_FULL_SCREEN;
   } else {
      NSMutableDictionary *sizeDict =
         [NSMutableDictionary dictionaryWithCapacity:2];
      [sizeDict setObject:[NSNumber numberWithInteger:[desktopSize width]]
                   forKey:RDC_KEY_DESKTOP_WIDTH];
      [sizeDict setObject:[NSNumber numberWithInteger:[desktopSize height]]
                   forKey:RDC_KEY_DESKTOP_HEIGHT];

      sizeVal = sizeDict;
   }
   [dict setObject:sizeVal forKey:RDC_KEY_DESKTOP_SIZE];
   [dict setObject:[NSNumber numberWithInteger:
                                [[NSScreen screens] indexOfObject:screen]]
            forKey:RDC_KEY_DISPLAY];

   [dict setObject:[NSString stringWithUtilString:connection.domainName]
            forKey:RDC_KEY_DOMAIN];
   [dict setObject:[NSString stringWithUtilString:connection.username]
            forKey:RDC_KEY_USER_NAME];

   NSString *error = nil;
   NSData *data = [NSPropertyListSerialization
                     dataFromPropertyList:dict
                                   format:NSPropertyListXMLFormat_v1_0
                         errorDescription:&error];
   if (!data) {
      cdk::Util::UserWarning(_("Could not serialize RDC data: %s.\n"),
                             [error UTF8String]);
      return false;
   }

   NSError *nserror = nil;
   if (![data writeToFile:tmpFile options:0  error:&nserror]) {
      cdk::Util::UserWarning(_("Could not write RDC data: %s.\n"),
                             [[nserror localizedDescription] UTF8String]);
      return false;
   }
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * [CdkRdc storePassword:connection:] --
 *
 *      Save the password for this RDP connection so that MS RDC 2 can
 *      use it to log in.
 *
 * Results:
 *      true if the password was successfully stored in the keychain.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)storePassword:(const char *)secret                                  // IN
       forConnection:(const cdk::BrokerXml::DesktopConnection &)connection // IN
{
   const char *service =
      [[NSString stringWithFormat:RDC_KEYCHAIN_FMT,
                 [NSString stringWithUtilString:connection.address]]
         UTF8String];
   const char *account =
      [[NSString stringWithFormat:@"%@\\%@",
                 [NSString stringWithUtilString:connection.domainName],
                 [NSString stringWithUtilString:connection.username]]
         UTF8String];

   SecKeychainItemRef item;
   OSStatus rv = SecKeychainFindGenericPassword(NULL,
                                                strlen(service), service,
                                                strlen(account), account,
                                                NULL, NULL, &item);
   if (rv == noErr) {
      rv = SecKeychainItemModifyContent(item, NULL, strlen(secret), secret);
      CFRelease(item);
   } else {
      rv = SecKeychainAddGenericPassword(NULL,
                                         strlen(service), service,
                                         strlen(account), account,
                                         strlen(secret), secret, NULL);
   }
   if (rv != noErr) {
      Warning("Could not save password: %d: %s (%s)\n", (int)rv,
              GetMacOSStatusErrorString(rv),
              GetMacOSStatusCommentString(rv));
   }
   return rv == noErr;
}


/*
 *-----------------------------------------------------------------------------
 *
 * [CdkRdc startWithConnection:size:] --
 *
 *      Launch MS RDC by creating an RDP file, and opening that using
 *      open(1).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      RDP app is run.
 *
 *-----------------------------------------------------------------------------
 */

-(void)startWithConnection:(const cdk::BrokerXml::DesktopConnection &)connection // IN
                  password:(const char *)password                                // IN
                      size:(CdkDesktopSize *)desktopSize                         // IN
                    screen:(NSScreen *)screen                                    // IN
{
   NSString *rdcPath = [CdkRdc rdcPath];
   if (!rdcPath) {
      cdk::Util::UserWarning(_("Microsoft Remote Desktop Connection 2 could "
                               "not be found.\n"));
      return;
   }

   if (![self createTempFile]) {
      return;
   }

   Warning("Using temporary RDC file %s.\n", [tmpFile UTF8String]);

   if (![self createRdcFileForConnection:connection
                                    size:desktopSize
                                  screen:screen]) {
      [self unlink];
      return;
   }

   [self storePassword:password
         forConnection:connection];

   [self startWithProcName:rdcPath
                  procPath:rdcPath
                      args:[NSArray arrayWithObjects:tmpFile, nil]];
}


@end // @implementation CdkRdc
