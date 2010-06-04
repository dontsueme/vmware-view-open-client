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


#include "desktop.hh"
#include "broker.hh"
#include "util.hh"


#define CVPA_MOID_PREFIX        "cvpa-moid:"
#define FRAMEWORK_LISTENER_NAME "FRAMEWORKCHANNEL"

#define BYTES_PER_MB            (1024 * 1024)


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
     mProtocol(desktopInfo.protocols[desktopInfo.defaultProtocol]),
     mForcedStatus(STATUS_UNKNOWN)
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
      /*
       * XXX: See comment in Broker::OnDesktopOpDone... just kill the
       * session id here until 364022 is fixed, to avoid bugs like
       * 448470.
       */
      mDesktopInfo.sessionId.clear();
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
 * cdk::Desktop::SetProtocol --
 *
 *      Sets mProtocol if the given protocol is available for this desktop.
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
Desktop::SetProtocol(const Util::string &protocol) // IN
{
   std::vector<Util::string> protocols = GetProtocols();
   if (find(protocols.begin(), protocols.end(), protocol) != protocols.end()) {
      mProtocol = protocol;
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
Desktop::Connect(Util::AbortSlot onAbort,         // IN
                 Util::DoneSlot onDone,           // IN
                 const Util::ClientInfoMap &info) // IN
{
   ASSERT(mConnectionState == STATE_DISCONNECTED);
   ASSERT(!GetID().empty());

   SetConnectionState(STATE_CONNECTING);
   mXml.GetDesktopConnection(GetID(),
      boost::bind(&Desktop::OnGetDesktopConnectionAbort, this, _1, _2, onAbort),
      boost::bind(&Desktop::OnGetDesktopConnectionDone, this, _1, _2, onDone),
      info, mProtocol);
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

   mUsb.Kill();
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
   /*
    * If the user canceled the request, we most likely already disconnected the
    * desktop.
    */
   if (!cancelled) {
      ASSERT(mConnectionState == STATE_CONNECTING);
   }

   Disconnect();
   Util::exception myErr(
      _("Unable to connect to desktop"),
      err.code(),
      Util::Format(_("An error occurred while connecting to \"%s\": %s"),
                   GetName().c_str(), err.what()));
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
      _("Unable to reset desktop"),
      err.code(),
      Util::Format(_("An error occured while attempting to reset \"%s\": %s"),
              GetName().c_str(), err.what()));
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
 *      true if the desktop is active.
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
   switch (GetStatus()) {
   case STATUS_LOGGED_ON:
   case STATUS_AVAILABLE_REMOTE:
      return true;
   case STATUS_AVAILABLE_LOCAL:
   case STATUS_EXPIRED:
   case STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT:
   case STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN:
      return IsCVP();
   default:
      return false;
   }
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
      _("Unable to log out"),
      err.code(),
      Util::Format(_("An error occurred while trying to log out of \"%s\": %s"),
                   GetName().c_str(), err.what()));
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
      _("Unable to rollback desktop"),
      err.code(),
      Util::Format(_("An error occurred while attempting to rollback \"%s\": "
                     "%s"), GetName().c_str(), err.what()));
   onAbort(cancelled, myErr);
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
Desktop::StartUsb()
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
                 mDesktopConn.channelTicket);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::IsCVP --
 *
 *      Returns whether the desktop is a CVP desktop.
 *
 *      As it turns out, CVPA will not use a cvpa-moid id if the desktop
 *      is not local.  Thus, we'll assume that a desktop is CVP only if
 *      VIEW_CVP is defined.
 *
 * Results:
 *      true if a CVP desktop, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::IsCVP()
   const
{
#ifdef VIEW_CVP
   return true;
#else
   return false;
#endif // VIEW_CVP
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetRequiresDownload --
 *
 *      Returns whether the desktop must be downloaded prior to use.
 *
 *      Currently, this only applies to CVP desktops.  A CVP desktop that
 *      has an offline state of OFFLINE_CHECKED_IN or OFFLINE_CHECKING_OUT
 *      indicates that the desktop is not local and must be downloaded.
 *
 * Results:
 *      true if this desktop must be downloaded to connect, false otherwise
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::GetRequiresDownload() const
{
   // Check if the desktop is not checked out or only partially checked out
   bool isRemote = GetOfflineState() == BrokerXml::OFFLINE_CHECKED_IN ||
                   GetOfflineState() == BrokerXml::OFFLINE_CHECKING_OUT;

   return IsCVP() && GetEndpointEnabled() && isRemote && !GetCheckedOutByOther();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::IsCheckedOutHereAndDisabled --
 *
 *      Returns whether the desktop is checked out here, but is currently
 *      disabled for some reason, for example, because the underlying VM files
 *      have been corrupted.
 *
 * Results:
 *      bool.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::IsCheckedOutHereAndDisabled()
   const
{
   return IsCVP() && mDesktopInfo.checkedOutHereAndDisabled;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::InNonBackgroundDesktopTransfer --
 *
 *      Returns whether the desktop is in the middle of a "non background"
 *      desktop transfer to or fromm this machine. A "non background" transfer
 *      is either a check in or check out that must be completed or cancelled
 *      before the desktop may be connected to.
 *
 *      Note that this function operates on only the desktop's offline desktop
 *      state as reported by the broker or cvpa and should not be used to
 *      determine if there is an active check in or check out operation
 *      underway.
 *
 * Results:
 *      bool.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::InNonBackgroundDesktopTransfer()
   const
{
   switch (GetOfflineState()) {
   case BrokerXml::OFFLINE_CHECKING_IN:
   case BrokerXml::OFFLINE_CHECKING_OUT:
      return !GetCheckedOutByOther();
   default:
      return false;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetCheckedOutUnavailable --
 *
 *      Returns whether the desktop has been checked out and is unavailable.
 *      Currently, only CVP desktops support a local checkout.
 *
 * Results:
 *      bool.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Desktop::GetCheckedOutUnavailable()
   const
{
   if (GetCheckedOutByOther()) {
      return true;
   }

   /*
    * Non CVP clients do not support offline desktops.
    */
   return (!IsCVP() && GetOfflineState() != BrokerXml::OFFLINE_CHECKED_IN);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetStatus --
 *
 *      Returns the desktop's current status.
 *
 * Results:
 *      Status representing current status of the desktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Desktop::Status
Desktop::GetStatus()
   const
{
   if (mForcedStatus != STATUS_UNKNOWN) {
      return mForcedStatus;
   }

   switch (mConnectionState) {
   case Desktop::STATE_RESETTING:
      return Desktop::STATUS_RESETTING;
   case Desktop::STATE_KILLING_SESSION:
      return Desktop::STATUS_LOGGING_OFF;
   case Desktop::STATE_ROLLING_BACK:
      return Desktop::STATUS_ROLLING_BACK;
   default:
      if (GetOfflineState() == BrokerXml::OFFLINE_ROLLING_BACK) {
         return Desktop::STATUS_SERVER_ROLLBACK;
      } else if (IsExpired()) {
         return Desktop::STATUS_EXPIRED;
      } else if (IsCheckedOutHereAndDisabled()) {
         return STATUS_CHECKED_OUT_DISABLED;
      } else if (GetCheckedOutUnavailable()) {
         return STATUS_CHECKED_OUT_UNAVAILABLE;
      } else if (GetOfflineState() == BrokerXml::OFFLINE_CHECKING_IN) {
         return STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN;
      } else if (GetOfflineState() == BrokerXml::OFFLINE_CHECKING_OUT) {
         return STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT;
      } else if (InMaintenanceMode()) {
         return STATUS_MAINTENANCE_MODE;
      } else if (!GetSessionID().empty()) {
         return STATUS_LOGGED_ON;
      } else if (GetOfflineState() == BrokerXml::OFFLINE_NONE) {
         return STATUS_UNKNOWN_OFFLINE_STATE;
      } else if (IsCVP() && !GetRequiresDownload()) {
         return STATUS_AVAILABLE_LOCAL;
      } else {
         return STATUS_AVAILABLE_REMOTE;
      }
      break;
   }

   NOT_REACHED();
   return STATUS_UNKNOWN;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetStatusMsg --
 *
 *      Returns a user visible string describing the status of this desktop.
 *
 * Results:
 *      string representing the desktop's status.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Desktop::GetStatusMsg(bool isOffline) // IN
   const
{
   Status status = GetStatus();
   Util::string connMsg = isOffline ? _("connect to server to resume")
                                    : _("select connect to resume");

   switch (status) {
   case Desktop::STATUS_RESETTING:
      return _("Resetting desktop");
   case Desktop::STATUS_LOGGING_OFF:
      return _("Logging off");
   case Desktop::STATUS_ROLLING_BACK:
      return _("Rolling back checkout");
   case Desktop::STATUS_SERVER_ROLLBACK:
      return _("The desktop's local session is being rolled back");
   case Desktop::STATUS_HANDLING_SERVER_ROLLBACK:
      return _("Handling a local session rollback");
   case Desktop::STATUS_CHECKED_OUT_DISABLED:
      return _("Desktop is corrupted");
   case Desktop::STATUS_CHECKED_OUT_UNAVAILABLE:
      return _("Checked out to another machine");
   case Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN:
      return Util::Format(_("Check in paused, %s"), connMsg.c_str());
   case Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT:
      if (!IsCVP()) {
         return Util::Format(_("Download paused, %s"), connMsg.c_str());
      } else if (mDesktopInfo.progressWorkDoneSoFar == 0 &&
                 mDesktopInfo.progressTotalWork == 0) {
         return Util::Format(_("Download paused during initialization, %s"),
                             connMsg.c_str());
      } else {
         return Util::Format(_("Download paused at %"FMT64"u MB of "
                               "%"FMT64"u MB, %s"),
                             mDesktopInfo.progressWorkDoneSoFar / BYTES_PER_MB,
                             mDesktopInfo.progressTotalWork / BYTES_PER_MB,
                             connMsg.c_str());
      }
   case Desktop::STATUS_DISCARDING_CHECKOUT:
      return _("Discarding paused download");
   case Desktop::STATUS_MAINTENANCE_MODE:
      return _("Maintenance (may not be available)");
   case Desktop::STATUS_LOGGED_ON:
      return _("Logged on");
   case Desktop::STATUS_AVAILABLE_REMOTE:
      if (IsCVP()) {
         return Util::Format(_("Requires download%s"),
                             isOffline ? ", no connection to server" : "");
      } else {
         return _("Available");
      }
   case Desktop::STATUS_AVAILABLE_LOCAL:
      return _("Available");
   case Desktop::STATUS_UNKNOWN_OFFLINE_STATE:
      return _("Unavailable, contact administrator");
   case Desktop::STATUS_EXPIRED:
      return _("The desktop has expired");
   default:
      NOT_REACHED();
      return _("Unknown status");
   }
}


} // namespace cdk
