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
 * rdesktop.hh --
 *
 *    Simple command line wrapper for rdesktop.
 *
 */

#ifndef RDESKTOP_HH
#define RDESKTOP_HH

#include <vector>


#include "brokerXml.hh"
#include "procHelper.hh"


namespace cdk {


class RDesktop
   : virtual public ProcHelper
{
public:
   RDesktop();
   virtual ~RDesktop() { }
   static inline bool GetIsProtocolAvailable() { 
        return GetIsInPath(RDesktopBinary); 
   }
   void Start(const BrokerXml::DesktopConnection &connection,
              const Util::string &windowId,
              const Util::Rect *geometry,
              bool enableMMR,
              const std::vector<Util::string> &devRedirectArgs =
                 std::vector<Util::string>(),
              GdkScreen *screen = NULL);

private:
   static void OnError(Util::string errorString);

   enum MMRError {
      MMR_ERROR_GSTREAMER = 3,
   };

   static const Util::string RDesktopBinary;
};


} // namespace cdk


#endif // RDESKTOP_HH
