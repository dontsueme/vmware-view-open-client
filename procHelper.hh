/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
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
 * procHelper.hh --
 *
 *    Child process helper.
 */

#ifndef PROC_HELPER_HH
#define PROC_HELPER_HH


#include <boost/signal.hpp>
#include <vector>


#include "util.hh"


namespace cdk {


class ProcHelper
{
public:
   ProcHelper();
   virtual ~ProcHelper();

   void Start(Util::string procName, Util::string procPath,
              std::vector<Util::string> args = std::vector<Util::string>(),
              int argsCensorMask = 0, gpointer screen = NULL,
              Util::string stdIn = "");

   void Kill();
   static bool GetIsInPath(const Util::string &programName);

   bool IsRunning() const { return mPid > (GPid)-1; }
   GPid GetPID() const { return mPid; }

   virtual bool GetIsErrorExitStatus(int exitCode) { return exitCode != 0; }

   boost::signal1<void, int> onExit;
   boost::signal1<void, Util::string> onErr;

private:
   static void OnErr(void *data);

#ifdef __MINGW32__
   static void OnProcExit(GPid pid, int status, gpointer data);
#endif

   Util::string mProcName;
   GPid mPid;
#ifdef __MINGW32__
   unsigned int mSourceId;
#endif
   int mErrFd;
   Util::string mErrPartialLine;
};


} // namespace cdk


#endif // PROC_HELPER_HH
