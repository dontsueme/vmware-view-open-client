/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
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
 * mstsc.cc --
 *
 *    Simple command line wrapper for mstsc.exe.
 */

#include <boost/bind.hpp>


#include "app.hh"
#include "prefs.hh"
#include "mstsc.hh"


namespace cdk {


const Util::string Mstsc::MstscBinary = "mstsc.exe";


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Mstsc::Start --
 *
 *      Forks & spawns the mstsc process (respects $PATH for finding
 *      mstsc) using ProcHelper::Start.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Spawns a child process NOT embedded in our window.  Caller
 *      is responsible for closing any residual GUI.
 *
 *-----------------------------------------------------------------------------
 */

void
Mstsc::Start(const BrokerXml::DesktopConnection &connection,   // IN
             const Util::Rect *geometry,                       // IN
             GdkScreen *screen)                                // IN/OPT
{
   ASSERT(!connection.address.empty());

   std::vector<Util::string> args;

   args.push_back(Util::Format("/v:%s:%d",
                               connection.address.c_str(),
                               connection.port).c_str());
   if (Prefs::GetPrefs()->GetFullScreen()) {
      args.push_back(Util::Format("/f"));
   } else {
      args.push_back(Util::Format("/w:%d", geometry->width).c_str());
      args.push_back(Util::Format("/h:%d", geometry->height).c_str());
   }

   ProcHelper::Start(MstscBinary, MstscBinary, args, 0, screen);
}


} // namespace cdk
