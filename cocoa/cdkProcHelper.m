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
 * cdkProcHelper.m --
 *
 *      Implementation of CdkProcHelper.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import <boost/bind.hpp>


#import "cdkProcHelper.h"
#import "cdkString.h"
#import "procHelper.hh"


@interface CdkProcHelper ()
-(cdk::ProcHelper *)adaptedProcHelper;
@end // @interface CdkProcHelper ()


namespace cdk {


class ProcHelperConnections
{
public:
   ProcHelperConnections(CdkProcHelper *procHelper);
   ~ProcHelperConnections();

private:
   void OnExit(int status);
   void OnErr(Util::string error);

   boost::signals::connection mExitCnx;
   boost::signals::connection mErrCnx;

   CdkProcHelper *mProcHelper;
};


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProchelperConnections::ProcHelperConnections --
 *
 *      Constructor - bind our connections to the real proc helpers
 *      signals.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ProcHelperConnections::ProcHelperConnections(CdkProcHelper *procHelper) // IN
   : mProcHelper(procHelper)
{
   ProcHelper *proc = [mProcHelper adaptedProcHelper];
   mExitCnx = proc->onExit.connect(boost::bind(&ProcHelperConnections::OnExit,
                                               this, _1));
   mErrCnx = proc->onErr.connect(boost::bind(&ProcHelperConnections::OnErr,
                                             this, _1));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelperConnections::~ProcHelperConnections --
 *
 *      Destructor - disconnect our connections.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ProcHelperConnections::~ProcHelperConnections()
{
   mExitCnx.disconnect();
   mErrCnx.disconnect();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelperConnections::OnExit --
 *
 *      Exit callback handler.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Calls delegate procHelper:didExitWithStatus: method.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelperConnections::OnExit(int status) // IN
{
   ASSERT(mProcHelper);
   [[mProcHelper delegate] procHelper:mProcHelper
                    didExitWithStatus:status];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelperConnections::OnErr --
 *
 *      Error output callback handler.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Calls delegate procHelper:didWriteError: method.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelperConnections::OnErr(Util::string error) // IN
{
   ASSERT(mProcHelper);
   [[mProcHelper delegate] procHelper:mProcHelper
                        didWriteError:[NSString stringWithUtilString:error]];
}


} // namespace cdk


@implementation CdkProcHelper


@synthesize delegate;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper procHelper] --
 *
 *      Create, intialize, and autorelease a CdkProcHelper.
 *
 * Results:
 *      A new CdkProcHelper, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkProcHelper *)procHelper
{
   return [[[CdkProcHelper alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper procHelperWithProcHelper:] --
 *
 *      Create, initialize, and autorelease a CdkProcHelper which
 *      pwraps the givven cdk::ProcHelper.
 *
 * Results:
 *      A new CdkProcHelper, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkProcHelper *)procHelperWithProcHelper:(cdk::ProcHelper *)proc // IN
{
   return [[[CdkProcHelper alloc] initWithProcHelper:proc] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper initWithProcHelper:] --
 *
 *      Initialize a ProcHelper wrapper object.
 *
 * Results:
 *      self or nil on error.
 *
 * Side effects:
 *      Connections to procHelper are created.
 *
 *-----------------------------------------------------------------------------
 */

-(id)initWithProcHelper:(cdk::ProcHelper *)procHelper // IN
{
   ASSERT(procHelper);
   if (!(self = [super init])) {
      return nil;
   }
   mProcHelper = procHelper;
   mConnections = new cdk::ProcHelperConnections(self);
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper init] --
 *
 *      Initialize a ProcHelper wrapper object with a new
 *      cdk::ProcHelper.
 *
 * Results:
 *      self or nil.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   cdk::ProcHelper *proc = new cdk::ProcHelper();
   if (!(self = [self initWithProcHelper:proc])) {
      delete proc;
      return self;
   }
   ownsHelper = TRUE;
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper dealloc] --
 *
 *      Release the resources associated with this object.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Connections are disconnected.
 *
 *-----------------------------------------------------------------------------
 */

-(void)dealloc
{
   delete mConnections;
   if (ownsHelper) {
      delete mProcHelper;
   }
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper () adaptedProcHelper] --
 *
 *      Getter for the wrapped C++ object.
 *
 * Results:
 *      The C++ object this object is wrapping.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(cdk::ProcHelper *)adaptedProcHelper
{
   return mProcHelper;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper startWithProcName:procPath:args:] --
 *
 *      Start the proc helper with the given paths and arguments.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Process is created.
 *
 *-----------------------------------------------------------------------------
 */

-(void)startWithProcName:(NSString *)procName // IN
                procPath:(NSString *)procPath // IN
                    args:(NSArray *)args      // IN
{
   std::vector<cdk::Util::string> stdArgs;
   for (unsigned int i = 0; i < [args count]; i++) {
      stdArgs.push_back(
         [NSString utilStringWithString:[args objectAtIndex:i]]);
   }
   mProcHelper->Start([NSString utilStringWithString:procName],
                      [NSString utilStringWithString:procPath],
                      stdArgs);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper kill] --
 *
 *      Kill the child process, if running.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Sends SIGTERM to child.  Emits onExit if the child has exited.
 *
 *-----------------------------------------------------------------------------
 */

-(void)kill
{
   ASSERT(mProcHelper);
   mProcHelper->Kill();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper running] --
 *
 *      Getter for running property.
 *
 * Results:
 *      YES if chld is running.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)running
{
   ASSERT(mProcHelper);
   return mProcHelper->IsRunning();
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkProcHelper pid] --
 *
 *      Getter for pid property.
 *
 * Results:
 *      pid of child if running, -1 otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(pid_t)pid
{
   ASSERT(mProcHelper);
   return mProcHelper->GetPID();
}


@end // @implementation CdkProcHelper
