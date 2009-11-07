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
#ifdef __linux__
#include "prefs.hh"
#endif


#include <glib.h>
#include <sstream>
#include <unistd.h>
#include <vector>

extern "C" {
#include "file.h"
}


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
Usb::Start(const Util::string &address,       // IN
           int port,                          // IN
           const Util::string &channelTicket) // IN
{
   if (IsRunning()) {
      Warning("Stopping usb redirection.\n");
      ProcHelper::Kill();
      Warning("Restarting usb redirection.\n");
   }

   std::stringstream strmPort;
   strmPort << port;

   std::vector<Util::string>  args;
   int argsMask = 0;

   args.push_back(USB_ADDRESS_ARG);   args.push_back(address);
   args.push_back(USB_PORT_ARG);      args.push_back(strmPort.str());
   args.push_back(USB_TICKET_ARG);
   // Don't log the ticket.
   argsMask |= 1 << args.size();
   args.push_back(channelTicket);

#ifdef __linux__
   std::vector<Util::string> usbRedirectArgs = Prefs::GetPrefs()->GetUsbOptions();
   for (std::vector<Util::string>::const_iterator i = usbRedirectArgs.begin();
        i != usbRedirectArgs.end(); ++i) {
      args.push_back("-o");  args.push_back(*i);
   }
#endif

   Util::string defaultPath = BINDIR G_DIR_SEPARATOR_S VMWARE_VIEW_USB;
   Util::string usbPath = Util::GetUsefulPath(defaultPath, VMWARE_VIEW_USB);
   if (usbPath.empty()) {
      /*
       * So, the real binary path on SLETC is in
       * /read-write/mnt/addons/VMware-view-client-lite/usr/bin, which
       * we're unable to install the usb binary in (since it's a
       * squash rpm), and we may as well default back to
       * /usr/bin/vmware-view-usb if it exists.
       */
      if (File_Exists(defaultPath.c_str())) {
         usbPath = defaultPath;
      } else {
         Util::UserWarning(_("%s was not found; disabling USB redirection.\n"),
                           defaultPath.c_str());
         return false;
      }
   }

   ProcHelper::Start(VMWARE_VIEW_USB, usbPath, args, argsMask);
   return true;
}


} // namespace cdk
