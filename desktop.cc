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
#include "util.hh"


#define CVPA_MOID_PREFIX "cvpa-moid:"
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
     mProtocol(desktopInfo.protocols[desktopInfo.defaultProtocol])
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
      GetOfflineState() == BrokerXml::OFFLINE_CHECKED_IN ||
      !GetCheckedOutByOther());
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

   return IsCVP() && GetOfflineEnabled() && isRemote && !GetCheckedOutByOther();
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
   return false;
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
 * cdk::Desktop::GetCheckedOutHereAndDisabledMessage --
 *
 *      Returns a message appropriate for display as to why a locally checked
 *      out desktop is currently disabled.
 *
 * Results:
 *      Util::string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Desktop::GetCheckedOutHereAndDisabledMessage()
   const
{
   ASSERT(IsCheckedOutHereAndDisabled());

   return _("Desktop is corrupted.");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetNonBackgroundDesktopTransferMessage --
 *
 *      Returns a message appropriate for display summarizing the current
 *      progress made on a non background desktop transfer.
 *
 *      Note that this function operaets on only the desktop's offline desktop
 *      state as reported by cvpa and should not be used to determine if there
 *      is an active check in or check out operation underway.
 *
 * Results:
 *      Util::string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Desktop::GetNonBackgroundDesktopTransferMessage()
   const
{
   ASSERT(InNonBackgroundDesktopTransfer());
   /*
    * This call uses information only returned by cvpa, so we should only be
    * called on CVP. Given that only CVP clients can be in desktop transfers
    * to a client machine, this is a valid ASSERT.
    */
   ASSERT(IsCVP());

   Util::string message;

   switch (GetOfflineState()) {
   case BrokerXml::OFFLINE_CHECKING_IN:
      message = _("Desktop check in paused. Select connect to resume.");
      break;
   case BrokerXml::OFFLINE_CHECKING_OUT:
      message = _("Desktop check out paused. Select connect to resume.");
      break;
   default:
      NOT_IMPLEMENTED();
   }

   return message;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Desktop::GetStatus --
 *
 *      Returns the desktop's current status.
 *
 * Results:
 *      Desktop::Status.
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
   switch (mConnectionState) {
   case Desktop::STATE_RESETTING:
      return Desktop::STATUS_RESETTING;
   case Desktop::STATE_KILLING_SESSION:
      return Desktop::STATUS_LOGGING_OFF;
   case Desktop::STATE_ROLLING_BACK:
      return Desktop::STATUS_ROLLING_BACK;
   default:
      if (InLocalRollback()) {
         return STATUS_LOCAL_ROLLBACK;
      } else if (IsCheckedOutHereAndDisabled()) {
         return STATUS_CHECKED_OUT_DISABLED;
      } else if (GetCheckedOutByOther() ||
                 (!IsCVP() && InNonBackgroundDesktopTransfer())) {
         return STATUS_CHECKED_OUT_BY_OTHER;
      } else if (InNonBackgroundDesktopTransfer()) {
         return STATUS_NONBACKGROUND_TRANSFER;
      } else if (InMaintenanceMode()) {
         return STATUS_MAINTENANCE_MODE;
      } else if (!GetSessionID().empty()) {
         return STATUS_LOGGED_ON;
      } else {
         return STATUS_AVAILABLE;
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
Desktop::GetStatusMsg()
{
   Desktop::Status status = GetStatus();

   switch (status) {
   case Desktop::STATUS_RESETTING:
      return _("Resetting desktop");
   case Desktop::STATUS_LOGGING_OFF:
      return _("Logging off");
   case Desktop::STATUS_ROLLING_BACK:
      return _("Rolling back checkout");
   case Desktop::STATUS_LOCAL_ROLLBACK:
      return _("Desktop is being rolled back (may not be available)");
   case Desktop::STATUS_CHECKED_OUT_DISABLED:
      return GetCheckedOutHereAndDisabledMessage();
   case Desktop::STATUS_CHECKED_OUT_BY_OTHER:
      return _("Checked out to another machine");
   case Desktop::STATUS_NONBACKGROUND_TRANSFER:
      return GetNonBackgroundDesktopTransferMessage();
   case Desktop::STATUS_MAINTENANCE_MODE:
      return _("Maintenance (may not be available)");
   case Desktop::STATUS_LOGGED_ON:
      return _("Logged on");
   case Desktop::STATUS_AVAILABLE:
      return _("Available");
   default:
      NOT_REACHED();
      return _("Unknown status");
   }
}


} // namespace cdk
