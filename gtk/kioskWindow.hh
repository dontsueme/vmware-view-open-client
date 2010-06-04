/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
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
 * kioskWindow.hh --
 *
 *    Application's window, which logs the user in and displays a
 *    desktop in kiosk mode.
 *
 */

#ifndef KIOSK_WINDOW_HH
#define KIOSK_WINDOW_HH


#include <time.h>


#include "window.hh"


namespace cdk {


class KioskWindow
   : public Window
{
public:
   KioskWindow();

   virtual void DoInitialize();

   // State change actions, implements Broker::Delegate
   virtual void RequestBroker();
   virtual void RequestPassword(const Util::string &username,
                                bool readOnly,
                                const std::vector<Util::string> &domains,
                                const Util::string &suggestedDomain);
   virtual void RequestTransition(const Util::string &message,
                                  bool useMarkup = false);
   virtual void RequestDesktop();

   // These requests are handled but NOT supported in kiosk mode.
   virtual void RequestCertificate(std::list<Util::string> &trustedIssuers);
   virtual void RequestPasscode(const Util::string &username,
                                bool userSelectable);
   virtual void RequestNextTokencode(const Util::string &username);
   virtual void RequestPinChange(const Util::string &pin1,
                                 const Util::string &message,
                                 bool userSelectable);
   virtual void RequestDisclaimer(const Util::string &disclaimer);
   virtual void RequestPasswordChange(const Util::string &username,
                                      const Util::string &domain);

   virtual void ShowMessageDialog(GtkMessageType type,
                                  const Util::string &message,
                                  const Util::string &details,
                                  va_list args);

   virtual bool GetFullscreen() const { return true; }

   virtual void TunnelDisconnected(Util::string disconnectReason);

   virtual void Reset();

private:
   virtual void OnDesktopUIExit(Dlg *dlg, int status);

   void ValidatePrefs();

   void StartRetryThrottling();
   bool Throttling() { return mSourceId > 0; }
   static gboolean RetryThrottle(gpointer data);

   Util::string mRetryThrottlingMessage;
   time_t mRetryPeriod;
   time_t mRetryStartTime;
   unsigned int mSourceId;
   Util::string mLastErrorMsg;
};


} // namespace cdk


#endif // KIOSK_WINDOW_HH
