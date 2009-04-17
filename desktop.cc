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
 * desktop.cc --
 *
 *    Desktop info representing a possibly connected desktop exposed by the Broker.
 */


#include <boost/bind.hpp>
#include <glib/gi18n.h>

#include "desktop.hh"
#include "broker.hh"
#include "rdesktop.hh"
#include "util.hh"


#define FRAMEWORK_LISTENER_NAME "FRAMEWORKCHANNEL"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Desktop --
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

Desktop::Desktop(BrokerXml &xml,                  // IN
                 BrokerXml::Desktop &desktopInfo) // IN
   : mXml(xml),
     mConnectionState(STATE_DISCONNECTED),
     mDlg(NULL)
{
   SetInfo(desktopInfo);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::~Desktop --
 *
 *      Desktop destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May disconnect.
 *
 *-----------------------------------------------------------------------------
 */

Desktop::~Desktop()
{
   changed.disconnect_all_slots();
   if (mConnectionState == STATE_CONNECTED) {
      Disconnect();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::SetInfo --
 *
 *      Sets mDesktopInfo and resets mConnectionState if appropriate.
 *      This will NOT emit the changed() signal.
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
Desktop::SetInfo(BrokerXml::Desktop &desktopInfo)
{
   mDesktopInfo = desktopInfo;
   if (mConnectionState == STATE_ROLLING_BACK ||
       mConnectionState == STATE_RESETTING ||
       mConnectionState == STATE_KILLING_SESSION) {
      // Don't use SetConnectionState to avoid emitting changed(); see below.
      mConnectionState = STATE_DISCONNECTED;
   }
   /*
    * Don't explicitly emit changed() here. So far the only use of SetInfo is
    * in Broker::OnGetDesktopsRefresh, which will call UpdateDesktops() once
    * after all desktops have been refreshed.
    */
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::SetConnectionState --
 *
 *      Sets mConnectionState and emits changed() if appropriate.
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
Desktop::SetConnectionState(ConnectionState state) // IN
{
   if (mConnectionState != state) {
      mConnectionState = state;
      changed();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Connect --
 *
 *      Ask the broker to start a connection to this desktop, by calling the
 *      "get-desktop-connection" XML API method.
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
Desktop::Connect(Util::AbortSlot onAbort,   // IN
                 Util::DoneSlot onDone,     // IN
                 Util::ClientInfoMap &info) // IN
{
   ASSERT(mConnectionState == STATE_DISCONNECTED);
   ASSERT(!GetID().empty());

   SetConnectionState(STATE_CONNECTING);
   mXml.GetDesktopConnection(GetID(),
      boost::bind(&Desktop::OnGetDesktopConnectionAbort, this, _1, _2, onAbort),
      boost::bind(&Desktop::OnGetDesktopConnectionDone, this, _1, _2, onDone),
      info);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Disconnect --
 *
 *      TBI.
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
Desktop::Disconnect()
{
   SetConnectionState(STATE_DISCONNECTED);
   if (mDlg) {
      ProcHelper *ph = dynamic_cast<ProcHelper*>(mDlg);
      if (ph) {
         ph->Kill();
      }
      mDlg = NULL;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnGetDesktopConnectionDone --
 *
 *      Success handler for "get-desktop-connection" XML API request.  Store the
 *      broker's connected info.
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
Desktop::OnGetDesktopConnectionDone(BrokerXml::Result &result,          // IN
                                    BrokerXml::DesktopConnection &conn, // IN
                                    Util::DoneSlot onDone)              // IN
{
   ASSERT(mConnectionState == STATE_CONNECTING);

   SetConnectionState(STATE_CONNECTED);
   mDesktopConn = conn;

   onDone();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnGetDesktopConnectionAbort --
 *
 *      Failure handler for "get-desktop-connection" XML API request.  Just
 *      invoke the initially passed abort handler with a more friendly error.
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
Desktop::OnGetDesktopConnectionAbort(bool cancelled,          // IN
                                     Util::exception err,     // IN
                                     Util::AbortSlot onAbort) // IN
{
   ASSERT(mConnectionState == STATE_CONNECTING);

   SetConnectionState(STATE_DISCONNECTED);
   Util::exception myErr(
      Util::Format(_("Unable to connect to desktop \"%s\": %s"),
                   GetName().c_str(), err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetAutoConnect --
 *
 *      Returns whether or not the user preference "alwaysConnect" is true.
 *
 * Results:
 *      true if "alwaysconnect" = "true"; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::GetAutoConnect()
   const
{
   std::vector<BrokerXml::Preference> prefs =
      mDesktopInfo.userPreferences.preferences;
   for (std::vector<BrokerXml::Preference>::iterator i = prefs.begin();
        i != prefs.end(); i++) {
      if (i->first == "alwaysConnect") {
         return i->second == "true";
      }
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::ResetDesktop --
 *
 *      Proxy for BrokerXml::ResetDesktop (restart VM).
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
Desktop::ResetDesktop(Util::AbortSlot onAbort, // IN
                      Util::DoneSlot onDone)   // IN
{
   ASSERT(mConnectionState == STATE_DISCONNECTED ||
          mConnectionState == STATE_CONNECTED);

   SetConnectionState(STATE_RESETTING);
   mXml.ResetDesktop(GetID(),
                     boost::bind(&Desktop::OnResetDesktopAbort, this, _1, _2, onAbort),
                     boost::bind(onDone));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnResetDesktopAbort --
 *
 *      Error handler for reset desktop RPC.
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
Desktop::OnResetDesktopAbort(bool cancelled,
                             Util::exception err,
                             Util::AbortSlot onAbort)
{
   SetConnectionState(STATE_DISCONNECTED);
   Util::exception myErr(
      Util::Format(_("Unable to reset desktop \"%s\": %s"), GetName().c_str(),
                   err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::CanConnect --
 *
 *      Returns whether or not we can connect to this desktop given offline
 *      state and in-flight operations.
 *
 * Results:
 *      true if the desktop is disconnected and is not checked in somewhere.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::CanConnect()
   const
{
   return mConnectionState == STATE_DISCONNECTED &&
     (GetOfflineState() == BrokerXml::OFFLINE_NONE ||
      GetOfflineState() == BrokerXml::OFFLINE_CHECKED_IN);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::KillSession --
 *
 *      Proxy for BrokerXml::KillSession (log out).
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
Desktop::KillSession(Util::AbortSlot onAbort, // IN
                     Util::DoneSlot onDone)   // IN
{
   ASSERT(mConnectionState == STATE_DISCONNECTED);
   ASSERT(!GetSessionID().empty());

   SetConnectionState(STATE_KILLING_SESSION);
   mXml.KillSession(GetSessionID(),
                    boost::bind(&Desktop::OnKillSessionAbort, this, _1, _2, onAbort),
                    boost::bind(onDone));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnKillSessionAbort --
 *
 *      Error handler for KillSession RPC.
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
Desktop::OnKillSessionAbort(bool cancelled,          // IN
                            Util::exception err,     // IN
                            Util::AbortSlot onAbort) // IN
{
   SetConnectionState(STATE_DISCONNECTED);
   Util::exception myErr(
      Util::Format(_("Unable to log out of \"%s\": %s"), GetName().c_str(),
                   err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::Rollback --
 *
 *      Proxy for BrokerXml::Rollback.
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
Desktop::Rollback(Util::AbortSlot onAbort, // IN
                  Util::DoneSlot onDone)   // IN
{
   ASSERT(mConnectionState == STATE_DISCONNECTED);
   ASSERT(GetOfflineState() == BrokerXml::OFFLINE_CHECKED_OUT);

   SetConnectionState(STATE_ROLLING_BACK);
   mXml.Rollback(GetID(),
                 boost::bind(&Desktop::OnRollbackAbort, this, _1, _2, onAbort),
                 boost::bind(onDone));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::OnRollbackAbort --
 *
 *      Error handler for Rollback RPC.
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
Desktop::OnRollbackAbort(bool cancelled,          // IN
                         Util::exception err,     // IN
                         Util::AbortSlot onAbort) // IN
{
   SetConnectionState(STATE_DISCONNECTED);
   Util::exception myErr(
      Util::Format(_("Unable to rollback \"%s\": %s"), GetName().c_str(),
                   err.what()),
      err.code());
   onAbort(cancelled, myErr);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetDlg --
 *
 *      Returns the RDesktop desktop UI object.
 *
 * Results:
 *      The UI Dlg.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Dlg*
Desktop::GetUIDlg()
{
   if (!mDlg) {
      if (mDesktopConn.protocol == "RDP") {
         RDesktop *ui = new RDesktop();
         ui->onExit.connect(boost::bind(&Desktop::Disconnect, this));
         mDlg = ui;
      } else {
         NOT_REACHED();
      }
   }
   return mDlg;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::StartUI --
 *
 *      Calls Start on mDlg. Passes geometry on to rdesktop to allow
 *      multi-monitor screen sizes.
 *
 * Results:
 *      true if a desktop connection exists and the UI has been started, false
 *      otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::StartUI(GdkRectangle *geometry,                           // IN
                 const std::vector<Util::string> &devRedirectArgs, // IN/OPT
                 const std::vector<Util::string> &usbRedirectArgs) // IN/OPT
{
   if (mConnectionState == STATE_CONNECTED && mDlg) {
      RDesktop *rdesktop = dynamic_cast<RDesktop*>(mDlg);

      // Start the vmware-view-usb app if USB is enabled.
      if (mDesktopConn.enableUSB) {
         StartUsb(usbRedirectArgs);
      }

      if (rdesktop) {
         Warning("Connecting rdesktop to %s:%d.\n",
                 mDesktopConn.address.c_str(), mDesktopConn.port);
         rdesktop->Start(mDesktopConn.address, mDesktopConn.username,
                         mDesktopConn.domainName, mDesktopConn.password,
                         geometry, mDesktopConn.port, devRedirectArgs);
      } else {
         NOT_REACHED();
      }
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::StartUsb --
 *
 *      Starts the vmware-view-usb application.
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
Desktop::StartUsb(const std::vector<Util::string> &usbRedirectArgs)  // IN
{
   // First we must locate the framework listener.
   BrokerXml::ListenerMap::iterator it =
      mDesktopConn.listeners.find(FRAMEWORK_LISTENER_NAME);
   if (it != mDesktopConn.listeners.end()) {
      // Start the vmware-view-usb redirection app.
      Warning("Starting usb redirection to '%s:%d' with ticket '%s'.\n",
              it->second.address.c_str(),
              it->second.port,
              mDesktopConn.channelTicket.c_str());
      mUsb.Start(it->second.address, it->second.port,
                 mDesktopConn.channelTicket, usbRedirectArgs);
   }
}


} // namespace cdk
