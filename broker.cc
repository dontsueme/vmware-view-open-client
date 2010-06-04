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
#include <glib.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "baseApp.hh"
#include "broker.hh"
#include "cdkErrors.h"
#include "desktop.hh"
#include "tunnel.hh"
#include "util.hh"


#define ERR_ALREADY_AUTHENTICATED "ALREADY_AUTHENTICATED"
#define ERR_AUTHENTICATION_FAILED "AUTHENTICATION_FAILED"
#define ERR_BASICHTTP_ERROR_SSL_CONNECT_ERROR "BASICHTTP_ERROR_SSL_CONNECT_ERROR"
#define ERR_DESKTOP_LAUNCH_ERROR "DESKTOP_LAUNCH_ERROR"
#define ERR_TUNNEL_ERROR "TUNNEL_ERROR"
#define ERR_NOT_AUTHENTICATED "NOT_AUTHENTICATED"
#define ERR_NOT_ENTITLED "NOT_ENTITLED"
#define ERR_UNSUPPORTED_VERSION "UNSUPPORTED_VERSION"


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
   : mDelegate(NULL),
     mXml(NULL),
     mTunnel(NULL),
     mDesktop(NULL),
     mCertState(CERT_NOT_REQUESTED),
     mTunnelState(TUNNEL_DOWN),
     mGettingDesktops(false),
     mSmartCardPin(NULL),
     mAuthRequestId(0),
     mAcceptedDisclaimer(false),
     mCert(NULL),
     mKey(NULL)
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
   Reset();
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
   Poll_CallbackRemove(POLL_CS_MAIN, 0, RefreshDesktopsTimeout, this,
                       POLL_REALTIME);

   /*
    * The abort handlers here could access Desktops, so cancel them
    * before deleting the desktops.
   */
   if (mXml) {
      mXml->CancelRequests();
   }

   for (std::vector<Desktop*>::iterator i = mDesktops.begin();
        i != mDesktops.end(); i++) {
      delete *i;
   }
   mDesktops.clear();
   // already deleted above
   mDesktop = NULL;

   ResetTunnel();
   mTunnelMonitor.Reset();

   delete mXml;
   mXml = NULL;
   mAuthRequestId = 0;

   ClearSmartCardPinAndReader();

   mCertState = CERT_NOT_REQUESTED;
   mAcceptedDisclaimer = false;

   mTrustedIssuers.clear();

   X509_free(mCert);
   mCert = NULL;

   EVP_PKEY_free(mKey);
   mKey = NULL;
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

   Log("Initializing connection to broker %s://%s:%d\n",
       secure ? "https" : "http", hostname.c_str(), port);

   mXml = CreateNewXmlConnection(hostname, port, secure);
   if (!mCookieFile.empty()) {
      mXml->SetCookieFile(mCookieFile);
      mXml->ForgetCookies();
   }
   mXml->certificateRequested.connect(
      boost::bind(&Broker::OnCertificateRequested, this, _1, _2, _3));
   mUsername = defaultUser;
   mDomain = defaultDomain;

   mXml->QueueRequests();
   SetLocale();
   GetConfiguration();
   mXml->SendQueuedRequests();
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
#ifndef __APPLE__
   const char *locale = setlocale(LC_MESSAGES, NULL);
#else
   char locale[32] = { '\0' };
   CFArrayRef langs = CFLocaleCopyPreferredLanguages();
   CFStringRef localeStr = (CFStringRef)CFArrayGetValueAtIndex(langs, 0);
   if (CFStringGetCString(localeStr, locale, sizeof(locale),
                          kCFStringEncodingASCII)) {
#endif
      if (locale != NULL && locale[0] != '\0' && strcmp(locale, "C") != 0 &&
          strcmp(locale, "POSIX") != 0) {
         mXml->SetLocale(locale,
                         boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
                         boost::bind(&Broker::OnLocaleSet, this));
      }
#ifdef __APPLE__
   }
   CFRelease(langs);
#endif
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
   mAcceptedDisclaimer = true;
   if (mCertState == CERT_REQUESTED) {
      /*
       * Pre-login message is enabled, and the server asked us for a
       * cert.  We'll reset our connections, and when we do the accept
       * disclaimer RPC, we'll get asked for a cert again.
       */
      Log("Accepting disclaimer and cert was requested; prompting user for a "
          "certificate.\n");
      if (mDelegate) {
         mDelegate->RequestCertificate(mTrustedIssuers);
      }
   } else {
      mXml->QueueRequests();
      mXml->AcceptDisclaimer(
         boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
         boost::bind(&Broker::OnAuthResult, this, _1, _2));
      InitTunnel();
      GetDesktops();
      mXml->SendQueuedRequests();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::SubmitCertificate --
 *
 *      Set the certificate and private key to use when
 *      authenticating, and smart card PIN and reader name (if
 *      available).
 *
 *      NULL cert or key signifies not to authenticate using a
 *      certificate.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Authentication will continue.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::SubmitCertificate(X509 *cert,                 // IN/OPT
                          EVP_PKEY *key,              // IN/OPT
                          const char *pin,            // IN/OPT
                          const Util::string &reader) // IN/OPT
{
   ASSERT(mCertState == CERT_REQUESTED);
   ASSERT(!mCert);
   ASSERT(!mKey);
   ASSERT(!mSmartCardPin);
   ASSERT(mSmartCardReader.empty());

   mCert = cert;
   mKey = key;
   mSmartCardPin = g_strdup(pin);
   mSmartCardReader = reader;

   mCertState = CERT_SHOULD_RESPOND;
   mXml->ResetConnections();

   if (mAcceptedDisclaimer) {
      Log("Accepting disclaimer with cert response enabled.\n");
      mXml->QueueRequests();
      mXml->AcceptDisclaimer(
         boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
         boost::bind(&Broker::OnAuthResult, this, _1, _2));
      InitTunnel();
      GetDesktops();
      mXml->SendQueuedRequests();
   } else {
      Log("Getting configuration with cert response enabled.\n");
      GetConfiguration();
   }
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
   mUsername = username;
   mXml->QueueRequests();
   mXml->SecurIDUsernamePasscode(
      username, passcode,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
   InitTunnel();
   GetDesktops();
   mXml->SendQueuedRequests();
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
   mXml->QueueRequests();
   mXml->SecurIDNextTokencode(
      tokencode,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
   InitTunnel();
   GetDesktops();
   mXml->SendQueuedRequests();
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
   mXml->QueueRequests();
   mXml->SecurIDPins(
      pin1, pin2,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
   InitTunnel();
   GetDesktops();
   mXml->SendQueuedRequests();
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
   mUsername = username;
   mDomain = domain;
   mXml->QueueRequests();
   mXml->PasswordAuthentication(
      username, password, domain,
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnAuthResult, this, _1, _2));
   InitTunnel();
   GetDesktops();
   mXml->SendQueuedRequests();
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
   mXml->QueueRequests();
   mXml->ChangePassword(oldPassword, newPassword, confirm,
                        boost::bind(&Broker::OnAbort, this, _1,_2),
                        boost::bind(&Broker::OnAuthResult, this, _1, _2));
   InitTunnel();
   GetDesktops();
   mXml->SendQueuedRequests();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ConnectDesktop --
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
   // XXX - Log/assert to determine cause of BZ 510532.
   if (desktop->GetConnectionState() != Desktop::STATE_DISCONNECTED) {
      Log("Broker::ConnectDesktop: unexpected desktop status "
          "(see BZ 510532) %d\n", (int)desktop->GetConnectionState());
      ASSERT_BUG(510532,
                 desktop->GetConnectionState() == Desktop::STATE_DISCONNECTED);
   }

   mDesktop = desktop;
   if (mDelegate) {
      mDelegate->RequestTransition(_("Connecting to the desktop..."));
   }
   switch (mTunnelState) {
   case TUNNEL_RUNNING:
      /*
       * Connecting to the desktop before the tunnel is connected
       * results in DESKTOP_NOT_AVAILABLE.
       */
      desktop->Connect(boost::bind(&Broker::OnAbort, this, _1, _2),
                       boost::bind(&Broker::MaybeLaunchDesktop, this),
                       Util::GetClientInfo(mXml->GetHostname(),
                                           mXml->GetPort()));
      break;
   case TUNNEL_DOWN:
      InitTunnel();
      break;
   default:
      break;
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

   // XXX - Log/assert to determine cause of BZ 510532.
   if (mDesktop->GetConnectionState() != Desktop::STATE_DISCONNECTED) {
      Log("Broker::ReconnectDesktop: unexpected desktop status "
          "(see BZ 510532) %d\n", (int)mDesktop->GetConnectionState());
      ASSERT_BUG(510532,
                 mDesktop->GetConnectionState() == Desktop::STATE_DISCONNECTED);
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

   desktop->ResetDesktop(boost::bind(&Broker::OnAbort, this, _1, _2),
                         boost::bind(&Broker::OnDesktopOpDone,
                                     this, desktop, andQuit));
   if (mDelegate && !andQuit) {
      mDelegate->UpdateDesktops();
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
                      boost::bind(&Broker::OnDesktopOpDone, this, desktop,
                                  false));
   mDelegate->UpdateDesktops();
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
                     boost::bind(&Broker::OnDesktopOpDone, this, desktop,
                                 false));
   mDelegate->UpdateDesktops();
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
 *      get-desktops request sent.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnDesktopOpDone(Desktop *desktop,   // IN
                        bool andDisconnect) // IN
{
   if (andDisconnect) {
      if (mDelegate) {
         mDelegate->Disconnect();
      }
      return;
   }
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
      Poll_CallbackRemove(POLL_CS_MAIN, 0, RefreshDesktopsTimeout, this,
                          POLL_REALTIME);
      Poll_Callback(POLL_CS_MAIN, 0, RefreshDesktopsTimeout, this,
                    POLL_REALTIME, 10 * 1000 * 1000, NULL);
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

void
Broker::RefreshDesktopsTimeout(void *data) // IN
{
   Broker *that = reinterpret_cast<Broker *>(data);
   ASSERT(that);

   that->GetDesktops(true);
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
   if (result.result == "ok" && mDelegate) {
      mDelegate->SetLogoutOnCertRemoval(auth.logoutOnCertRemoval);
   }
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

   mAuthRequestId = mXml->GetRequestId();

   if (result.result == "ok" && !treatOkAsPartial) {
      // get-desktops reply is later in this RPC.
      return;
   } else if (result.result != "partial" && result.result != "ok") {
      BaseApp::ShowError(CDK_ERR_AUTH_UNKNOWN_RESULT, _("Unknown result returned"),
                         "%s", result.result.c_str());
      if (mDelegate) {
         mDelegate->RequestBroker();
      }
      return;
   }

  /*
   * Reset the tunnel here because at this point, we were not authenticated
   * by the broker and the following notexecuted response to get-tunnel-connection
   * will simply be ignored.
   */
   ResetTunnel();

   const Util::string error = authInfo.GetError();
   if (!error.empty()) {
      BaseApp::ShowError(CDK_ERR_AUTH_ERROR,
                         _("Error authenticating"), "%s", error.c_str());
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
          " promting user for a certificate.\n");
      mXml->ForgetCookies();
      if (mDelegate) {
         mDelegate->RequestCertificate(mTrustedIssuers);
      }
      return;
   }

   switch (authInfo.GetAuthType()) {
   case BrokerXml::AUTH_DISCLAIMER:
      if (mDelegate) {
         mDelegate->RequestDisclaimer(authInfo.GetDisclaimer());
      }
      break;
   case BrokerXml::AUTH_SECURID_PASSCODE:
      if (mDelegate) {
         bool readOnly = false;
         Util::string username = authInfo.GetUsername(&readOnly);
         if (!username.empty()) {
            mUsername = username;
         }
         mDelegate->RequestPasscode(mUsername, !readOnly);
      }
      break;
   case BrokerXml::AUTH_SECURID_NEXTTOKENCODE:
      if (mDelegate) {
         mDelegate->RequestNextTokencode(mUsername);
      }
      break;
   case BrokerXml::AUTH_SECURID_PINCHANGE:
      // This is a bit complicated, so defer to another function
      OnAuthInfoPinChange(authInfo.params);
      break;
   case BrokerXml::AUTH_SECURID_WAIT:
      BaseApp::ShowInfo(_("Your new RSA SecurID PIN has been set"),
                        _("Please wait for the next tokencode to appear"
                          " on your RSA SecurID token, then continue."));
      if (mDelegate) {
         mDelegate->RequestPasscode(mUsername, false);
      }
      break;
   case BrokerXml::AUTH_WINDOWS_PASSWORD: {
      bool readOnly = false;
      Util::string user = authInfo.GetUsername(&readOnly);
      if (mDelegate) {
         mDelegate->RequestPassword(user.empty() ? mUsername : user, readOnly,
                                    authInfo.GetDomains(), mDomain);
      }
      break;
   }
   case BrokerXml::AUTH_WINDOWS_PASSWORD_EXPIRED:
      mUsername = authInfo.GetUsername();
      if (mDelegate) {
         mDelegate->RequestPasswordChange(mUsername, mDomain);
      }
      break;
   case BrokerXml::AUTH_CERT_AUTH:
      mXml->QueueRequests();
      mXml->SubmitCertAuth(
         true, mSmartCardPin, mSmartCardReader,
         boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
         boost::bind(&Broker::OnAuthResult, this, _1, _2));
      InitTunnel();
      GetDesktops();
      mXml->SendQueuedRequests();
      ClearSmartCardPinAndReader();
      break;
   default:
      BaseApp::ShowError(CDK_ERR_AUTH_UNKNOWN_METHOD_REQUEST,
                         _("Unknown authentication method requested"),
                         _("Received unknown request \"%s\" from the broker"),
                         authInfo.name.c_str());
      if (mDelegate) {
         mDelegate->RequestBroker();
      }
      break;
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
      BaseApp::ShowError(CDK_ERR_INVALID_SERVER_RESPONSE,
                         _("Invalid response from server"),
                         _("Invalid PIN Change response sent by server."));
   } else if (mDelegate) {
      mDelegate->RequestPinChange(pin, message, userSelectable);
   }
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
         mSupportedProtocols,
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
   /*
    * Ensure we have a clean state for the tunnel.
    */
   ResetTunnel();
   mTunnelState = TUNNEL_GETTING_INFO;
   mXml->GetTunnelConnection(
      boost::bind(&Broker::OnAbort, this, _1, _2),
      boost::bind(&Broker::OnGetTunnelConnectionDone, this, _2));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ResetTunnel --
 *
 *      Reset tunnel state to allow a new tunnel connection.
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
Broker::ResetTunnel()
{
   mTunnelState = TUNNEL_DOWN;
   mTunnelDisconnectCnx.disconnect();
   delete mTunnel;
   mTunnel = NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnGetTunnelConnectionDone --
 *
 *      Done handler for get-tunnel-connection - creates a tunnel and
 *      connects it to the tunnel server, if direct connect is not
 *      enabled.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tunnel may be started & connected.
 *
 *-----------------------------------------------------------------------------
 */

void
Broker::OnGetTunnelConnectionDone(BrokerXml::Tunnel &tunnel) // IN
{
   mTunnelDisconnectCnx.disconnect();
   delete mTunnel;

   ASSERT(mTunnelState == TUNNEL_GETTING_INFO);
   mTunnelState = TUNNEL_CONNECTING;
   mTunnel = new Tunnel();
   mTunnel->onReady.connect(boost::bind(&Broker::OnTunnelConnected, this));
   mTunnelDisconnectCnx = mTunnel->onDisconnect.connect(
      boost::bind(&Broker::OnTunnelDisconnect, this, _1, _2));
   mTunnel->Connect(tunnel);
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
   ASSERT(mTunnelState == TUNNEL_CONNECTING);
   mTunnelState = TUNNEL_RUNNING;
   ASSERT(GetTunnelReady());
   if (mDesktop) {
      switch (mDesktop->GetConnectionState()) {
      case Desktop::STATE_DISCONNECTED:
         ConnectDesktop(mDesktop);
         return;
      case Desktop::STATE_CONNECTED:
         // App will likely respawn the desktop if desired.
         mDesktop->Disconnect();
         return;
      default:
         break;
      }
   }
   MaybeLaunchDesktop();
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
   // ResetTunnel resets the monitor too
   bool shouldThrottle = mTunnelMonitor.ShouldThrottle();

   ResetTunnel();

   if (disconnectReason.empty() && status && !shouldThrottle) {
      InitTunnel();
   } else {
      mTunnelMonitor.Reset();
      if (mDelegate) {
         mDelegate->TunnelDisconnected(disconnectReason);
      }
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

   if (desktops.desktops.empty()) {
      OnAbort(false,
              Util::exception(_("You are not entitled to use the system."),
                              ERR_AUTHENTICATION_FAILED));
      return;
   }

   std::vector<Desktop *> newDesktops;
   for (BrokerXml::DesktopList::iterator i = desktops.desktops.begin();
        i != desktops.desktops.end(); i++) {
      Desktop *desktop = new Desktop(*mXml, *i);
      newDesktops.push_back(desktop);
      desktop->changed.connect(boost::bind(&Broker::Delegate::UpdateDesktops,
                                           mDelegate));
   }

   mDesktops = newDesktops;
   if (mDelegate) {
      // This is a superset of UpdateDesktops().
      mDelegate->RequestDesktop();
   }
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

   if (desktops.desktops.empty()) {
      OnAbort(false,
              Util::exception(_("You are not entitled to use the system."),
                              ERR_AUTHENTICATION_FAILED));
      return;
   }

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
         desktop->changed.connect(boost::bind(&Broker::Delegate::UpdateDesktops,
                                              mDelegate));
      }
   }
   // Delete desktops that aren't still in the list.
   for (std::vector<Desktop *>::iterator i = mDesktops.begin();
        i != mDesktops.end(); i++) {
      delete *i;
   }
   mDesktops.clear();

   mDesktops = newDesktops;
   if (mDelegate) {
      mDelegate->UpdateDesktops();
   }
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
   if (mDelegate && GetTunnelReady() && GetDesktopReady()) {
      mDelegate->RequestLaunchDesktop(mDesktop);
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
   if (mDelegate) {
      mDelegate->Disconnect();
   }
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
   bool showGenericError = false;

   // Update list to reflect reset Desktop ConnectionState values on failure.
   if (mDelegate) {
      mDelegate->UpdateDesktops();
   }
   if (cancelled) {
      return;
   }

   if (err.code() == ERR_AUTHENTICATION_FAILED) {
      BaseApp::ShowError(CDK_ERR_AUTH_ERROR, _("Error authenticating"), "%s", err.what());
      if (mDelegate) {
         mDelegate->RequestBroker();
      }
   } else if (err.code() == ERR_NOT_AUTHENTICATED) {
      /*
       * VDM 2.0 sends NOT_AUTHENTICATED instead of notexecuted for
       * requests that need auth if the corresponding
       * submit-authentication partially failed.
       *
       * So, we ignore these errors when we're in a multi-rpc that
       * we've already had an auth result for (bz 471680).
       */
      if (mXml->GetProtocolVersion() != BaseXml::VERSION_1 ||
          mXml->GetRequestId() != mAuthRequestId) {
         Util::string hostname = mXml->GetHostname();
         int port = mXml->GetPort();
         bool secure = mXml->GetSecure();
         Reset();
         Initialize(hostname, port, secure, mUsername, mDomain);
      }
   } else if (err.code() == ERR_DESKTOP_LAUNCH_ERROR &&
              mTunnel && mTunnel->GetIsBypassed() &&
              !mTunnelMonitor.ShouldThrottle()) {
      /*
       * The linux vdm client is hacked to attempt reestablishing a
       * tunnel connection if the existing tunnel connection was
       * offline-bypassed and the get-destkop-connection fails with
       * "DESKTOP_LAUNCH_ERROR".  This covers the case where the
       * connection is reestablished and the user attempts to connect
       * to a remote desktop.
       */
      /*
       * XXX: Going this route is very slow, as the broker waits for tunnel
       *      setup before failing the desktop connection.
       */
      ResetTunnel();
      InitTunnel();
   } else if (err.code() == ERR_NOT_ENTITLED) {
      // This probably means we have out-of-date information; refresh.
      GetDesktops(true);
      showGenericError = true;
   } else if (err.code() == ERR_TUNNEL_ERROR) {
      ASSERT(mTunnelState == TUNNEL_GETTING_INFO);
      ResetTunnel();
      showGenericError = true;
   } else {
      showGenericError = true;
   }

   if (showGenericError) {
      Util::string errorDetails = err.details();
      if (errorDetails.empty()) {
         BaseApp::ShowError(CDK_ERR_AUTH_ERROR, _("An error occurred"),
                            "%s", _(err.what()));
      } else {
         BaseApp::ShowError(CDK_ERR_AUTH_ERROR, _(err.what()), "%s",
                            _(errorDetails.c_str()));
      }

      if (mDelegate) {
         mDelegate->SetReady();
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::OnInitialRPCAbort --
 *
 *      Failure handler for our first request.  If the server doesn't support
 *      this RPC, drop the broker into protocol 1.0 mode.  If we're already
 *      authenticated (e.g. via cvpa cookie state), continue as normal.
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
             "prompting user for a certificate.\n");
         if (mDelegate) {
            mDelegate->RequestCertificate(mTrustedIssuers);
         }
         return;
      }

      if (err.code() == ERR_UNSUPPORTED_VERSION) {
         switch (mXml->GetProtocolVersion()) {
         case BrokerXml::VERSION_4_5:
            mXml->SetProtocolVersion(BrokerXml::VERSION_4);
            goto retry_with_setlocale;
         case BrokerXml::VERSION_4:
            mXml->SetProtocolVersion(BrokerXml::VERSION_3);
            goto retry_with_setlocale;
         case BrokerXml::VERSION_3:
            mXml->SetProtocolVersion(BrokerXml::VERSION_2);
         retry_with_setlocale:
            mXml->QueueRequests();
            SetLocale();
            GetConfiguration();
            mXml->SendQueuedRequests();
            return;
         case BrokerXml::VERSION_2:
            mXml->SetProtocolVersion(BrokerXml::VERSION_1);
            // Don't retry SetLocale, as 1.0 doesn't support it at all.
            GetConfiguration();
            return;
         default:
            break;
         }
      } else if (err.code() == ERR_ALREADY_AUTHENTICATED) {
         if (mTunnel) {
            GetDesktops();
         } else {
            mXml->QueueRequests();
            InitTunnel();
            GetDesktops();
            mXml->SendQueuedRequests();
         }
         return;
      } else if (err.code() == ERR_BASICHTTP_ERROR_SSL_CONNECT_ERROR &&
                 mCertState == CERT_DID_RESPOND) {
         mXml->ResetConnections();
         if (mAcceptedDisclaimer) {
            Log("Accepting disclaimer and cert response failed; "
                "disabling cert response and accepting disclaimer "
                "again.\n");
            mXml->QueueRequests();
            mXml->AcceptDisclaimer(
               boost::bind(&Broker::OnInitialRPCAbort, this, _1, _2),
               boost::bind(&Broker::OnAuthResult, this, _1, _2));
            InitTunnel();
            GetDesktops();
            mXml->SendQueuedRequests();
         } else {
            Log("No disclaimer seen, but cert response failed; just trying to "
                "GetConfiguration() again.\n");
            GetConfiguration();
         }
         return;
      }
   }

   /*
    * After OnAbort() is called, we may be deleted, so save the
    * delegate so we can reset it to the broker page (if it was doing
    * smart card auth, for example).
    */
   Delegate *delegate = mDelegate;
   Reset();
   OnAbort(cancelled, err);
   if (delegate) {
      delegate->RequestBroker();
   }
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
      {
         /*
          * Cache the issuers since they almost certainly won't change
          * between this request and the one we later use to actually
          * do certificate authentication.
          */
         STACK_OF(X509_NAME) *issuers = SSL_get_client_CA_list(ssl);
         ASSERT(mTrustedIssuers.empty());
         for (int i = 0; i < sk_X509_NAME_num(issuers); i++) {
            X509_NAME *issuer = sk_X509_NAME_value(issuers, i);
            if (issuer) {
               char *dispName = X509_NAME_oneline(issuer, NULL, 0);
               if (dispName && strcmp(dispName, "NO X509_NAME")) {
                  mTrustedIssuers.push_back(dispName);
               }
               OPENSSL_free(dispName);
            }
         }
      }
      // fall through...
   case CERT_DID_RESPOND:
   case CERT_REQUESTED:
      break;
   case CERT_SHOULD_RESPOND:
      *x509 = mCert;
      mCert = NULL;
      *privKey = mKey;
      mKey = NULL;
      if (!*x509 || !*privKey) {
         ClearSmartCardPinAndReader();
      }
      mCertState = CERT_DID_RESPOND;
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }
   Log("Returning cert: %p key: %p\n", *x509, *privKey);
   return *x509 && *privKey ? 1 : 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::ClearSmartCardPinAndReader --
 *
 *      Overwrites and frees the PIN and clears the Reader.
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
Broker::ClearSmartCardPinAndReader()
{
   if (mSmartCardPin) {
      ZERO_STRING(mSmartCardPin);
      g_free(mSmartCardPin);
      mSmartCardPin = NULL;
   }
   mSmartCardReader.clear();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Broker::GetDesktopName --
 *
 *      Runs through the list of desktops and if the desktop with the given ID
 *      is in the list, returns it's name.
 *
 * Results:
 *      Util::string holding the desktop name, or empty.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Broker::GetDesktopName(Util::string desktopID) // IN
{
   for (std::vector<Desktop *>::iterator iter = mDesktops.begin();
        iter != mDesktops.end(); iter++) {
      if ((*iter)->GetID() == desktopID) {
         return (*iter)->GetName();
      }
   }

   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 *  Broker::CreateNewXmlConnection --
 *
 *     Creates a new instance of the object used for sending XML requests.
 *     This is a virtual method so derived classes can create a specialization
 *     of BrokerXML instead.
 *
 * Results:
 *     A new instance of BrokerXml* .
 *
 * Side effects:
 *     The caller is responsible for freeing the returned object.
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml *
Broker::CreateNewXmlConnection(const Util::string &hostname,      // IN
                               int port,                          // IN
                               bool secure)                       // IN
{
   return new BrokerXml(hostname, port, secure);
}


/*
 *-----------------------------------------------------------------------------
 *
 *  Broker::GetSupportBrokerUrl --
 *
 *     Returns the URL of the connection server we are connected to.
 *
 * Results:
 *     Util::string with the protocol, hostname, and port of the connection
 *     server.
 *
 * Side effects:
 *     None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Broker::GetSupportBrokerUrl()
   const
{
   return Util::Format("%s://%s:%d", GetSecure() ? "https" : "http",
                       GetHostname().c_str(), GetPort());
}


/*
 *-----------------------------------------------------------------------------
 *
 *  Broker::CancelRequests --
 *
 *     Cancels all outstanding requests.
 *
 * Results:
 *     The number of requests that were cancelled.
 *
 * Side effects:
 *     None
 *
 *-----------------------------------------------------------------------------
 */

int
Broker::CancelRequests()
{
   ASSERT(mXml);

   /*
    * If the tunnel's state was CONNECTING, but we tried to connect to a
    * desktop, when the tunnel finishes connecting, it will try to launch the
    * desktop.  In order to avoid this, set mDesktop to NULL so this race
    * condition won't happen. See bz 514312.
    */
   if (mDesktop && mTunnelState == TUNNEL_CONNECTING) {
      mDesktop = NULL;
   }

   return mXml->CancelRequests();
}


} // namespace cdk
