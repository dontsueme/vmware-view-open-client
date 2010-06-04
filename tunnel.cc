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
 * tunnel.cc --
 *
 *    Tunnel wrapper API.
 */


#include <boost/bind.hpp>


#include "cdkErrors.h"
#include "tunnel.hh"
#include "baseApp.hh"


#ifdef _WIN32
#define VMWARE_VIEW_TUNNEL "vmware-view-tunnel.exe"
#else
#define VMWARE_VIEW_TUNNEL "vmware-view-tunnel"
#endif

// NOTE: Keep up to date with strings in tunnelMain.c
#define TUNNEL_READY "TUNNEL READY"
#define TUNNEL_STOPPED "TUNNEL STOPPED: "
#define TUNNEL_DISCONNECT "TUNNEL DISCONNECT: "
#define TUNNEL_SYSTEM_MESSAGE "TUNNEL SYSTEM MESSAGE: "
#define TUNNEL_ERROR "TUNNEL ERROR: "

#define SOCKET_ERROR_PREFIX "SOCKET "
// lib/bora/asyncsocket/asyncsocket.c:864
#define SOCKET_ERROR_FAILED_TO_RESOLVE SOCKET_ERROR_PREFIX "Failed to resolve address '"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::Tunnel --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Tunnel::Tunnel()
   : mIsConnected(false)
{
   mProc.onExit.connect(boost::bind(&Tunnel::OnDisconnect, this, _1));
   mProc.onErr.connect(boost::bind(&Tunnel::OnErr, this, _1));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::~Tunnel --
 *
 *      Destructor.  Calls Disconnect.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Tunnel::~Tunnel()
{
   Disconnect();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::GetIsConnected --
 *
 *      Returns whether this tunnel is logically connected.
 *
 * Results:
 *      true if the tunnel is connected, or no tunnel is needed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Tunnel::GetIsConnected() const
{
   return mTunnelInfo.bypassTunnel || mIsConnected;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::Connect --
 *
 *      Fork and exec vmware-view-tunnel.  The binary must exist in the same
 *      directory as the vmware-view binary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Creates two pipes for stdio, and forks a child.
 *
 *-----------------------------------------------------------------------------
 */

void
Tunnel::Connect(const BrokerXml::Tunnel &tunnelInfo) // IN
{
   ASSERT(!mIsConnected);
   ASSERT(!mProc.IsRunning());

   mTunnelInfo = tunnelInfo;
   if (mTunnelInfo.bypassTunnel) {
      Log("Direct connection to desktop enabled; bypassing tunnel "
          "connection.\n");
      onReady();
      return;
   }

   Util::string tunnelPath =
      Util::GetUsefulPath(BINDIR G_DIR_SEPARATOR_S VMWARE_VIEW_TUNNEL,
                          VMWARE_VIEW_TUNNEL);
   if (tunnelPath.empty()) {
      Util::UserWarning(_("Could not find secure tunnel executable.\n"));
      return;
   }
   Log("Executing secure HTTP tunnel: %s\n", tunnelPath.c_str());

   std::vector<Util::string> args;
   args.push_back(GetTunnelUrl());

   mProc.Start(VMWARE_VIEW_TUNNEL, tunnelPath, args, 0, NULL, GetConnectionId() + "\n");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::OnDisconnect --
 *
 *      Callback for when the tunnel has disconnected.  If we have
 *      some status text, pass it on.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Emits onDisconnect signal.
 *
 *-----------------------------------------------------------------------------
 */

void
Tunnel::OnDisconnect(int status) // IN
{
   mIsConnected = false;
   onDisconnect(status, mDisconnectReason);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Tunnel::OnErr --
 *
 *      Stderr callback for the vmware-view-tunnel child process.  If the line
 *      matches the magic TUNNEL_READY string, emit onReady.  For tunnel system
 *      messages and errors, calls BaseApp::ShowInfo and BaseApp::ShowError to
 *      display a dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Tunnel::OnErr(Util::string line) // IN: this
{
  /*
   * In general, messages from the tunnel are not translated. The ones we know
   * about and can reasonably translate, i.e., ones which don't have variable
   * content, are defined in extraTranslations.hh.
   */
   if (line == TUNNEL_READY) {
      mIsConnected = true;
      onReady();
   } else if (line.find(TUNNEL_STOPPED, 0, strlen(TUNNEL_STOPPED)) == 0) {
      mDisconnectReason = Util::string(line, strlen(TUNNEL_STOPPED));
   } else if (line.find(TUNNEL_DISCONNECT, 0, strlen(TUNNEL_DISCONNECT)) == 0) {
      mDisconnectReason = _(Util::string(line,
                              strlen(TUNNEL_DISCONNECT)).c_str());
   } else if (line.find(TUNNEL_SYSTEM_MESSAGE, 0,
                        strlen(TUNNEL_SYSTEM_MESSAGE)) == 0) {
      Util::string msg = Util::string(line, strlen(TUNNEL_SYSTEM_MESSAGE));
      Log("Tunnel system message: %s\n", msg.c_str());
      BaseApp::ShowInfo(_("Message from View Server"), "%s", msg.c_str());
   } else if (line.find(TUNNEL_ERROR, 0, strlen(TUNNEL_ERROR)) == 0) {
      const char *err = _(Util::string(line, strlen(TUNNEL_ERROR)).c_str());
      Log("Tunnel error message: %s\n", err);
      BaseApp::ShowError(CDK_ERR_CONNECTION_SERVER_ERROR,
                         _("Error from View Connection Server"), "%s", err);
   } else if (line.find(SOCKET_ERROR_FAILED_TO_RESOLVE, 0,
                        strlen(SOCKET_ERROR_FAILED_TO_RESOLVE)) == 0) {
      mDisconnectReason =
         Util::Format(_("Couldn't resolve tunnel address '%s'"),
                      GetTunnelUrl().c_str());
   } else if (line.find(SOCKET_ERROR_PREFIX, 0, strlen(SOCKET_ERROR_PREFIX)) == 0) {
      mDisconnectReason = Util::string(line, strlen(SOCKET_ERROR_PREFIX));
   }
}


} // namespace cdk
