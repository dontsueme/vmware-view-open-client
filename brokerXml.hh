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
 * brokerXml.hh --
 *
 *    Handlers for the specific Broker XML API requests.
 */

#ifndef BROKER_XML_HH
#define BROKER_XML_HH

#if defined(_WIN32) && !defined(__MINGW32__)
#include <atlbase.h>
#endif

#include <boost/function.hpp>
#include <libxml/tree.h>
#include <vector>


#include "baseXml.hh"


namespace cdk {


class BrokerXml
   : public BaseXml
{
public:
   enum AuthType {
      AUTH_NONE = 0,
      AUTH_DISCLAIMER,
      AUTH_SECURID_PASSCODE,
      AUTH_SECURID_NEXTTOKENCODE,
      AUTH_SECURID_PINCHANGE,
      AUTH_SECURID_WAIT,
      AUTH_WINDOWS_PASSWORD,
      AUTH_WINDOWS_PASSWORD_EXPIRED,
      AUTH_CERT_AUTH
   };

   enum OfflineState {
      OFFLINE_CHECKED_IN,
      OFFLINE_CHECKED_OUT,
      OFFLINE_CHECKING_IN,
      OFFLINE_CHECKING_OUT,
      OFFLINE_BACKGROUND_CHECKING_IN,
      OFFLINE_ROLLING_BACK,
      OFFLINE_NONE
   };


   struct AuthInfo
   {
      Util::string name;
      Util::string title;
      Util::string text;
      std::vector<Param> params;

      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);

      AuthType GetAuthType() const;
      Util::string GetDisclaimer() const;
      Util::string GetUsername(bool *readOnly = NULL) const;
      std::vector<Util::string> GetDomains() const
         { return GetParam("domain"); }
      Util::string GetError() const;

   private:
      std::vector<Util::string> GetParam(const Util::string name,
                                         bool *readOnly = NULL) const;
   };

   struct Configuration
   {
      AuthInfo authInfo;

      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   struct AuthResult
   {
      AuthInfo authInfo;
      bool logoutOnCertRemoval;

      AuthResult() : logoutOnCertRemoval(false) { }
      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   struct Tunnel
   {
      // Tunnel Connect
      Util::string connectionId;
      int statusPort;
      Util::string server1;
      Util::string server2;
      int generation;
      // Direct Connect
      bool bypassTunnel;

      Tunnel();
      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   typedef std::pair<Util::string, Util::string> Preference;

   struct UserPreferences
   {
      std::vector<Preference> preferences;

      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   struct Desktop
   {
      Util::string id;
      Util::string name;
      Util::string type;
      Util::string state;
      bool offlineEnabled;
      bool endpointEnabled;
      OfflineState offlineState;
      bool checkedOutByOther;
      Util::string sessionId;
      bool resetAllowed;
      bool resetAllowedOnSession;
      bool inMaintenance;
      UserPreferences userPreferences;
      std::vector<Util::string> protocols;
      int defaultProtocol;
      bool expired;
      uint64 progressWorkDoneSoFar;
      uint64 progressTotalWork;
      bool checkedOutHereAndDisabled;

      Desktop();
      virtual ~Desktop() {}
      virtual bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   typedef std::vector<Desktop> DesktopList;

   struct EntitledDesktops
   {
      DesktopList desktops;

      virtual ~EntitledDesktops() {}
      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   struct Listener
   {
      Util::string address;
      int port;

      bool Parse(xmlNode *parentNode, Util::string &name,
                 Util::AbortSlot onAbort);
   };

   // Maps listener names to listener objects.
   typedef std::map<Util::string, Listener> ListenerMap;

   struct DesktopConnection
   {
      ListenerMap listeners;
      Util::string id;
      Util::string address;
      int port;
      Util::string channelTicket;
      Util::string protocol;
      Util::string username;
      Util::string password;
      Util::string domainName;
      Util::string token;
      bool enableUSB;
      bool enableMMR;

      DesktopConnection();
      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   typedef boost::function2<void, Result&, Configuration&> ConfigurationSlot;
   typedef boost::function1<void, Result&> LocaleSlot;
   typedef boost::function2<void, Result&, AuthResult&> AuthenticationSlot;
   typedef boost::function2<void, Result&, Tunnel&> TunnelConnectionSlot;
   typedef boost::function2<void, Result&, EntitledDesktops&> DesktopsSlot;
   typedef boost::function2<void, Result&, UserPreferences&> PreferencesSlot;
   typedef boost::function3<void, Result&, Util::string,
                                  UserPreferences&> DesktopPreferencesSlot;
   typedef boost::function2<void, Result&,
                                  DesktopConnection&> DesktopConnectionSlot;
   typedef boost::function1<void, Result&> LogoutSlot;
   typedef boost::function1<void, Result&> KillSessionSlot;
   typedef boost::function1<void, Result&> ResetDesktopSlot;
   typedef boost::function1<void, Result&> RollbackSlot;

   struct DoneSlots {
      ConfigurationSlot configuration;
      LocaleSlot locale;
      AuthenticationSlot authentication;
      TunnelConnectionSlot tunnelConnection;
      DesktopsSlot desktops;
      PreferencesSlot preferences;
      DesktopPreferencesSlot desktopPreferences;
      DesktopConnectionSlot desktopConnection;
      LogoutSlot logout;
      KillSessionSlot killSession;
      ResetDesktopSlot reset;
      RollbackSlot rollback;
   };

   struct RequestState
      : public BaseXml::RequestState
   {
      DoneSlots onDone;
   };

   BrokerXml(Util::string hostname, int port, bool secure,
             const Util::string &sslCAPath = "");
   virtual ~BrokerXml();

   void GetConfiguration(Util::AbortSlot onAbort, ConfigurationSlot onDone);

   void SetLocale(Util::string locale, Util::AbortSlot onAbort,
                  LocaleSlot onDone);

   void SubmitAuthentication(AuthInfo &auth, Util::AbortSlot onAbort,
                             AuthenticationSlot onDone);

   void PasswordAuthentication(Util::string username,
                               Util::string password,
                               Util::string domain,
                               Util::AbortSlot onAbort,
                               AuthenticationSlot onDone);

   void SecurIDUsernamePasscode(Util::string username, Util::string passcode,
                                Util::AbortSlot onAbort,
                                AuthenticationSlot onDone);
   void SecurIDNextTokencode(Util::string tokencode, Util::AbortSlot onAbort,
                             AuthenticationSlot onDone);
   void SecurIDPins(Util::string pin1, Util::string pin2,
                    Util::AbortSlot onAbort, AuthenticationSlot onDone);

   void AcceptDisclaimer(Util::AbortSlot onAbort, AuthenticationSlot onDone);

   void ChangePassword(Util::string oldPassword, Util::string newPassword,
                       Util::string confirm, Util::AbortSlot onAbort,
                       AuthenticationSlot onDone);

   void SubmitCertAuth(bool accept,
                       const char *pin,
                       const Util::string &reader,
                       Util::AbortSlot onAbort,
                       AuthenticationSlot onDone);

   void GetTunnelConnection(Util::AbortSlot onAbort,
                            TunnelConnectionSlot onDone);

   void GetDesktops(std::vector<Util::string> protocols,
                    Util::AbortSlot onAbort, DesktopsSlot onDone);

   void GetUserGlobalPreferences(Util::AbortSlot onAbort,
                                 PreferencesSlot onDone);

   void SetUserGlobalPreferences(UserPreferences &preferences,
                                 Util::AbortSlot onAbort,
                                 PreferencesSlot onDone);

   void SetUserDesktopPreferences(Util::string desktopId,
                                  UserPreferences &preferences,
                                  Util::AbortSlot onAbort,
                                  DesktopPreferencesSlot onDone);

   void GetDesktopConnection(Util::string desktopId,
                             Util::AbortSlot onAbort,
                             DesktopConnectionSlot onDone,
                             const Util::ClientInfoMap &info,
                             const Util::string &protocol);

   void Logout(Util::AbortSlot onAbort, LogoutSlot onDone);

   void KillSession(Util::string sessionId,
                    Util::AbortSlot onAbort,
                    KillSessionSlot onDone);

   void ResetDesktop(Util::string desktopId,
                     Util::AbortSlot onAbort,
                     ResetDesktopSlot onDone);

   void Rollback(Util::string sessionId,
                 Util::AbortSlot onAbort,
                 RollbackSlot onDone);

protected:
   virtual bool ResponseDispatch(xmlNode *operationNode,
                                 BaseXml::RequestState &state,
                                 Result &result);

};


} // namespace cdk


#endif // BROKER_XML_HH
