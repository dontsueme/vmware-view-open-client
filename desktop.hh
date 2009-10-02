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
      STATUS_UNKNOWN,
      STATUS_RESETTING,
      STATUS_LOGGING_OFF,
      STATUS_ROLLING_BACK,
      STATUS_LOCAL_ROLLBACK,
      STATUS_CHECKED_OUT_DISABLED,
      STATUS_CHECKED_OUT_BY_OTHER,
      STATUS_NONBACKGROUND_TRANSFER,
      STATUS_MAINTENANCE_MODE,
      STATUS_LOGGED_ON,
      STATUS_AVAILABLE
   };

   Desktop(BrokerXml &xml, BrokerXml::Desktop &desktopInfo);
   virtual ~Desktop();

   void SetInfo(BrokerXml::Desktop &desktopInfo);
   ConnectionState GetConnectionState() const { return mConnectionState; }

   void Connect(Util::AbortSlot onAbort, Util::DoneSlot onDone,
                Util::ClientInfoMap &info);
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
   BrokerXml::OfflineState GetOfflineState() const
      { return mDesktopInfo.offlineState; }
   bool GetCheckedOutByOther() const { return mDesktopInfo.checkedOutByOther; }
   bool InMaintenanceMode() const { return mDesktopInfo.inMaintenance; }
   bool InLocalRollback() const { return mDesktopInfo.inLocalRollback; }
   bool IsCheckedOutHereAndDisabled() const;
   bool InNonBackgroundDesktopTransfer() const;

   Util::string GetCheckedOutHereAndDisabledMessage() const;
   Util::string GetNonBackgroundDesktopTransferMessage() const;

   bool GetIsUSBEnabled() const { return mDesktopConn.enableUSB; }
   bool GetIsMMREnabled() const { return mDesktopConn.enableMMR; }

   std::vector<Util::string> GetProtocols() const
      { return mDesktopInfo.protocols; }
   Util::string GetProtocol() const { return mProtocol; }
   void SetProtocol(Util::string protocol) { mProtocol = protocol; }

   bool GetAutoConnect() const;

   virtual Status GetStatus() const;
   virtual Util::string GetStatusMsg();
   bool IsCVP() const;
   bool GetRequiresDownload() const;

   void StartUsb();

   const BrokerXml::DesktopConnection &GetConnection() const
      { return mDesktopConn; }

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
};


} // namespace cdk


#endif // DESKTOP_HH
