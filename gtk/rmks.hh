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
 * rmks.hh --
 *
 *    Simple command line wrapper for rmks.
 *
 */

#ifndef RMKS_HH
#define RMKS_HH


#include "brokerXml.hh"
#include "procHelper.hh"


#ifdef _WIN32
#include <winsock2.h>
#include <winsockerr.h>
#endif


namespace cdk {


class RMks
   : virtual public ProcHelper
{
public:
   RMks(bool tunneledRdpAvailable);
   virtual ~RMks() { }
   static inline bool GetIsProtocolAvailable()
      { return GetIsInPath(VMwareRMksBinary); }

   void Start(const BrokerXml::DesktopConnection &connection,
              const Util::string &windowId,
              const Util::Rect *geometry,
              GdkScreen *screen = NULL);

   virtual bool GetIsErrorExitStatus(int exitCode);

private:
   static void OnError(Util::string errorString, bool tunneledRdpAvailable);

   static const Util::string VMwareRMksBinary;
};


} // namespace cdk


#endif // RMKS_HH
