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
 * broker.cc --
 *
 *    Broker control.
 */


#include <boost/bind.hpp>
#include <glib/gi18n.h>


#include "app.hh"
#include "broker.hh"
#include "desktop.hh"
#include "tunnel.hh"
#include "util.hh"


#define ERR_UNSUPPORTED_VERSION "UNSUPPORTED_VERSION"
#define ERR_ALREADY_AUTHENTICATED "ALREADY_AUTHENTICATED"
#define ERR_AUTHENTICATION_FAILED "AUTHENTICATION_FAILED"


#define TOKEN_EVENT_TIMEOUT_MS 500


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Broker --
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

Broker::Broker()
   : mXml(NULL),
     mTunnel(NULL),
     mDesktop(NULL),
     mCryptoki(NULL),
     mPin(NULL),
     mX509(NULL),
     mCertState(CERT_NOT_REQUESTED),
     mRefreshTimeoutSourceID(0),
     mGettingDesktops(false),
     mTokenEventTimeout(0)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::~Broker --
 *
 *      Destructor.  Deletes all connected desktops asynchronously.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Broker::~Broker()
{
   if (mRefreshTimeoutSourceID) {
      g_source_remove(mRefreshTimeoutSourceID);
      mRefreshTimeoutSourceID = 0;
   }
   Reset();
   delete mCryptoki;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Reset --
 *
 *      Reset state to allow a new login.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      State is restored as if a new broker was created.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::Reset()
{
   for (std::vector<Desktop*>::iterator i = mDesktops.begin();
        i != mDesktops.end(); i++) {
      delete *i;
   }
   mDesktops.clear();
   // already deleted above
   mDesktop = NULL;

   mTunnelDisconnectCnx.disconnect();
   mTunnelMonitor.Reset();
   delete mTunnel;
   mTunnel = NULL;

   delete mXml;
   mXml = NULL;

   ASSERT(!mX509);
   ASSERT(!mPin);

   if (mCryptoki) {
      mCryptoki->CloseAllSessions();
   }
   mCertState = CERT_NOT_REQUESTED;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Initialize --
 *
 *      Initialize the broker connection, calling BrokerXml::SetLocale and
 *      BrokerXml::GetConfiguration.
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
Broker::Initialize(const Util::string &hostname,      // IN
                   int port,                          // IN
                   bool secure,                       // IN
                   const Util::string &defaultUser,   // IN
                   const Util::string &defaultDomain) // IN
{
   ASSERT(!mXml);
   ASSERT(!mTunnel);

   Log("Initialzing connection to broker %s://%s:%d\n",
       secure ? "https" : "http", hostname.c_str(), port);

   mXml = new BrokerXml(hostname, port, secure);
   mXml->certificateRequested.connect(
      boost::bind(&Broker::OnCertificateRequested, this, _1, _2, _3));
   mUsername = defaultUser;
   mDomain = defaultDomain;

   SetLocale();
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SetLocale --
 *
 *      Issue a set-locale RPC, based on our current locale.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      set-locale RPC is issued.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SetLocale()
{
   /*
    * The SetLocale RPC is only supported by protocol 2.0, so it'd be
    * nice not to send it to 1.0 servers.  Sadly, it's the first RPC
    * we send, so we don't know what version the server is... yet.
    */
   char *locale = setlocale(LC_MESSAGES, NULL);
   if (locale != NULL && locale[0] != '\0' && strcmp(locale, "C") != 0 &&
       strcmp(locale, "POSIX") != 0) {
      SetBusy(_("Setting client locale..."));
      mXml->SetLocale(locale,
                      boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
                      boost::bind(&Broker::GetConfiguration, this));
   } else {
      GetConfiguration();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitScInsertPrompt --
 *
 *      Answer to a RequestScInsertPrompt call.  Quit the main loop
 *      and return to processing the certificate request.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A Gtk main loop is quit.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitScInsertPrompt(bool useCert) // IN
{
   ASSERT(mCertState == CERT_SHOULD_RESPOND);
   SetBusy(_("Logging in..."));
   if (!useCert) {
      mCertState = CERT_DID_RESPOND;
   }
   gtk_main_quit();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitScPin --
 *
 *      Answer to a ScPinRequested call.  Store the PIN, and quit the
 *      extra main loop to continue with the SSL handshake process.
 *
 *      A NULL pin signifies that the card should not be logged in to.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A Gtk main loop is quit.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitScPin(const char *pin) // IN
{
   SetBusy(_("Logging in..."));
   ASSERT(!mPin);
   mPin = g_strdup(pin);
   gtk_main_quit();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitCertificate --
 *
 *      Answer to the RequestScPin call.  Stores the cert to use, and
 *      quits a main loop so the SSL handshake can continue.
 *
 *      A NULL x509 signifies that no certificate should be used.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A Gtk main loop is quit.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitCertificate(X509 *x509) // IN
{
   SetBusy(_("Logging in..."));
   ASSERT(!mX509);
   mX509 = x509;
   gtk_main_quit();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::AcceptDisclaimer --
 *
 *      Notify the broker that the user has accepted the disclaimer.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::AcceptDisclaimer()
{
   SetBusy(_("Accepting disclaimer..."));
   if (mCertState == CERT_REQUESTED) {
      /*
       * Pre-login message is enabled, and the server asked us for a
       * cert.  We'll reset our connections, and when we do the accept
       * disclaimer RPC, we'll get asked for a cert again.
       */
      Log("Accepting disclaimer and cert was requested; "
          "enabling cert response and accepting disclaimer.\n");
      mXml->ResetConnections();
      mCertState = CERT_SHOULD_RESPOND;
   }
   mXml->AcceptDisclaimer(
      boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitPasscode --
 *
 *      Attempt authentication using a SecurID passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitPasscode(const Util::string &username, // IN
                       const Util::string &passcode) // IN
{
   SetBusy(_("Logging in..."));
   mUsername = username;
   mXml->SecurIDUsernamePasscode(
      username, passcode,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitNextTokencode --
 *
 *      Continue authentication by providing the next tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitNextTokencode(const Util::string &tokencode) // IN
{
   SetBusy(_("Logging in..."));
   mXml->SecurIDNextTokencode(
      tokencode,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitPins --
 *
 *      Continue authentication by providing new PINs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitPins(const Util::string &pin1, // IN
                   const Util::string &pin2) // IN
{
   SetBusy(_("Logging in..."));
   mXml->SecurIDPins(
      pin1, pin2,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitPassword --
 *
 *      Authenticate with a windows username and password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitPassword(const Util::string &username, // IN
                       const Util::string &password, // IN
                       const Util::string &domain)   // IN
{
   SetBusy(_("Logging in..."));
   mUsername = username;
   mDomain = domain;
   mXml->PasswordAuthentication(
      username, password, domain,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ChangePassword --
 *
 *      Provide a new password for the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ChangePassword(const Util::string &oldPassword, // IN
                       const Util::string &newPassword, // IN
                       const Util::string &confirm)     // IN
{
   SetBusy(_("Changing password..."));
   mXml->ChangePassword(oldPassword, newPassword, confirm,
                        boost::bind(&Broker::OnAbort, this, _1,_2),
                        boost::bind(&Broker::OnAuthResult, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetDesktopConnection --
 *
 *      Begin connecting to a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ConnectDesktop(Desktop *desktop) // IN
{
   ASSERT(desktop);
   ASSERT(desktop->GetConnectionState() == Desktop::STATE_DISCONNECTED);

   mDesktop = desktop;
   RequestTransition(_("Connecting to the desktop..."));
   if (!mTunnel) {
      InitTunnel();
   } else if (mTunnel->GetIsConnected()) {
      /*
       * Connecting to the desktop before the tunnel is connected
       * results in DESKTOP_NOT_AVAILABLE.
       */
      Util::ClientInfoMap clientInfo = Util::GetClientInfo(mXml->GetHostname(),
                                                           mXml->GetPort());
      desktop->Connect(boost::bind(&Broker::OnAbort, this, _1, _2),
                       boost::bind(&Broker::MaybeLaunchDesktop, this),
                       clientInfo);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ReconnectDesktop --
 *
 *      Initiate a desktop reconnection (rdesktop or the tunnel may
 *      have died).  If this is called because the tunnel died, the
 *      server would tell us that it's unable to reconnect to the
 *      desktop, so we defer the actual connection until we reconnect
 *      the tunnel.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop may be connected.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ReconnectDesktop()
{
   ASSERT(mDesktop);
   ASSERT(mDesktop->GetConnectionState() != Desktop::STATE_CONNECTING);

   if (mDesktop->GetConnectionState() == Desktop::STATE_CONNECTED) {
      mDesktop->Disconnect();
   }
   ConnectDesktop(mDesktop);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ResetDesktop --
 *
 *      Reset a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop is reset, app may quit.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::ResetDesktop(Desktop *desktop, // IN
                     bool andQuit)     // IN
{
   ASSERT(mXml);
   ASSERT(desktop);

   Util::AbortSlot abort = boost::bind(&Broker::OnAbort, this, _1, _2);
   if (andQuit) {
      desktop->ResetDesktop(abort, boost::bind(&Broker::Quit, this));
   } else {
      desktop->ResetDesktop(abort, boost::bind(&Broker::OnDesktopOpDone,
                                               this, desktop));
      UpdateDesktops();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::KillSession --
 *
 *      Log out from a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Attempts to kill the active session.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::KillSession(Desktop *desktop) // IN
{
   ASSERT(mXml);
   ASSERT(desktop);

   desktop->KillSession(
                      boost::bind(&Broker::OnAbort, this, _1, _2),
                      boost::bind(&Broker::OnDesktopOpDone, this, desktop));
   UpdateDesktops();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::RollbackDesktop --
 *
 *      Roll back a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tries to roll back desktop.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::RollbackDesktop(Desktop *desktop) // IN
{
   ASSERT(mXml);
   ASSERT(desktop);
   ASSERT(desktop->GetOfflineState() == BrokerXml::OFFLINE_CHECKED_OUT);

   desktop->Rollback(boost::bind(&Broker::OnAbort, this, _1, _2),
                     boost::bind(&Broker::OnDesktopOpDone, this, desktop));
   UpdateDesktops();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnDesktopOpDone --
 *
 *      Success handler for a desktop operation (log out, reset, rollback).
 *      Refreshes the list of desktops.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      get-desktops request sent 
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnDesktopOpDone(Desktop *desktop) // IN
{
   if (desktop->GetConnectionState() == Desktop::STATE_RESETTING ||
       desktop->GetConnectionState() == Desktop::STATE_KILLING_SESSION) {
      /*
       * XXX: This is a temporary workaround for the fact that the broker still
       * reports the existence of a desktop session after "kill-session" or
       * "reset-desktop" is "ok"'ed--see bug 364022.
       * Wait some time before getting the list of desktops again.
       *
       * If there's a timeout already in flight from another operation,
       * kill it and extend the wait time so there's only one refresh.
       * If we don't do that, temporary Desktop ConnectionState values will
       * get cleared by the refresh before they should.
       */
      if (mRefreshTimeoutSourceID) {
         g_source_remove(mRefreshTimeoutSourceID);
      }
      mRefreshTimeoutSourceID =
         g_timeout_add(4000, RefreshDesktopsTimeout, this);
   } else {
      GetDesktops(true);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::RefreshDesktopsTimeout --
 *
 *      Handler for delay in initiating a desktop list refresh.
 *      Initiates the refresh.
 *
 * Results:
 *      false to remove the handler
 *
 * Side effects:
 *      get-desktops request sent 
 *
 *-----------------------------------------------------------------------------
 */

gboolean
Broker::RefreshDesktopsTimeout(gpointer data) // IN
{
   Broker *that = reinterpret_cast<Broker *>(data);
   ASSERT(that);

   that->GetDesktops(true);

   that->mRefreshTimeoutSourceID = 0;
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::Logout --
 *
 *      Notify the broker that we are done with this session.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI disabled, RPC in-flight.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::Logout()
{
   SetBusy(_("Logging out..."));
   mXml->Logout(boost::bind(&Broker::OnAbort, this, _1, _2),
                boost::bind(&Broker::OnLogoutResult, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetConfiguration --
 *
 *      Initiate a GetConfiguration RPC to the broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A GetConfiguration RPC in flight, or onAbort is called.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::GetConfiguration()
{
   SetBusy(_("Getting server configuration..."));
   mXml->GetConfiguration(
      boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
      boost::bind(&Broker::OnConfigurationDone, this, _1, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthResult --
 *
 *      Handle an AuthResult from the broker.  Simply pass the
 *      AuthInfo off to OnAuthInfo.
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
Broker::OnAuthResult(BrokerXml::Result &result,   // IN
                     BrokerXml::AuthResult &auth) // IN
{
   OnAuthInfo(result, auth.authInfo);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnConfigurationDone --
 *
 *      Handle a Configuration from the broker.  Simply pass the
 *      AuthInfo off to OnAuthInfo.
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
Broker::OnConfigurationDone(BrokerXml::Result &result,        // IN
                            BrokerXml::Configuration &config) // IN
{
   OnAuthInfo(result, config.authInfo, true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthInfo --
 *
 *      Handles the reply to an RPC that returns AuthInfo.  Either
 *      displays an error and stays on the same state, or continues
 *      based on what the server requires next.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      New UI page may be requested and enables the UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAuthInfo(BrokerXml::Result &result,     // IN
                   BrokerXml::AuthInfo &authInfo, // IN
                   bool treatOkAsPartial)         // IN
{
   Log("Auth Info: Name: %s, result: %s\n",
       authInfo.name.c_str(), result.result.c_str());

   if (result.result == "ok" && !treatOkAsPartial) {
      OnAuthComplete();
   } else if (result.result == "partial" || result.result == "ok") {
      SetReady();
      const Util::string error = authInfo.GetError();
      if (!error.empty()) {
         App::ShowDialog(GTK_MESSAGE_ERROR, _("Error authenticating: %s"),
                         error.c_str());
      }

      if (authInfo.GetAuthType() != BrokerXml::AUTH_DISCLAIMER &&
          mCertState == CERT_REQUESTED) {
         /*
          * No pre-login message, and cert auth optional.  We need to
          * forget the cookies here, because get-configuration has
          * moved in its auth chain past certificate auth to
          * password/etc. auth.  If we do a get-configuration again,
          * it will remember that it had past cert auth and just ask
          * for a password again, without looking at the certificate
          * we gave it.
          */
         Log("Got non-disclaimer auth method and cert was previously requested;"
             " enabling cert response and retrying GetConfiguration()...\n");
         mXml->ForgetCookies();
         mXml->ResetConnections();
         mCertState = CERT_SHOULD_RESPOND;
         GetConfiguration();
         return;
      }

      switch (authInfo.GetAuthType()) {
      case BrokerXml::AUTH_DISCLAIMER:
         RequestDisclaimer(authInfo.GetDisclaimer());
         break;
      case BrokerXml::AUTH_SECURID_PASSCODE:
         RequestPasscode(mUsername);
         break;
      case BrokerXml::AUTH_SECURID_NEXTTOKENCODE:
         RequestNextTokencode(mUsername);
         break;
      case BrokerXml::AUTH_SECURID_PINCHANGE:
         // This is a bit complicated, so defer to another function
         OnAuthInfoPinChange(authInfo.params);
         break;
      case BrokerXml::AUTH_SECURID_WAIT:
         App::ShowDialog(GTK_MESSAGE_INFO,
                         _("Your new RSA SecurID PIN has been set.\n\n"
                           "Please wait for the next tokencode to appear"
                           " on your RSA SecurID token, then continue."));
         RequestPasscode(mUsername);
         break;
      case BrokerXml::AUTH_WINDOWS_PASSWORD: {
         bool readOnly = false;
         Util::string user = authInfo.GetUsername(&readOnly);
         RequestPassword(user.empty() ? mUsername : user, readOnly,
                         authInfo.GetDomains(), mDomain);
         break;
      }
      case BrokerXml::AUTH_WINDOWS_PASSWORD_EXPIRED:
         mUsername = authInfo.GetUsername();
         RequestPasswordChange(mUsername, mDomain);
         break;
      case BrokerXml::AUTH_CERT_AUTH:
         mXml->SubmitCertAuth(
            true, boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
            boost::bind(&Broker::OnAuthResult, this, _1, _2));
         break;
      default:
         App::ShowDialog(GTK_MESSAGE_ERROR,
                         _("Unknown authentication method requested: %s"),
                         authInfo.name.c_str());
         RequestBroker();
         break;
      }
   } else {
      App::ShowDialog(GTK_MESSAGE_ERROR, _("Unknown result returned: %s"),
                      result.result.c_str());
      SetReady();
      RequestBroker();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthInfoPinChange --
 *
 *      Handle a response indicating the user needs to change their
 *      PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Displays PIN change page.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAuthInfoPinChange(std::vector<BrokerXml::Param> &params) // IN
{
   Util::string message = "";
   Util::string pin = "";
   bool userSelectable = true;
   for (std::vector<BrokerXml::Param>::iterator i = params.begin();
        i != params.end(); i++) {
      // Just assume a single value; that's currently always the case.
      if (i->values.size() != 1) {
         break;
      }
      Util::string value = i->values[0];
      if (i->name == "user-selectable") {
         userSelectable = value != "CANNOT_CHOOSE_PIN";
      } else if (i->name == "message") {
         message = value;
      } else if (i->name == "pin1") {
         pin = value;
      }
      // Ignore other param names, like "error" (which we've already handled).
   }
   if (!userSelectable && pin.empty()) {
      App::ShowDialog(GTK_MESSAGE_ERROR,
                      _("Invalid PIN Change response sent by server."));
   } else {
      RequestPinChange(pin, message, userSelectable);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAuthComplete --
 *
 *      Handle the next phase of broker interaction following a successful (or
 *      skipped) authentication step.  If the broker is version >1, initialize
 *      the tunnel.  Begins loading the desktop list.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Displays PIN change page.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAuthComplete()
{
   /*
    * If this is a 1.0 broker, it'll get upset if we send both
    * the tunnel and desktop list requests at the same time. So
    * we'll set up the tunnel after we get the desktop list. See
    * bug 311999.
    */
   if (mXml->GetBrokerVersion() != BrokerXml::VERSION_1) {
      InitTunnel();
   }
   GetDesktops();
   SetBusy(_("Getting desktop list..."));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetDesktops --
 *
 *      Initiates get-desktops request. If refresh is true, the done handler
 *      will refresh the list instead of populating it from scratch.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      RPC request sent.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::GetDesktops(bool refresh) // IN/OPT
{
   if (!mGettingDesktops || !refresh) {
      mGettingDesktops = true;
      mXml->GetDesktops(
         boost::bind(&Broker::OnAbort, this, _1, _2),
         boost::bind(refresh ? &Broker::OnGetDesktopsRefresh :
                        &Broker::OnGetDesktopsSet, this, _2));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::InitTunnel --
 *
 *      Get tunnel connection info from the broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      get-tunnel-connection RPC dispatched.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::InitTunnel()
{
   ASSERT(!mTunnel);

   mTunnel = new Tunnel();
   mTunnel->onReady.connect(boost::bind(&Broker::OnTunnelConnected, this));
   mTunnelDisconnectCnx = mTunnel->onDisconnect.connect(
      boost::bind(&Broker::OnTunnelDisconnect, this, _1, _2));

   mXml->GetTunnelConnection(
      boost::bind(&Broker::OnTunnelRPCAbort, this, _1, _2),
      boost::bind(&Tunnel::Connect, mTunnel, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnTunnelConnected --
 *
 *      Callback when a tunnel has been created and connected.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The tunnel disconnecting will now exit; if the user was
 *      waiting for the tunnel, connect to the desktop.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnTunnelConnected()
{
   ASSERT(mTunnel);
   ASSERT(mTunnel->GetIsConnected());
   if (mDesktop &&
       mDesktop->GetConnectionState() == Desktop::STATE_DISCONNECTED) {
      ConnectDesktop(mDesktop);
   } else {
      MaybeLaunchDesktop();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnTunnelDisconnect --
 *
 *      Handler for the tunnel exiting.  If it was due to an error,
 *      restart it (if it isn't throttled).
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
Broker::OnTunnelDisconnect(int status,                    // IN
                           Util::string disconnectReason) // IN
{
   delete mTunnel;
   mTunnel = NULL;
   if (disconnectReason.empty() && status &&
       !mTunnelMonitor.ShouldThrottle()) {
      InitTunnel();
   } else {
      TunnelDisconnected(disconnectReason);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnGetDesktopsSet --
 *
 *      Done handler for getting list of desktops. Sets up the tunnel;
 *      offers the list to the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Displays desktop list, enables UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnGetDesktopsSet(BrokerXml::EntitledDesktops &desktops) // IN
{
   mGettingDesktops = false;
   // Since we didn't do this in OnAuthInfo, let's do it now.
   if (mXml->GetBrokerVersion() == BrokerXml::VERSION_1 &&
       !mTunnel) {
      InitTunnel();
   }

   std::vector<Desktop *> newDesktops;
   for (BrokerXml::DesktopList::iterator i = desktops.desktops.begin();
        i != desktops.desktops.end(); i++) {
      Desktop *desktop = new Desktop(*mXml, *i);
      newDesktops.push_back(desktop);
      desktop->changed.connect(boost::bind(&Broker::UpdateDesktops, this));
   }

   mDesktops = newDesktops;
   SetReady();
   // This is a superset of UpdateDesktops().
   RequestDesktop();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnGetDesktopsRefresh --
 *
 *      Done handler for getting list of desktops. Refreshes desktops in the
 *      list, updating their information, adding and removing as necessary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Updates UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnGetDesktopsRefresh(BrokerXml::EntitledDesktops &desktops) // IN
{
   mGettingDesktops = false;
   std::vector<Desktop *> newDesktops;
   for (BrokerXml::DesktopList::iterator i = desktops.desktops.begin();
        i != desktops.desktops.end(); i++) {
      bool found = false;
      for (std::vector<Desktop *>::iterator j = mDesktops.begin();
           j != mDesktops.end(); j++) {
         if (i->id == (*j)->GetID()) {
            Desktop *desktop = *j;
            mDesktops.erase(j);
            desktop->SetInfo(*i);
            newDesktops.push_back(desktop);
            found = true;
            break;
         }
      }
      if (!found) {
         // New desktop--add it.
         Desktop *desktop = new Desktop(*mXml, *i);
         newDesktops.push_back(desktop);
         desktop->changed.connect(boost::bind(&Broker::UpdateDesktops, this));
      }
   }
   // Delete desktops that aren't still in the list.
   for (std::vector<Desktop *>::iterator i = mDesktops.begin();
        i != mDesktops.end(); i++) {
      delete *i;
   }
   mDesktops.clear();

   mDesktops = newDesktops;
   UpdateDesktops();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::MaybeLaunchDesktop --
 *
 *      Callback for getting a desktop connection.  If we are all set
 *      to start the desktop, do so.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      rdesktop session may be requested.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::MaybeLaunchDesktop()
{
   if (GetTunnelReady() && GetDesktopReady()) {
      RequestLaunchDesktop(mDesktop);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnLogoutResult --
 *
 *      Handler for Logout RPC.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Exits.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnLogoutResult()
{
   SetReady();
   Quit();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnAbort --
 *
 *      Handle an error from an RPC.  This could be the user
 *      cancelling, a network error, an error returned by the broker
 *      to an RPC, or some unhandled or unexpected response by the
 *      broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Enables the UI.  If the user didn't cancel, displays an error
 *      and may go back to the broker page.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnAbort(bool cancelled,      // IN
                Util::exception err) // IN
{
   // Update list to reflect reset Desktop ConnectionState values on failure.
   UpdateDesktops();

   SetReady();
   if (cancelled) {
      return;
   }
   if (err.code() == ERR_AUTHENTICATION_FAILED) {
      RequestBroker();
      App::ShowDialog(GTK_MESSAGE_ERROR, _("Error authenticating: %s"),
                      err.what());
   } else if (err.code() == "NOT_AUTHENTICATED") {
      RequestBroker();
      App::ShowDialog(GTK_MESSAGE_ERROR,
                      _("The View Server has logged you out."));
   } else {
      if (err.code() == "NOT_ENTITLED") {
         // This probably means we have out-of-date information; refresh.
         GetDesktops(true);
      }
      App::ShowDialog(GTK_MESSAGE_ERROR, "%s", err.what());
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnInitialRPCAbort --
 *
 *      Failure handler for our first request.  If the server doesn't support
 *      this RPC, drop the broker into protocol 1.0 mode.  If we're already
 *      authenticated, continue as normal.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      mXml may be set to protocol 1.0.
 *      GetConfiguration RPC is issued.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnInitialRPCAbort(bool cancelled,      // IN
                          Util::exception err) // IN
{
   if (!cancelled) {
      if (err.code() == ERR_AUTHENTICATION_FAILED &&
          mCertState == CERT_REQUESTED) {
         /*
          * No pre-login message, and cert auth required.  We have
          * failed authentication, since we hadn't sent a cert, so we
          * can simply restart authentication.  The server probably
          * doesn't care about our cookie anymore.
          */
         Log("Got auth failure and a cert was requested; "
             "enabling cert response and retrying GetConfiguration()...\n");
         mXml->ResetConnections();
         mCertState = CERT_SHOULD_RESPOND;
         GetConfiguration();
         return;
      }

      if (err.code() == ERR_UNSUPPORTED_VERSION) {
         switch (mXml->GetBrokerVersion()) {
         case BrokerXml::VERSION_3:
            mXml->SetBrokerVersion(BrokerXml::VERSION_2);
            SetLocale();
            return;
         case BrokerXml::VERSION_2:
            mXml->SetBrokerVersion(BrokerXml::VERSION_1);
            // Don't retry SetLocale, as 1.0 doesn't support it at all.
            GetConfiguration();
            return;
         default:
            break;
         }
      } else if (err.code() == ERR_ALREADY_AUTHENTICATED) {
         OnAuthComplete();
         return;
      }
   }

   Reset();
   OnAbort(cancelled, err);
   // Cert selection may have changed the dialog state.
   RequestBroker();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnTunnelRPCAbort --
 *
 *      RPC abort handler for tunnel connection RPCs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Deletes mTunnel.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnTunnelRPCAbort(bool cancelled,      // IN
                         Util::exception err) // IN
{
   delete mTunnel;
   mTunnel = NULL;
   OnAbort(cancelled, err);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetTunnelReady --
 *
 *      Determine whether we're waiting on a tunnel connection.
 *
 * Results:
 *      true if the tunnel is not required, or is connected.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Broker::GetTunnelReady()
{
   return mTunnel && mTunnel->GetIsConnected();
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetDesktopReady --
 *
 *      Determine whether we're waiting on a desktop connection.
 *
 * Results:
 *      true if we have a connected desktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Broker::GetDesktopReady()
{
   return mDesktop &&
      mDesktop->GetConnectionState() == Desktop::STATE_CONNECTED;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnCertificateRequested --
 *
 *      SSL callback when a certificate is requested from the server.
 *      Search all cryptoki devices for certs, and ask the user to
 *      choose one.
 *
 * Results:
 *      0: Do not use a certificate.
 *      1: Use the provided certificate.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
Broker::OnCertificateRequested(SSL *ssl,           // IN
                               X509 **x509,        // OUT
                               EVP_PKEY **privKey) // OUT
{
   switch (mCertState) {
   case CERT_NOT_REQUESTED:
      /*
       * See bug 368087.  The broker will request a certificate from
       * us before it hits the certificate authentication module, but
       * we don't want to bother the user until after the pre-login
       * text is displayed (if there is any).  This defers prompting
       * the user until other logic, scattered around, determines that
       * we should do so.
       */
      mCertState = CERT_REQUESTED;
      // fall through...
   case CERT_DID_RESPOND:
   case CERT_REQUESTED:
      return 0;
   case CERT_SHOULD_RESPOND:
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }

   if (!mCryptoki) {
      mCryptoki = new Cryptoki();
      mCryptoki->requestPin.connect(boost::bind(&Broker::OnScPinRequested,
                                                this, _1, _2));
      mCryptoki->LoadModules(LIBDIR"/vmware/view/pkcs11");
   }

   std::list<X509 *> certs;
requestCerts:
   Cryptoki::FreeCertificates(certs);

   if (mCryptoki->GetHasSlots() && !mCryptoki->GetHasTokens()) {
      RequestScInsertPrompt(mCryptoki);
      // block the caller until we have an answer.
      gtk_main();
      // Set by SubmitScInsertPrompt() if the user canceled.
      if (mCertState == CERT_DID_RESPOND) {
         return 0;
      }
   }
   mCertState = CERT_DID_RESPOND;

   certs = mCryptoki->GetCertificates(SSL_get_client_CA_list(ssl));
   switch (certs.size()) {
   case 0:
      return 0;
   case 1:
      mX509 = certs.front();
      break;
   default:
      RequestCertificate(certs);
      StartWatchingForTokenEvents();
      // We need to block the caller until we have an answer.
      gtk_main();
      StopWatchingForTokenEvents();
      if (mCertState == CERT_SHOULD_RESPOND) {
         goto requestCerts;
      }
      break;
   }

   *x509 = mX509;
   certs.remove(mX509);
   mX509 = NULL;
   Cryptoki::FreeCertificates(certs);

   if (*x509) {
      *privKey = mCryptoki->GetPrivateKey(*x509);
      if (mCertState == CERT_SHOULD_RESPOND) {
         X509_free(*x509);
         *x509 = NULL;
         goto requestCerts;
      }
   }

   Log("Returning cert: %p key: %p\n", *x509, *privKey);

   return *x509 && *privKey ? 1 : 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnScPinRequested --
 *
 *      Cryptoki callback for when a PIN is requested.  Ask App for
 *      the PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
Broker::OnScPinRequested(const Util::string &label, // IN
                         const X509 *x509)          // IN
{
   RequestScPin(label, x509);
   StartWatchingForTokenEvents();
   // We need to block the caller until we have an answer.
   gtk_main();
   StopWatchingForTokenEvents();
   char *ret = mPin;
   mPin = NULL;
   // the caller frees ret.
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetSmartCardRedirects --
 *
 *      Get rdesktop redirect arguments for the smart cards we know
 *      about.
 *
 * Results:
 *      A list of strings that should be used with the -r option of
 *      rdesktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
Broker::GetSmartCardRedirects()
{
   std::vector<Util::string> names;
   if (mCryptoki) {
      names = mCryptoki->GetSlotNames();
      for (std::vector<Util::string>::iterator i = names.begin();
           i != names.end(); i++) {
         *i = "scard:" + *i;
      }
   }
   return names;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::StartWatchingForTokenEvents --
 *
 *      Adds a timeout which monitors for smart card event when the
 *      user is on a smart card related screen.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      TokenEventMonitor() will be called in timeouts.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::StartWatchingForTokenEvents()
{
   ASSERT(mTokenEventTimeout == 0);

   // Ignore any currently pending events.
   while (mCryptoki->GetHadEvent()) { }

   mTokenEventTimeout = g_timeout_add(TOKEN_EVENT_TIMEOUT_MS,
                                      TokenEventMonitor, this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::StopWatchingForTokenEvent --
 *
 *      Removes the token timeout poll.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      TokenEventMonitor() will no longer be called.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::StopWatchingForTokenEvents()
{
   if (mTokenEventTimeout) {
      g_source_remove(mTokenEventTimeout);
      mTokenEventTimeout = 0;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::TokenEventMonitor --
 *
 *      If any token had an event, quit the main loop so that the
 *      token auth detection process is restarted.
 *
 * Results:
 *      false if the source should be removed.
 *
 * Side effects:
 *      A main loop is quit if false is returned.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
Broker::TokenEventMonitor(gpointer data) // IN
{
   Broker *that = reinterpret_cast<Broker *>(data);
   ASSERT(that);

   ASSERT(that->mCertState == CERT_DID_RESPOND);

   if (!that->mCryptoki->GetHadEvent()) {
      return true;
   }

   that->mCertState = CERT_SHOULD_RESPOND;
   that->mTokenEventTimeout = 0;
   gtk_main_quit();
   return false;
}


} // namespace cdk
