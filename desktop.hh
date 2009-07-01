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
#include "dlg.hh"
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

   Desktop(BrokerXml &xml, BrokerXml::Desktop &desktopInfo);
   ~Desktop();

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
   BrokerXml::OfflineState GetOfflineState() const
      { return mDesktopInfo.offlineState; }
   bool InMaintenanceMode() const { return mDesktopInfo.inMaintenance; }

   bool GetIsUSBEnabled() const { return mDesktopConn.enableUSB; }
   bool GetIsMMREnabled() const { return mDesktopConn.enableMMR; }

   bool GetAutoConnect() const;

   Dlg* GetUIDlg();
   bool StartUI(GdkRectangle *geometry,
                const std::vector<Util::string> &devRedirectArgs =
                   std::vector<Util::string>(),
                const std::vector<Util::string> &usbRedirectArgs =
                   std::vector<Util::string>());

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

   void StartUsb(const std::vector<Util::string> &usbRedirectArgs);

   BrokerXml &mXml;
   BrokerXml::Desktop mDesktopInfo;

   ConnectionState mConnectionState;
   BrokerXml::DesktopConnection mDesktopConn;
   Usb mUsb;

   Dlg *mDlg;
};


} // namespace cdk


#endif // DESKTOP_HH
