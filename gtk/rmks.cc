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
 * rmks.cc --
 *
 *    Simple command line wrapper for rmks.
 */


#include <boost/bind.hpp>
#include <stdlib.h>


#include "app.hh"
#include "cdkErrors.h"
#include "rmks.hh"
#include "VMwareVDPPlugin.h"


namespace cdk {


const Util::string RMks::VMwareRMksBinary = "vmware-remotemks-container";


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RMks::RMks --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Sets up our stderr handler.
 *
 *-----------------------------------------------------------------------------
 */

RMks::RMks(bool tunneledRdpAvailable) // IN
{
   onErr.connect(boost::bind(OnError, _1, tunneledRdpAvailable));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RMks::OnError --
 *
 *      Error handler for RMks process.  Checks for error messages we
 *      might get from rmks, and handles them appropriately.
 *
 *      XXX This is a workaround for PCOIP bug 140, where connection
 *      errors get ignored.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Shows error dialog.
 *
 *-----------------------------------------------------------------------------
 */

void
RMks::OnError(Util::string errorString,  // IN
              bool tunneledRdpAvailable) // IN
{
   /*
    * This line from the pcoip client indicates an ignored connection
    * error that we're handling here:
    *
    * 37d,05:43:43.474> LVL:1 RC: 111      MGMT_SCHAN :scnet_client_open: tera_sock_connect failed to connect to 127.0.0.1:50002!
    * 36d,17:45:53.595> LVL:1 RC:-500 MGMT_PCOIP_DATA :ERROR: Failed to connect PCoIP socket to 127.0.0.1
    */
   if (!strstr(errorString.c_str(), "scnet_client_open: tera_sock_connect failed") &&
       !strstr(errorString.c_str(), "ERROR: Failed to connect PCoIP socket")) {
      return;
   }

   int rv = 0;
   const char *rc = strstr(errorString.c_str(), " RC:");
   if (!rc) {
      Log("Could not find RC from scnet_client_open message.\n");
   } else {
      errno = 0;
      rv = strtoul(rc + strlen(" RC:"), NULL, 10);
      if (errno) {
         Log("Could not parse RC from scnet_client_open message: %s\n",
             strerror(errno));
         rv = 0;
      }
   }

   Util::string message;
   if (rv) {
      message = Util::Format(_("An error was encountered with the remote desktop"
                               " connection: %s."), strerror(rv));
      if (tunneledRdpAvailable) {
         switch (rv) {
         case ENETDOWN:
         case ENETUNREACH:
         case ENETRESET:
         case ECONNABORTED:
         case ECONNRESET:
         case ETIMEDOUT:
         case ECONNREFUSED:
         case EHOSTDOWN:
         case EHOSTUNREACH:
            message += _("\n\nYou may be able to connect to this desktop by"
                         " clicking cancel and selecting a different"
                         " protocol.");
            break;
         default:
            break;
         }
      }
   } else {
      message = _("An unknown error was encountered with the remote desktop"
                  " connection.");
   }

   // An error dialog here will kill the desktop window and show an error.
   BaseApp::ShowError(CDK_ERR_RMKS_CONNECTION_ERROR,
                      _("An error occurred while connecting to the "
                        "remote desktop"), "%s", message.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RMks::Start --
 *
 *      Forks & spawns the rmks process (respects $PATH for finding
 *      rmks) using ProcHelper::Start.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Spawns a child process.
 *
 *-----------------------------------------------------------------------------
 */

void
RMks::Start(const BrokerXml::DesktopConnection &connection, // IN/UNUSED
            const Util::string &windowId,                   // IN
            const Util::Rect *geometry,                     // IN/UNUSED
            GdkScreen *screen)                              // IN
{
   std::vector<Util::string> args;
   int argsMask = 0;
   Util::string kbdLayout;

   args.push_back("pcoip_client");
   args.push_back("mksvchanclient");

   // We don't want to log the connection token.
   argsMask |= 1 << args.size();
   args.push_back(Util::Format("%s:%d;%s", connection.address.c_str(),
                               connection.port, connection.token.c_str()));

   args.push_back(Util::Format("%dx%d", geometry->width, geometry->height));
   args.push_back(windowId);

   kbdLayout = Prefs::GetPrefs()->GetKbdLayout();
   if (!kbdLayout.empty()) {
      args.push_back(kbdLayout);
   }

   if (Prefs::GetPrefs()->GetAllowWMBindings()) {
      Util::UserWarning(_("Warning: -K option is ignored when using PCoIP.\n"));
   }

   ProcHelper::Start(VMwareRMksBinary, VMwareRMksBinary, args, argsMask);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RMks::ExitStatusIsError --
 *
 *      Tells you if an exit code for RMks was an error or a user initiated and
 *      successful exit.
 *
 * Results:
 *      true if the exit code given was an error, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
RMks::GetIsErrorExitStatus(int exitCode) // IN
{
   switch (exitCode) {
   case VDPCONNECT_SUCCESS:
   case VDPCONNECT_SERVER_DISCONNECTED:
   case VDPCONNECT_SERVER_DISCONNECTED_MANUAL_LOGOUT:
   case VDPCONNECT_SERVER_DISCONNECTED_ADMIN_MANUAL:
   case VDPCONNECT_SERVER_DISCONNECTED_RECONNECT:
   /*
    * There is no point in linux client trying to reconnect
    * in case of encryption mismatch hence return this as not an error.
    */
   case VDPCONNECT_ENCRYPTION_MISMATCH:
      return false;
   default:
      return true;
   }
}


} // namespace cdk
