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
 * kioskWindow.cc --
 *
 *    Implementation of cdk::KioskWindow.
 *
 */


#include "cdkErrors.h"
#include "kioskWindow.hh"
#include "transitionDlg.hh"
#include "loginDlg.hh"


#ifdef GDK_WINDOWING_X11
// Include after VMW headers to prevent conflicts.
#include <gdk/gdkx.h>
#endif


#define RETRY_TIMEOUT         1000
#define RETRY_PERIOD_SCALER   2

#define RETURN_IF_THROTTLING  \
   if (Throttling()) { \
      return; \
   }

#define UNSUPPORTED_OP(n, s, a) \
   void KioskWindow::n a \
   { \
      BaseApp::ShowError(CDK_ERR_KIOSK_UNSUPPORTED_OP, \
                         _("Unattended Mode Error"), s); \
   }


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::KioskWindow --
 *
 *      Constructor
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

KioskWindow::KioskWindow()
   : mRetryPeriod(Prefs::GetPrefs()->GetInitialRetryPeriod()),
     mRetryStartTime((time_t)0),
     mSourceId(0)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::ValidatePrefs --
 *
 *      Ensure required preferences are set and mutually consistent
 *      with respect to kiosk mode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Sets default user to the default Kiosk mode user, if the user name
 *      was not specified via a preference or command line option.
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::ValidatePrefs()
{
   if (Prefs::GetPrefs()->GetDefaultBroker().empty()) {
      Util::UserWarning(_("Unattended mode requires the connection "
                          "server name to be provided.\n"));
      exit(1);
   }

   if (Prefs::GetPrefs()->GetDefaultUser().empty()) {
      unsigned short port;
      Util::ClientInfoMap info =
         Util::GetClientInfo(Util::ParseHostLabel(
            Prefs::GetPrefs()->GetDefaultBroker(), &port, NULL), port);
      Prefs::GetPrefs()->SetDefaultUser(CLIENT_MAC + info["MAC_Address"]);
   }

#ifdef GDK_WINDOWING_X11
   GdkAtom atom = gdk_atom_intern("_NET_WM_FULLSCREEN_MONITORS", FALSE);
   bool supported = atom != GDK_NONE && gdk_net_wm_supports(atom);
#else
   bool supported = false;
#endif
   Prefs::GetPrefs()->SetDefaultDesktopSize(
      supported ? Prefs::ALL_SCREENS : Prefs::FULL_SCREEN);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KikoskWindow::RequestBroker --
 *
 *    Set up the broker connection, displaying in a transition dialog.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    Resets the broker state.
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::RequestBroker()
{
   RETURN_IF_THROTTLING;

   Reset();
   ValidatePrefs();
   g_idle_add(DelayedDoInitialize, this);
   RequestTransition(_("Connecting..."));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KikoskWindow::RequestPassword --
 *
 *    Submit default login information and display transition dialog.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::RequestPassword(const Util::string &username,             // IN/UNUSED
                             bool readOnly,                            // IN/UNUSED
                             const std::vector<Util::string> &domains, // IN/UNUSED
                             const Util::string &suggestedDomain)      // IN/UNUSED
{
   RETURN_IF_THROTTLING;

   RequestTransition(_("Logging in..."));
   const char *password = Prefs::GetPrefs()->GetPassword();
   GetBroker()->SubmitPassword(Prefs::GetPrefs()->GetDefaultUser(),
                               password ? password : "",
                               Prefs::GetPrefs()->GetDefaultDomain());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::RequestTransition --
 *
 *      Show the transition dialog; a message with a spinner, without
 *      the Help or other buttons.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Transition dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::RequestTransition(const Util::string &message, bool useMarkup)
{
   Window::RequestTransition(message, useMarkup);

   UpdateForwardButton(false, false);
   UpdateCancelButton(false, false);
   UpdateHelpButton(false, false);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::RequestDesktop --
 *
 *      Force default desktop to be used and invoke the forward handler to move
 *      onward to the desktop connect step.
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
KioskWindow::RequestDesktop()
{
   RETURN_IF_THROTTLING;

   Desktop *desktop = NULL;
   Util::string defaultDesktop = Prefs::GetPrefs()->GetDefaultDesktop();

   if (defaultDesktop.empty()) {
      /*
       * If no default desktop name is provided then we use the desktop given
       * to us by the broker (in a properly configured kiosk broker at most
       * one desktop should be provided for us to use so we assume that here.)
       */
      if (GetBroker()->mDesktops.empty()) {
         BaseApp::ShowError(CDK_ERR_DESKTOP_NOT_AVAILABLE,
                            _("Desktop Not Available"),
                            _("No desktop for user '%s' "
                              "on connection server '%s'."),
                            Prefs::GetPrefs()->GetDefaultUser().c_str(),
                            Prefs::GetPrefs()->GetDefaultBroker().c_str());
         return;
      }
      desktop = GetBroker()->mDesktops[0];
   } else {
      for (std::vector<Desktop *>::iterator i = GetBroker()->mDesktops.begin();
           i != GetBroker()->mDesktops.end(); i++) {
         if ((*i)->GetName() == defaultDesktop) {
            desktop = *i;
            break;
         }
      }
      if (!desktop) {
         BaseApp::ShowError(CDK_ERR_DESKTOP_NOT_AVAILABLE,
                            _("Desktop Not Available"),
                            _("Desktop '%s' is not available "
                              "from connection server '%s'."),
                            defaultDesktop.c_str(),
                            Prefs::GetPrefs()->GetDefaultBroker().c_str());
         return;
      }
   }

   if (!Prefs::GetPrefs()->GetDefaultProtocol().empty()) {
      desktop->SetProtocol(Prefs::GetPrefs()->GetDefaultProtocol());
   }

   DoDesktopConnect(desktop);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::RequestCertificate --
 *
 *      Handle certificate requests (we ignore them.)
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
KioskWindow::RequestCertificate(std::list<Util::string> &trustedIssuers) // IN/UNUSED
{
   GetBroker()->SubmitCertificate();
}


UNSUPPORTED_OP(RequestPasscode,
               _("Unexpected pass code request encountered."),
               (const Util::string &username, bool userSelectable));


UNSUPPORTED_OP(RequestNextTokencode,
               _("Unexpected next token request encountered."),
               (const Util::string &username));


UNSUPPORTED_OP(RequestPinChange,
               _("Unexpected pin change request encountered."),
               (const Util::string &pin, const Util::string &message,
                bool userSelectable));


UNSUPPORTED_OP(RequestPasswordChange,
               _("Unexpected password change request."),
               (const Util::string &username, const Util::string &domain));


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::DoInitialize --
 *
 *    Initialize broker with minimal settings for kiosk mode.
 *
 *
 * Results:
 *    None
 *
 * Side effects:
 *    See Broker::Initialize().
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::DoInitialize()
{
   unsigned short port;
   bool secure;
   Prefs *prefs;
   Util::string url;

   SetBroker(new Broker());
   prefs = Prefs::GetPrefs();
   url = Util::ParseHostLabel(prefs->GetDefaultBroker(), &port, &secure);
   SetCookieFile(Util::GetHostLabel(prefs->GetDefaultBroker(), port, secure));
   InitializeProtocols();
   GetBroker()->Initialize(url, port, secure, prefs->GetDefaultUser(), "");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::ShowMessageDialog --
 *
 *      Shows an error message in a transition dialog, with a countdown timer
 *      indicating when whatever has failed will be retried again.
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
KioskWindow::ShowMessageDialog(GtkMessageType type,         // IN
                               const Util::string &message, // IN
                               const Util::string &details, // IN
                               va_list args)                // IN
{
   Util::string dets = Util::FormatV(details.c_str(), args);
   char *tmp = g_markup_printf_escaped("<b>%s</b>\n\n%s",
                                       message.c_str(), dets.c_str());
   mRetryThrottlingMessage = tmp;
   g_free(tmp);

   if (mRetryThrottlingMessage != mLastErrorMsg) {
      mRetryPeriod = Prefs::GetPrefs()->GetInitialRetryPeriod();
      mLastErrorMsg = mRetryThrottlingMessage;
   }
   mRetryThrottlingMessage += _("\n\nRetrying in %d seconds...");

   GetBroker()->Reset();

   TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_ERROR,
                                          Util::Format(
                                             mRetryThrottlingMessage.c_str(),
                                             mRetryPeriod),
                                          true);
   switch (type) {
   case GTK_MESSAGE_ERROR:
      dlg->SetStock(GTK_STOCK_DIALOG_ERROR);
      break;
   case GTK_MESSAGE_WARNING:
      dlg->SetStock(GTK_STOCK_DIALOG_WARNING);
      break;
   default:
      // we don't specialize for other message types, e.g., other, etc.
      dlg->SetStock(GTK_STOCK_DIALOG_INFO);
      break;
   }
   SetContent(dlg);

   UpdateForwardButton(false, false);
   UpdateCancelButton(false, false);
   UpdateHelpButton(false, false);

   StartRetryThrottling();

   Log("(Kiosk) %s %s\n", message.c_str(), dets.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::RetryThrottle --
 *
 *      Timeout callback to count down to the next retry of whatever
 *      failure resulted in a call to ShowMessageDialog.  Updates
 *      contents of the current window dialog to reflect the amount
 *      of time remaining until retry.
 *
 * Results:
 *      true if timeout has not elapsed; false if it has.
 *
 * Side effects:
 *      Timeout is automatically destroyed if this method returns false.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
KioskWindow::RetryThrottle(gpointer data) // IN
{
   KioskWindow *window = reinterpret_cast<KioskWindow *>(data);
   ASSERT(window);

   time_t elapsedTime = time(NULL) - window->mRetryStartTime;

   if (elapsedTime >= window->mRetryPeriod) {
      window->mRetryPeriod = MIN(window->mRetryPeriod * RETRY_PERIOD_SCALER,
                                 Prefs::GetPrefs()->GetMaximumRetryPeriod());
      window->mSourceId = 0;
      window->RequestBroker();
      return false;
   }

   TransitionDlg *dlg = dynamic_cast<TransitionDlg *>(window->GetDlg());
   ASSERT(dlg);

   dlg->SetMessage(Util::Format(window->mRetryThrottlingMessage.c_str(),
                                (int)(window->mRetryPeriod - elapsedTime)));
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::StartRetryThrottling --
 *
 *      Sets up / starts the kiosk's retry throttle callback.
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
KioskWindow::StartRetryThrottling()
{
   if (mSourceId) {
      g_source_remove(mSourceId);
      mSourceId = 0;
   }
   mRetryStartTime = time(NULL);
   mSourceId = g_timeout_add(RETRY_TIMEOUT, KioskWindow::RetryThrottle, this);
   ASSERT(mSourceId > 0);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::TunnelDisconnected --
 *
 *      Tunnel onDisconnect signal handler.  Defer to base class handler
 *      then impose our retry policy.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Relaunch of tunnel and reconnection delayed by retry throttling.
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::TunnelDisconnected(Util::string disconnectReason) // IN
{
   Window::TunnelDisconnected(disconnectReason);
   StartRetryThrottling();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::Reset --
 *
 *    Resets the kiosk window and ensures our timeout is not left dangling.
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
KioskWindow::Reset()
{
   if (mSourceId > 0) {
      g_source_remove(mSourceId);
      mSourceId = 0;
   }

   Window::Reset();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::OnDesktopUIExit --
 *
 *      Handle desktop exiting.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Restarts desktop.
 *
 *-----------------------------------------------------------------------------
 */

void
KioskWindow::OnDesktopUIExit(Dlg *dlg,   // IN/UNUSED
                             int status) // IN/UNUSED
{
   RequestBroker();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::KioskWindow::RequestDisclaimer --
 *
 *      Trivially accepts the incoming disclaimer.
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
KioskWindow::RequestDisclaimer(const Util::string &disclaimer) // IN/UNUSED
{
   GetBroker()->AcceptDisclaimer();
}


} // namespace cdk
