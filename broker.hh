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
 * broker.hh --
 *
 *    Broker control.
 */

#ifndef BROKER_HH
#define BROKER_HH


#include <vector>
#include <glib.h>


#include "brokerXml.hh"
#include "util.hh"
#include "restartMonitor.hh"
#include "tunnel.hh"


namespace cdk {


class Desktop;


class Broker
{
public:
   struct CertAuthInfo
   {
      X509 *cert;
      EVP_PKEY *key;
      char *pin;
      Util::string reader;

      CertAuthInfo() : cert(NULL), key(NULL), pin(NULL) { }
   };

   class Delegate
   {
   public:
      virtual ~Delegate() { }

      // Status notifications
      virtual void SetBusy(const Util::string &message) { }
      virtual void SetReady() { }
      virtual void SetLogoutOnCertRemoval(bool enabled) { }

      virtual void Disconnect() { }

      // State change notifications
      virtual void RequestBroker() { }
      virtual void RequestDisclaimer(const Util::string &disclaimer) { }
      virtual void RequestPasscode(const Util::string &username) { }
      virtual void RequestNextTokencode(const Util::string &username) { }
      virtual void RequestPinChange(const Util::string &pin,
                                    const Util::string &message,
                                    bool userSelectable) { }
      virtual void RequestPassword(const Util::string &username,
                                   bool readOnly,
                                   const std::vector<Util::string> &domains,
                                   const Util::string &domain) { }
      virtual void RequestPasswordChange(const Util::string &username,
                                         const Util::string &domain) { }
      virtual void RequestDesktop() { }
      virtual void RequestTransition(const Util::string &message) { }
      virtual void RequestLaunchDesktop(Desktop *desktop) { }

      virtual CertAuthInfo GetCertAuthInfo(SSL *ssl)
         { CertAuthInfo info; return info; }

      /*
       * We can't totally handle this here, as App may have an RDesktop
       * session open that it needs to ignore an exit from while it
       * displays this message.
       */
      virtual void TunnelDisconnected(Util::string disconnectReason) { }

      virtual void UpdateDesktops() { }
   };

   Broker();
   virtual ~Broker();

   virtual void SetDelegate(Delegate *delegate) { mDelegate = delegate; }
   Delegate *GetDelegate() const { return mDelegate; }

   virtual void SetSupportedProtocols(std::vector<Util::string> protocols)
      { mSupportedProtocols = protocols; }

   virtual void Reset();
   // Broker XML API
   virtual void Initialize(const Util::string &hostname,
                           int port, bool secure,
                           const Util::string &defaultUser,
                           const Util::string &defaultDomain);
   virtual void AcceptDisclaimer();
   virtual void SubmitPasscode(const Util::string &username,
                               const Util::string &passcode);
   virtual void SubmitNextTokencode(const Util::string &tokencode);
   virtual void SubmitPins(const Util::string &pin1,
                           const Util::string &pin2);
   virtual void SubmitPassword(const Util::string &username,
                               const Util::string &password,
                               const Util::string &domain);
   virtual void ChangePassword(const Util::string &oldPassword,
                               const Util::string &newPassword,
                               const Util::string &confirm);
   virtual void LoadDesktops() { mDelegate->RequestDesktop(); }
   virtual void ConnectDesktop(Desktop *desktop);
   virtual void ReconnectDesktop();
   virtual void ResetDesktop(Desktop *desktop, bool andQuit = false);
   virtual void KillSession(Desktop *desktop);
   virtual void RollbackDesktop(Desktop *desktop);
   virtual void Logout();

   Util::string GetHostname() const
      { ASSERT(mXml); return mXml->GetHostname(); }
   int GetPort() const { ASSERT(mXml); return mXml->GetPort(); }
   bool GetSecure() const { ASSERT(mXml); return mXml->GetSecure(); }

   virtual int CancelRequests() { ASSERT(mXml); return mXml->CancelRequests(); }
   void SetCookieFile(const Util::string &cookieFile)
      { mCookieFile = cookieFile; }

   Desktop *GetDesktop() const { return mDesktop; }
   void GetDesktops(bool refresh = false);

   bool GetIsUsingTunnel() const { return mTunnel && !mTunnel->GetIsBypassed(); }

   std::vector<Desktop*> mDesktops;

protected:
   virtual void SetLocale();
   virtual bool GetTunnelReady();
   virtual bool GetDesktopReady();
   virtual void MaybeLaunchDesktop();

   // brokerXml onDone handlers
   virtual void OnLocaleSet() { }
   virtual void GetConfiguration();
   virtual void OnAuthResult(BrokerXml::Result &result,
                             BrokerXml::AuthResult &auth);
   virtual void OnConfigurationDone(BrokerXml::Result &result,
                                    BrokerXml::Configuration &config);
   virtual void OnAuthInfo(BrokerXml::Result &result,
                           BrokerXml::AuthInfo &authInfo,
                           bool treatOkAsPartial = false);
   virtual void OnAuthInfoPinChange(std::vector<BrokerXml::Param> &params);
   virtual void InitTunnel();
   virtual void ResetTunnel();
   virtual void OnGetTunnelConnectionDone(BrokerXml::Tunnel &tunnel);
   virtual void OnTunnelConnected();
   virtual void OnTunnelDisconnect(int status, Util::string disconnectReason);
   virtual void OnGetDesktopsSet(BrokerXml::EntitledDesktops &desktops);
   virtual void OnGetDesktopsRefresh(BrokerXml::EntitledDesktops &desktops);
   virtual void OnLogoutResult();
   virtual void OnDesktopOpDone(Desktop *desktop, bool disconnect);

   virtual void OnInitialRPCAbort(bool cancelled, Util::exception err);

   virtual int OnCertificateRequested(SSL *ssl, X509 **x509,
                                      EVP_PKEY **privKey);
   virtual void OnAbort(bool cancelled, Util::exception err);

private:
   enum CertState {
      // The server has not requested a certificate from us.
      CERT_NOT_REQUESTED,
      // The server has requested a certificate (but we have not sent one).
      CERT_REQUESTED,
      // THe next time the server requests a certificate, return one.
      CERT_SHOULD_RESPOND,
      // We have sent a certificate, if one was available.
      CERT_DID_RESPOND
   };

   static gboolean RefreshDesktopsTimeout(gpointer data);

   void ClearSmartCardPinAndReader();

   Delegate *mDelegate;
   BrokerXml *mXml;
   Tunnel *mTunnel;
   Desktop *mDesktop;
   Util::string mUsername;
   Util::string mDomain;
   boost::signals::connection mTunnelDisconnectCnx;
   RestartMonitor mTunnelMonitor;
   CertState mCertState;
   unsigned int mRefreshTimeoutSourceID;
   bool mGettingDesktops;
   char *mSmartCardPin;
   Util::string mSmartCardReader;
   std::vector<Util::string> mSupportedProtocols;
   Util::string mCookieFile;
   unsigned int mAuthRequestId;
};


} // namespace cdk


#endif // BROKER_HH
