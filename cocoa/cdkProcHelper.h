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
 * cdkProcHelper.h --
 *
 *      Objective-C wrapper for the cdk::ProcHelper C++ object.
 */


#import <Cocoa/Cocoa.h>


namespace cdk {
   class ProcHelper;
   class ProcHelperConnections;
}


@class CdkProcHelper;


@protocol CdkProcHelperDelegate
-(void)procHelper:(CdkProcHelper *)procHelper
didExitWithStatus:(int)status;

-(void)procHelper:(CdkProcHelper *)procHelper
    didWriteError:(NSString *)error;
@end // @protocol CdkProcHelperDelegate


@interface CdkProcHelper : NSObject
{
   cdk::ProcHelper *mProcHelper;
   cdk::ProcHelperConnections *mConnections;
   id<CdkProcHelperDelegate> delegate;
   BOOL ownsHelper;
}


@property(readonly) BOOL running;
@property(readonly) pid_t pid;
@property(assign) id<CdkProcHelperDelegate> delegate;


+(CdkProcHelper *)procHelper;
+(CdkProcHelper *)procHelperWithProcHelper:(cdk::ProcHelper *)procHelper;


-(id)initWithProcHelper:(cdk::ProcHelper *)procHelper;

-(void)startWithProcName:(NSString *)procName
		procPath:(NSString *)procPath
		    args:(NSArray *)args;
-(void)kill;


@end // @interface CdkProcHelper
