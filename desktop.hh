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
 * desktop.hh --
 *
 *    Desktop info representing a possibly connected desktop exposed by the Broker.
 */

#ifndef DESKTOP_HH
#define DESKTOP_HH


#include <boost/signal.hpp>


#include "brokerXml.hh"
#include "usb.hh"


namespace cdk {


class Desktop
{
public:
   /*
    * This enum captures various states of connection as well as in-flight
    * commands being executed on the desktop (reset, logout, rollback).
    * These commands are remembered here because they're asynchronous, and
    * we can't always tell what's going on otherwise.
    */
   enum ConnectionState {
      STATE_DISCONNECTED,
      STATE_CONNECTING,
      STATE_CONNECTED,
      STATE_RESETTING,
      STATE_KILLING_SESSION,
      STATE_ROLLING_BACK
   };

   /*
    * This enum represents the various statii the desktop can be in.  This is
    * used for status messages and icons.
    */
   enum Status {
      /* We could not determine the status. */
      STATUS_UNKNOWN,
      /* The server reported an offline state we do not understand. */
      STATUS_UNKNOWN_OFFLINE_STATE,
      /* We are current resetting the desktop. */
      STATUS_RESETTING,
      /* We are currently logging off from the desktop. */
      STATUS_LOGGING_OFF,
      /* We are currently rolling back the desktop. */
      STATUS_ROLLING_BACK,
      /*
       * The administrator has initiated a rollback of the desktop that has not
       * yet completed.
       */
      STATUS_SERVER_ROLLBACK,
      /*
       * We are in the process of doing local processing in response to a
       * a server side rollback.
       */
      STATUS_HANDLING_SERVER_ROLLBACK,
      /* The desktop is checked out here but is currently disabled. */
      STATUS_CHECKED_OUT_DISABLED,
      /* The desktop is checked out by another user. */
      STATUS_CHECKED_OUT_BY_OTHER,
      /* The desktop is checked out, but unavailable. */
      STATUS_CHECKED_OUT_UNAVAILABLE,
      /* The desktop is currently being checked in. */
      STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN,
      /* The desktop is currently being checked out. */
      STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT,
      /* We are currently discarding the desktop's checkout. */
      STATUS_DISCARDING_CHECKOUT,
      /* The desktop is in maintenance mode. */
      STATUS_MAINTENANCE_MODE,
      /* The desktop currently has a login session. */
      STATUS_LOGGED_ON,
      /* The desktop is available for remote use. */
      STATUS_AVAILABLE_REMOTE,
      /* The desktop is available for local use. */
      STATUS_AVAILABLE_LOCAL,
      /* The desktop is expired. */
      STATUS_EXPIRED
   };

   Desktop(BrokerXml &xml, BrokerXml::Desktop &desktopInfo);
   ~Desktop();

   void SetInfo(BrokerXml::Desktop &desktopInfo);
   ConnectionState GetConnectionState() const { return mConnectionState; }

   void Connect(Util::AbortSlot onAbort, Util::DoneSlot onDone,
                const Util::ClientInfoMap &info);
   void Disconnect();

   bool CanConnect() const;
   bool CanReset() const { return mDesktopInfo.resetAllowed; }
   bool CanResetSession() const { return mDesktopInfo.resetAllowedOnSession; }
   void ResetDesktop(Util::AbortSlot onAbort, Util::DoneSlot onDone);

   Util::string GetID() const { return mDesktopInfo.id; }
   Util::string GetName() const { return mDesktopInfo.name; }
   Util::string GetSessionID() const { return mDesktopInfo.sessionId; }
   void KillSession(Util::AbortSlot onAbort, Util::DoneSlot onDone);
   void Rollback(Util::AbortSlot onAbort, Util::DoneSlot onDone);

   Util::string GetState() const { return mDesktopInfo.state; }
   bool GetOfflineEnabled() const { return mDesktopInfo.offlineEnabled; }
   bool GetEndpointEnabled() const { return mDesktopInfo.endpointEnabled; }
   BrokerXml::OfflineState GetOfflineState() const
      { return mDesktopInfo.offlineState; }
   bool GetCheckedOutUnavailable() const;
   bool GetCheckedOutByOther() const { return mDesktopInfo.checkedOutByOther; }
   bool InMaintenanceMode() const { return mDesktopInfo.inMaintenance; }
   bool IsCheckedOutHereAndDisabled() const;
   bool InNonBackgroundDesktopTransfer() const;
   bool IsExpired() const { return mDesktopInfo.expired; }

   bool GetIsUSBEnabled() const { return mDesktopConn.enableUSB; }
   bool GetIsMMREnabled() const { return mDesktopConn.enableMMR; }

   std::vector<Util::string> GetProtocols() const
      { return mDesktopInfo.protocols; }
   Util::string GetProtocol() const { return mProtocol; }
   void SetProtocol(const Util::string &protocol);

   bool GetAutoConnect() const;

   Status GetStatus() const;
   Util::string GetStatusMsg(bool isOffline) const;
   bool IsCVP() const;
   bool GetRequiresDownload() const;

   void StartUsb();

   const BrokerXml::DesktopConnection &GetConnection() const
      { return mDesktopConn; }

   void SetForcedStatus(Status status)
      { mForcedStatus = status; }
   void ClearForcedStatus()
      { mForcedStatus = STATUS_UNKNOWN; }
   bool HasForcedStatus() const
      { return STATUS_UNKNOWN != mForcedStatus; }

   boost::signal0<void> changed;

private:
   void SetConnectionState(ConnectionState state);

   void OnGetDesktopConnectionDone(BrokerXml::Result &result,
                                   BrokerXml::DesktopConnection &conn,
                                   Util::DoneSlot onDone);

   void OnGetDesktopConnectionAbort(bool cancelled, Util::exception err,
                                    Util::AbortSlot onAbort);

   void OnResetDesktopAbort(bool canceled, Util::exception err,
                            Util::AbortSlot onAbort);

   void OnKillSessionAbort(bool canceled, Util::exception err,
                           Util::AbortSlot onAbort);
   void OnRollbackAbort(bool canceled, Util::exception err,
                        Util::AbortSlot onAbort);

   BrokerXml &mXml;
   BrokerXml::Desktop mDesktopInfo;

   ConnectionState mConnectionState;
   BrokerXml::DesktopConnection mDesktopConn;
   Usb mUsb;

   Util::string mProtocol;
   Status mForcedStatus;
};


} // namespace cdk


#endif // DESKTOP_HH
