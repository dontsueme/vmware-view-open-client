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
 * usb.cc --
 *
 *    Starts the vmware-view-usb application.
 */

#include "usb.hh"


#include <glib.h>
#include <sstream>
#include <unistd.h>
#include <vector>


#define USB_ADDRESS_ARG "-a"
#define USB_PORT_ARG "-p"
#define USB_TICKET_ARG "-u"
#define VMWARE_VIEW_USB "vmware-view-usb"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Usb::Start --
 *
 *      Launches the vmware-view-usb application
 *
 * Results:
 *      True if vmware-view-usb is started, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Usb::Start(const Util::string &address,                        // IN
           int port,                                           // IN
           const Util::string &channelTicket,                  // IN
           const std::vector<Util::string> &usbRedirectArgs)   // IN
{
   if (IsRunning()) {
      Warning("USB redirect already running.\n");
      return false;
   }

   std::stringstream strmPort;
   strmPort << port;

   std::vector<Util::string>  args;
   args.push_back(USB_ADDRESS_ARG);   args.push_back(address);
   args.push_back(USB_PORT_ARG);      args.push_back(strmPort.str());
   args.push_back(USB_TICKET_ARG);    args.push_back(channelTicket);

   for (std::vector<Util::string>::const_iterator i = usbRedirectArgs.begin();
        i != usbRedirectArgs.end(); ++i) {
      args.push_back("-o");  args.push_back(*i);
   }

   Util::string usbPath =
      Util::GetUsefulPath(BINDIR G_DIR_SEPARATOR_S VMWARE_VIEW_USB,
                          VMWARE_VIEW_USB);
   if (usbPath.empty()) {
      Util::UserWarning(_("%s was not found; disabling USB redirection."),
                        BINDIR G_DIR_SEPARATOR_S VMWARE_VIEW_USB);
      return false;
   }

   ProcHelper::Start(VMWARE_VIEW_USB, usbPath, args);
   return true;
}


} // namespace cdk
