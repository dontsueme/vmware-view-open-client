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
 * brokerXml.cc --
 *
 *    Handlers for the specific Broker XML API requests.
 */


#include <gmodule.h>
#include <libxml/parser.h>
#include <list>
#include <set>


#if defined(_WIN32) && !defined(__MINGW32__)
#define _(String) (String)
#endif


#include "brokerXml.hh"


#define BROKER_NODE_NAME "broker"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::BrokerXml --
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

BrokerXml::BrokerXml(Util::string hostname, // IN
                     int port,              // IN
                     bool secure,           // IN
                     const Util::string &sslCAPath)// IN
   : BaseXml(BROKER_NODE_NAME, hostname, port, secure, sslCAPath)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::~BrokerXml --
 *
 *      Destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::~BrokerXml()
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::ResponseDispatch --
 *
 *      Dispatcher for handling responses for the request types introduced
 *      by this class.
 *
 * Results:
 *      'true' if the response was handled. 'false' otherwise.
 *
 * Side effects:
 *      Response signals may be fired.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::ResponseDispatch(xmlNode *operationNode,           // IN
                            BaseXml::RequestState &baseState, // IN
                            Result &result)                     // IN
{
   RequestState &state = static_cast<RequestState &>(baseState);

   if (result.result == "notexecuted") {
      Log("Not executed: %s; skipping callbacks.\n", baseState.responseOp.c_str());
      return true;
   }

   if (state.responseOp == "configuration") {
      Configuration config;
      if (config.Parse(operationNode, state.onAbort)) {
         state.onDone.configuration(result, config);
      }
   } else if (state.responseOp == "set-locale") {
      state.onDone.locale(result);
   } else if (state.responseOp == "submit-authentication") {
      AuthResult authResult;
      if (authResult.Parse(operationNode, state.onAbort)) {
         state.onDone.authentication(result, authResult);
      }
   } else if (state.responseOp == "tunnel-connection") {
      Tunnel tunnel;
      if (tunnel.Parse(operationNode, state.onAbort)) {
         state.onDone.tunnelConnection(result, tunnel);
      }
   } else if (state.responseOp == "desktops") {
      EntitledDesktops desktops;
      if (desktops.Parse(operationNode, state.onAbort)) {
         state.onDone.desktops(result, desktops);
      }
   } else if (state.responseOp == "user-global-preferences" ||
              state.responseOp == "set-user-global-preferences") {
      UserPreferences prefs;
      if (prefs.Parse(operationNode, state.onAbort)) {
         state.onDone.preferences(result, prefs);
      }
   } else if (state.responseOp == "set-user-desktop-preferences") {
      UserPreferences prefs;
      if (prefs.Parse(operationNode, state.onAbort)) {
         Util::string desktopId = GetChildContent(operationNode, "desktop-id");
         state.onDone.desktopPreferences(result, desktopId, prefs);
      }
   } else if (state.responseOp == "desktop-connection") {
      DesktopConnection conn;
      if (conn.Parse(operationNode, state.onAbort)) {
         state.onDone.desktopConnection(result, conn);
      }
   } else if (state.responseOp == "logout") {
      state.onDone.logout(result);
   } else if (state.responseOp == "kill-session") {
      state.onDone.killSession(result);
   } else if (state.responseOp == "reset-desktop") {
      state.onDone.reset(result);
   } else if (state.responseOp == "rollback-checkout-desktop") {
      state.onDone.rollback(result);
   } else {
      return false;
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::Parse --
 *
 *       Parse an <authentication> node and its <param> children values.
 *
 * Results:
 *       true if parsed successfully, false otherwise and the onAbort handler
 *       was invoked.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::AuthInfo::Parse(xmlNode *parentNode,     // IN
                           Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   xmlNode *authNode = GetChild(parentNode, "authentication");
   if (!authNode) {
      onAbort(false, Util::exception(_("Invalid response from broker"), "",
                                     _("Invalid \"authentication\" in XML.")));
      return false;
   }

   xmlNode *screenNode = GetChild(authNode, "screen");
   if (!screenNode) {
      onAbort(false, Util::exception(_("Invalid response from broker"), "",
                                     _("Invalid \"screen\" in XML.")));
      return false;
   }

   name = GetChildContent(screenNode, "name");
   if (GetAuthType() == AUTH_NONE) {
      Log("Broker XML AuthInfo name unknown: \"%s\"\n", name.c_str());
      onAbort(false, Util::exception(_("Invalid response from broker"), "",
                                     _("Invalid \"name\" in XML.")));
      return false;
   }

   title = GetChildContent(screenNode, "title");
   text = GetChildContent(screenNode, "text");

   xmlNode *paramsNode = GetChild(screenNode, "params");
   if (paramsNode) {
      for (xmlNode *paramNode = paramsNode->children; paramNode;
           paramNode = paramNode->next) {
         if (Str_Strcasecmp((const char *) paramNode->name, "param") == 0) {
            Param param;
            if (!param.Parse(paramNode, onAbort)) {
               return false;
            }
            params.push_back(param);
         }
      }
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetParam --
 *
 *       Accessor for the values associated with the named param, and optional
 *       read-only specifier.
 *
 * Results:
 *       List of param values. If readOnly is non-NULL, it is set to true if
 *       the param has been specified read-only, false otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
BrokerXml::AuthInfo::GetParam(const Util::string name, // IN
                              bool *readOnly)          // OUT/OPT
   const
{
   for (std::vector<Param>::const_iterator i = params.begin();
        i != params.end(); i++) {
      if (i->name == name) {
         if (readOnly) {
            *readOnly = i->readOnly;
         }
         return i->values;
      }
   }
   return std::vector<Util::string>();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetAuthType --
 *
 *      Returns the current type of authentication: disclaimer, SecurID,
 *      or password.
 *
 * Results:
 *      The AuthType.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::AuthType
BrokerXml::AuthInfo::GetAuthType()
   const
{
   if (name == "disclaimer") {
      return AUTH_DISCLAIMER;
   } else if (name == "securid-passcode") {
      return AUTH_SECURID_PASSCODE;
   } else if (name == "securid-nexttokencode") {
      return AUTH_SECURID_NEXTTOKENCODE;
   } else if (name == "securid-pinchange") {
      return AUTH_SECURID_PINCHANGE;
   } else if (name == "securid-wait") {
      return AUTH_SECURID_WAIT;
   } else if (name == "windows-password") {
      return AUTH_WINDOWS_PASSWORD;
   } else if (name == "windows-password-expired") {
      return AUTH_WINDOWS_PASSWORD_EXPIRED;
   } else if (name == "cert-auth") {
      return AUTH_CERT_AUTH;
   } else {
      return AUTH_NONE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetDisclaimer --
 *
 *       Accessor for the "text" param value in a "disclaimer" AuthInfo.
 *
 * Results:
 *       The value of the "text" param if it has exactly one value;
 *       empty string otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::AuthInfo::GetDisclaimer()
   const
{
   std::vector<Util::string> values = GetParam("text");
   return values.size() == 1 ? values[0] : "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetUsername --
 *
 *       Accessor for the "username" param value and its read-only status.
 *
 * Results:
 *       The value of the "username" param if it has exactly one value;
 *       empty string otherwise. readOnly, if given, is set to the param's
 *       read-only state.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::AuthInfo::GetUsername(bool *readOnly) // OUT/OPT
   const
{
   std::vector<Util::string> values = GetParam("username", readOnly);
   if (values.size() == 1) {
      return values[0];
   } else {
      return "";
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthInfo::GetError --
 *
 *       Accessor for the "error" param values, concatenated with a newline.
 *
 * Results:
 *       String of concatenated error values.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BrokerXml::AuthInfo::GetError()
   const
{
   std::vector<Util::string> values = GetParam("error");
   Util::string err;
   for (std::vector<Util::string>::const_iterator i = values.begin();
        i != values.end(); i++) {
      if (!err.empty()) {
         err += "\n";
      }
      err += *i;
   }
   return err;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Configuration::Parse --
 *
 *       Parse a <configuration> parentNode's children, currently consisting of
 *       optional authentication information.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Configuration::Parse(xmlNode *parentNode,     // IN
                                Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   // Authentication info seems optional
   if (GetChild(parentNode, "authentication")) {
      return authInfo.Parse(parentNode, onAbort);
   } else {
      return true;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AuthResult::Parse --
 *
 *       Parse a <submit-authentication> parentNode's children, currently
 *       consisting of optional authentication information.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::AuthResult::Parse(xmlNode *parentNode,     // IN
                             Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   logoutOnCertRemoval = GetChildContentBool(parentNode,
                                             "logout-on-cert-removal-enabled");
   // Authentication info seems optional
   if (GetChild(parentNode, "authentication")) {
      return authInfo.Parse(parentNode, onAbort);
   } else {
      return true;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Tunnel::Parse --
 *
 *       Parse a <tunnel-connection> parentNode's children.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Tunnel::Parse(xmlNode *parentNode,     // IN
                         Util::AbortSlot onAbort) // IN
{
   connectionId = GetChildContent(parentNode, "connection-id");
   statusPort = GetChildContentInt(parentNode, "status-port");
   server1 = GetChildContent(parentNode, "server1");
   server2 = GetChildContent(parentNode, "server2");
   server1 = GetChildContent(parentNode, "server1");
   generation = GetChildContentInt(parentNode, "generation");
   bypassTunnel = GetChildContentBool(parentNode, "bypass-tunnel");
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::UserPreferences::Parse --
 *
 *       Parse a <user-preferences> subelement of the parentNode, including
 *       loading individual preference key/value pairs.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::UserPreferences::Parse(xmlNode *parentNode,     // IN
                                  Util::AbortSlot onAbort) // IN
{
   xmlNode *userPrefsNode = GetChild(parentNode, "user-preferences");
   if (userPrefsNode) {
      for (xmlNode *prefNode = userPrefsNode->children; prefNode;
           prefNode = prefNode->next) {
         if (Str_Strcasecmp((const char*) prefNode->name, "preference") == 0) {
            Preference pref;
            pref.first = (const char*) xmlGetProp(prefNode, (const xmlChar*) "name");
            pref.second = GetContent(prefNode);
            preferences.push_back(pref);
         }
      }
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Desktop::Parse --
 *
 *       Parse a <desktop> parentNode's content.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Desktop::Parse(xmlNode *parentNode,     // IN
                          Util::AbortSlot onAbort) // IN
{
   id = GetChildContent(parentNode, "id");
   name = GetChildContent(parentNode, "name");
   type = GetChildContent(parentNode, "type");
   state = GetChildContent(parentNode, "state");

   offlineEnabled = GetChildContentBool(parentNode, "offline-enabled");
   endpointEnabled = GetChildContentBool(parentNode, "endpoint-enabled");
   Util::string offline = GetChildContent(parentNode, "offline-state");
   if (offline == "checked in") {
      offlineState = OFFLINE_CHECKED_IN;
   } else if (offline == "checked out") {
      offlineState = OFFLINE_CHECKED_OUT;
   } else if (offline == "checking in") {
      offlineState = OFFLINE_CHECKING_IN;
   } else if (offline == "checking out") {
      offlineState = OFFLINE_CHECKING_OUT;
   } else if (offline == "background checking in") {
      offlineState = OFFLINE_BACKGROUND_CHECKING_IN;
   } else if (offline == "rolling back") {
      offlineState = OFFLINE_ROLLING_BACK;
   } else if (offline.empty()) {
      offlineState = OFFLINE_CHECKED_IN;
   } else {
      Log("Unknown local state \"%s\" in XML.\n", offline.c_str());
      offlineState = OFFLINE_NONE;
   }

   checkedOutByOther = GetChildContentBool(parentNode, "checked-out-by-other");
   sessionId = GetChildContent(parentNode, "session-id");
   resetAllowed = GetChildContentBool(parentNode, "reset-allowed");
   resetAllowedOnSession = GetChildContentBool(parentNode,
                                               "reset-allowed-on-session");
   inMaintenance = GetChildContentBool(parentNode, "in-maintenance-mode");
   expired = GetChildContentBool(parentNode, "expired");
   checkedOutHereAndDisabled =
      GetChildContentBool(parentNode, "checked-out-here-and-disabled");

#ifdef VIEW_CVP
   if (offlineState == OFFLINE_CHECKING_OUT) {
      progressWorkDoneSoFar =
         GetChildContentUInt64(parentNode, "progress-work-done-so-far");
      progressTotalWork =
         GetChildContentUInt64(parentNode, "progress-total-work");
   }
#endif // VIEW_CVP

   xmlNode *protocolNode = GetChild(parentNode, "protocols");
   if (protocolNode) {
      std::set<Util::string> protos;
      Util::string defaultProto;
      for (xmlNode *protoNode = protocolNode->children; protoNode;
           protoNode = protoNode->next) {
         if (Str_Strcasecmp((const char *)protoNode->name, "protocol") == 0) {
            Util::string proto = GetChildContent(protoNode, "name");
            if (!proto.empty()) {
               if (GetChildContentBool(protoNode, "is-default")) {
                  defaultProto = proto;
               }
               protos.insert(proto);
            }
         }
      }
      for (std::set<Util::string>::iterator i = protos.begin();
           i != protos.end(); i++) {
         protocols.push_back(*i);
         if (*i == defaultProto) {
            defaultProtocol = protocols.size() - 1;
         }
      }
   }
   if (protocols.empty()) {
      protocols.push_back("RDP");
      defaultProtocol = 0;
   }
   return userPreferences.Parse(parentNode, onAbort);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::DesktopConnection::Parse --
 *
 *       Parse a <desktop-connection> parentNode's content.
 *
 * Results:
 *       True on success, false on parser error.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::DesktopConnection::Parse(xmlNode *parentNode,     // IN
                                    Util::AbortSlot onAbort) // IN
{
   id = GetChildContent(parentNode, "id");
   address = GetChildContent(parentNode, "address");
   /*
    * The broker always returns "localhost" for tunneled connections,
    * but that may resolve to an IPv6 address, which our tunnel proxy
    * is not listening on.  This results in the RDP client's
    * connection timing out.  See bug #391088.
    */
   if (address == "localhost") {
      address = "127.0.0.1";
   }
   port = GetChildContentInt(parentNode, "port");
   channelTicket = GetChildContent(parentNode, "framework-channel-ticket");
   protocol = GetChildContent(parentNode, "protocol");
   username = GetChildContent(parentNode, "user-name");
   password = GetChildContent(parentNode, "password");
   domainName = GetChildContent(parentNode, "domain-name");
   enableUSB = GetChildContentBool(parentNode, "enable-usb");
   enableMMR = GetChildContentBool(parentNode, "enable-mmr");

   // Parse additional listeners, if available.
   xmlNode *listenersNode = GetChild(parentNode, "additional-listeners");
   if (listenersNode) {
      // Iterate over the listeners and add them to the listeners map.
      for (xmlNode *listenerNode = listenersNode->children; listenerNode;
           listenerNode = listenerNode->next) {
         if (Str_Strcasecmp((const char*) listenerNode->name, "additional-listener") == 0) {
            Util::string listenerName;
            Listener listener;
            if (!listener.Parse(listenerNode, listenerName, onAbort)) {
               return false;
            }
            listeners.insert(std::make_pair(listenerName, listener));
         }
      }
   }

   xmlNode *settingsNode = GetChild(parentNode, "protocol-settings");
   if (settingsNode) {
      token = GetChildContent(settingsNode, "token");
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Listener::Parse --
 *
 *       Parse a <additional-listener> parentNode's content.
 *
 * Results:
 *       True on success, false if the XML is invalid and calls onAbort().
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::Listener::Parse(xmlNode *parentNode,     // IN
                           Util::string &name,      // OUT
                           Util::AbortSlot onAbort) // IN
{
   // Name is an attribute
   name = (const char *)xmlGetProp(parentNode, (const xmlChar *)"name");

   // this will be hostname:port
   Util::string hostAndPort = GetContent(parentNode);

   Util::string::size_type colonIdx = hostAndPort.find(":");
   if (colonIdx == std::string::npos) {
      onAbort(false, Util::exception(_("Invalid response from broker"), "",
                                     _("Listener with invalid host name.")));
      return false;
   }

   address = hostAndPort.substr(0, colonIdx);
   port = atoi(hostAndPort.substr(colonIdx + 1).c_str());
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::EntitledDesktops::Parse --
 *
 *       Parse a <desktops> parentNode's <desktop> children, and collect all the
 *       children.
 *
 * Results:
 *       Always true.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerXml::EntitledDesktops::Parse(xmlNode *parentNode,     // IN
                                   Util::AbortSlot onAbort) // IN
{
   for (xmlNode *desktopNode = parentNode->children; desktopNode;
        desktopNode = desktopNode->next) {
      if (Str_Strcasecmp((const char*) desktopNode->name, "desktop") == 0) {
         Desktop desktop;
         if (!desktop.Parse(desktopNode, onAbort)) {
            return false;
         }
         desktops.push_back(desktop);
      }
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetConfiguration --
 *
 *      Send a "get-configuration" request to the broker server.
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
BrokerXml::GetConfiguration(Util::AbortSlot onAbort,  // IN
                            ConfigurationSlot onDone) // IN
{
   RequestState *req = new RequestState();
   req->requestOp = "get-configuration";
   req->responseOp = "configuration";
   req->onAbort = onAbort;
   req->onDone.configuration = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SetLocale --
 *
 *      Send a "set-locale" request to the broker server.
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
BrokerXml::SetLocale(Util::string locale,     // IN
                     Util::AbortSlot onAbort, // IN
                     LocaleSlot onDone)       // IN
{
   RequestState *req = new RequestState();
   req->requestOp = "set-locale";
   req->responseOp = "set-locale";
   req->args = "<locale>" + Encode(locale) + "</locale>";
   req->onAbort = onAbort;
   req->onDone.locale = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SubmitAuthentication --
 *
 *      Send a "do-submit-authentication" request to the broker server.
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
BrokerXml::SubmitAuthentication(AuthInfo &auth,            // IN
                                Util::AbortSlot onAbort,   // IN
                                AuthenticationSlot onDone) // IN
{
   Util::string arg = "<screen>";

   if (!auth.name.empty()) {
      arg += "<name>" + Encode(auth.name) + "</name>";
   }
   if (!auth.title.empty()) {
      arg += "<title>" + Encode(auth.title) + "</title>";
   }
   if (!auth.title.empty()) {
      arg += "<text>" + Encode(auth.text) + "</text>";
   }

   arg += "<params>";
   for (std::vector<Param>::iterator i = auth.params.begin();
        i != auth.params.end(); i++) {
      arg += "<param>";
      arg += "<name>" + Encode((*i).name) + "</name>";

      arg += "<values>";
      for (std::vector<Util::string>::iterator j = (*i).values.begin();
           j != (*i).values.end(); j++) {
         arg += "<value>" + Encode((*j)) + "</value>";
      }
      arg += "</values>";

      if ((*i).readOnly) {
         arg += "<readonly/>";
      }
      arg += "</param>";
   }
   arg += "</params>";
   arg += "</screen>";

   RequestState *req = new RequestState();
   req->requestOp = "do-submit-authentication";
   req->responseOp = "submit-authentication";
   req->args = arg;
   req->onAbort = onAbort;
   req->onDone.authentication = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::PasswordAuthentication --
 *
 *      Helper for SubmitAuthentication to send a "windows-password" auth info,
 *      containing username, password, and domain params.
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
BrokerXml::PasswordAuthentication(Util::string username,     // IN
                                  Util::string password,     // IN
                                  Util::string domain,       // IN
                                  Util::AbortSlot onAbort,   // IN
                                  AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "windows-password";

   Param usernameParam;
   usernameParam.name = "username";
   usernameParam.values.push_back(username);
   authInfo.params.push_back(usernameParam);

   Param passwdParam;
   passwdParam.name = "password";
   passwdParam.values.push_back(password);
   authInfo.params.push_back(passwdParam);

   Param domainParam;
   domainParam.name = "domain";
   domainParam.values.push_back(domain);
   authInfo.params.push_back(domainParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SecurIDUsernamePasscode --
 *
 *      Helper for SubmitAuthentication to send a "securid-passcode" auth info,
 *      containing username and passcode params.
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
BrokerXml::SecurIDUsernamePasscode(Util::string username,     // IN
                                   Util::string passcode,     // IN
                                   Util::AbortSlot onAbort,   // IN
                                   AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "securid-passcode";

   Param usernameParam;
   usernameParam.name = "username";
   usernameParam.values.push_back(username);
   authInfo.params.push_back(usernameParam);

   Param passcodeParam;
   passcodeParam.name = "passcode";
   passcodeParam.values.push_back(passcode);
   authInfo.params.push_back(passcodeParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SecurIDNextTokencode --
 *
 *      Helper for SubmitAuthentication to send a "securid-nexttokencode"
 *      auth info, containing a tokencode param.
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
BrokerXml::SecurIDNextTokencode(Util::string tokencode,    // IN
                                Util::AbortSlot onAbort,   // IN
                                AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "securid-nexttokencode";

   Param tokencodeParam;
   tokencodeParam.name = "tokencode";
   tokencodeParam.values.push_back(tokencode);
   authInfo.params.push_back(tokencodeParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SecurIDNextTokencode --
 *
 *      Helper for SubmitAuthentication to send a "securid-pinchange"
 *      auth info, containing two (hopefully matching) PIN values.
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
BrokerXml::SecurIDPins(Util::string pin1,         // IN
                       Util::string pin2,         // IN
                       Util::AbortSlot onAbort,   // IN
                       AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "securid-pinchange";

   Param pinParam1;
   pinParam1.name = "pin1";
   pinParam1.values.push_back(pin1);
   authInfo.params.push_back(pinParam1);

   Param pinParam2;
   pinParam2.name = "pin2";
   pinParam2.values.push_back(pin2);
   authInfo.params.push_back(pinParam2);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::AcceptDisclaimer --
 *
 *      Helper for SubmitAuthentication to send a "disclaimer"
 *      AuthInfo with param "accept" = "true".
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
BrokerXml::AcceptDisclaimer(Util::AbortSlot onAbort,   // IN
                            AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "disclaimer";

   Param acceptParam;
   acceptParam.name = "accept";
   acceptParam.values.push_back("true");
   authInfo.params.push_back(acceptParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::ChangePassword --
 *
 *      Helper for SubmitAuthentication to send a "windows-password-expired"
 *      AuthInfo with params for old, new, and confirmed-new passwords.
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
BrokerXml::ChangePassword(Util::string oldPassword,  // IN
                          Util::string newPassword,  // IN
                          Util::string confirm,      // IN
                          Util::AbortSlot onAbort,   // IN
                          AuthenticationSlot onDone) // IN
{
   AuthInfo authInfo;
   authInfo.name = "windows-password-expired";

   Param oldParam;
   oldParam.name = "oldPassword";
   oldParam.values.push_back(oldPassword);
   authInfo.params.push_back(oldParam);

   Param newParam;
   newParam.name = "newPassword1";
   newParam.values.push_back(newPassword);
   authInfo.params.push_back(newParam);

   Param confirmParam;
   confirmParam.name = "newPassword2";
   confirmParam.values.push_back(confirm);
   authInfo.params.push_back(confirmParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SubmitCertAuth --
 *
 *      Respond to a cert-auth authentication message.  Submits
 *      cert-auth either accepting or rejecting the user the server
 *      thinks we are.
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
BrokerXml::SubmitCertAuth(bool accept,                // IN
                          const char *pin,            // IN
                          const Util::string &reader, // IN
                          Util::AbortSlot onAbort,    // IN
                          AuthenticationSlot onDone)  // IN
{
   AuthInfo authInfo;
   authInfo.name = "cert-auth";

   Param acceptParam;
   acceptParam.name = "accept";
   acceptParam.values.push_back(accept ? "true" : "false");
   authInfo.params.push_back(acceptParam);

   Param pinParam;
   pinParam.name = "smartCardPIN";
   pinParam.values.push_back(pin ? Util::string(pin) : "");
   authInfo.params.push_back(pinParam);

   Param readerParam;
   readerParam.name = "smartCardReader";
   readerParam.values.push_back(reader);
   authInfo.params.push_back(readerParam);

   SubmitAuthentication(authInfo, onAbort, onDone);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetTunnelConnection --
 *
 *      Send a "get-tunnel-connection" request to the broker server.
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
BrokerXml::GetTunnelConnection(Util::AbortSlot onAbort,     // IN
                               TunnelConnectionSlot onDone) // IN
{
   RequestState *req = new RequestState();
   req->requestOp = "get-tunnel-connection";
   req->responseOp = "tunnel-connection";
   req->onAbort = onAbort;
   req->onDone.tunnelConnection = onDone;
   SendRequest(req);
}



/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetDesktops --
 *
 *      Send a "get-desktops" request to the broker server.
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
BrokerXml::GetDesktops(std::vector<Util::string> protocols, // IN
                       Util::AbortSlot onAbort,             // IN
                       DesktopsSlot onDone)                 // IN
{
   RequestState *req = new RequestState();
   req->requestOp = "get-desktops";
   req->responseOp = "desktops";
   req->onAbort = onAbort;
   req->onDone.desktops = onDone;
   if (!protocols.empty()) {
      req->args = "<supported-protocols>";
      for (std::vector<Util::string>::iterator i = protocols.begin();
           i != protocols.end(); ++i) {
         req->args += "<protocol><name>" + Encode(*i) + "</name></protocol>";
      }
      req->args += "</supported-protocols>";
   }
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetUserGlobalPreferences --
 *
 *      Send a "get-user-global-preferences" request to the broker server.
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
BrokerXml::GetUserGlobalPreferences(Util::AbortSlot onAbort, // IN
                                    PreferencesSlot onDone)  // IN
{
   RequestState *req = new RequestState();
   req->requestOp = "get-user-global-preferences";
   req->responseOp = "user-global-preferences";
   req->onAbort = onAbort;
   req->onDone.preferences = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SetUserGlobalPreferences --
 *
 *      Send a "set-user-global-preferences" request to the broker server.
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
BrokerXml::SetUserGlobalPreferences(UserPreferences &prefs,  // IN
                                    Util::AbortSlot onAbort, // IN
                                    PreferencesSlot onDone)  // IN
{
   Util::string arg = "<user-preferences>";
   for (std::vector<Preference>::iterator i = prefs.preferences.begin();
        i != prefs.preferences.end(); i++) {
      arg += Util::Format("<preferences name=\"%s\">%s</preference>",
                          Encode((*i).first).c_str(),
                          Encode((*i).second).c_str());
   }
   arg += "</user-preferences>";

   RequestState *req = new RequestState();
   req->requestOp = "set-user-global-preferences";
   req->responseOp = "set-user-global-preferences";
   req->args = arg;
   req->onAbort = onAbort;
   req->onDone.preferences = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::SetUserDesktopPreferences --
 *
 *      Send a "set-user-desktop-preferences" request to the broker server.
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
BrokerXml::SetUserDesktopPreferences(Util::string desktopId,        // IN
                                     UserPreferences &prefs,        // IN
                                     Util::AbortSlot onAbort,       // IN
                                     DesktopPreferencesSlot onDone) // IN
{
   ASSERT(!desktopId.empty());

   Util::string arg;
   arg = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";
   arg += "<user-preferences>";
   for (std::vector<Preference>::iterator i = prefs.preferences.begin();
        i != prefs.preferences.end(); i++) {
      arg += Util::Format("<preference name=\"%s\">%s</preference>",
                          Encode((*i).first).c_str(),
                          Encode((*i).second).c_str());
   }
   arg += "</user-preferences>";

   RequestState *req = new RequestState();
   req->requestOp = "set-user-desktop-preferences";
   req->responseOp = "set-user-desktop-preferences";
   req->args = arg;
   req->onAbort = onAbort;
   req->onDone.desktopPreferences = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::GetDesktopConnection --
 *
 *      Send a "get-desktop-connection" request to the broker server.
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
BrokerXml::GetDesktopConnection(Util::string desktopId,          // IN
                                Util::AbortSlot onAbort,         // IN
                                DesktopConnectionSlot onDone,    // IN
                                const Util::ClientInfoMap &info, // IN
                                const Util::string &protocol)    // IN
{
   ASSERT(!desktopId.empty());

   RequestState *req = new RequestState();
   req->requestOp = "get-desktop-connection";
   req->responseOp = "desktop-connection";
   req->args = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";

   if (!protocol.empty()) {
      req->args += "<protocol><name>" + Encode(protocol) +
                   "</name></protocol>";
   }

   if (!info.empty()) {
      req->args += "<environment-information>";
      for (Util::ClientInfoMap::const_iterator iter = info.begin();
           iter != info.end(); ++iter) {
         req->args += Util::Format("<info name=\"%s\">%s</info>",
                                  Encode(iter->first).c_str(),
                                  Encode(iter->second).c_str());
      }
      req->args += "</environment-information>";
   }

   req->onAbort = onAbort;
   req->onDone.desktopConnection = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Logout --
 *
 *      Send a "do-logout" request to the broker server.
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
BrokerXml::Logout(Util::AbortSlot onAbort, // IN
                  LogoutSlot onDone)       // IN
{
   RequestState *req = new RequestState();
   req->requestOp = "do-logout";
   req->responseOp = "logout";
   req->onAbort = onAbort;
   req->onDone.logout = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::KillSession --
 *
 *      Send a "kill-session" request (log out of remote desktop) to
 *      the broker server.
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
BrokerXml::KillSession(Util::string sessionId,  // IN
                       Util::AbortSlot onAbort, // IN
                       KillSessionSlot onDone)  // IN
{
   ASSERT(!sessionId.empty());

   RequestState *req = new RequestState();
   req->requestOp = "kill-session";
   req->responseOp = "kill-session";
   req->args = "<session-id>" + Encode(sessionId) + "</session-id>";
   req->onAbort = onAbort;
   req->onDone.killSession = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::ResetDesktop --
 *
 *      Send a "reset-desktop" request (restart?) to the broker server.
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
BrokerXml::ResetDesktop(Util::string desktopId,  // IN
                        Util::AbortSlot onAbort, // IN
                        ResetDesktopSlot onDone) // IN
{
   ASSERT(!desktopId.empty());

   RequestState *req = new RequestState();
   req->requestOp = "reset-desktop";
   req->responseOp = "reset-desktop";
   req->args = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";
   req->onAbort = onAbort;
   req->onDone.reset = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Rollback --
 *
 *      Send a "rollback-checkout-desktop" request to the broker server.
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
BrokerXml::Rollback(Util::string desktopId,  // IN
                    Util::AbortSlot onAbort, // IN
                    RollbackSlot onDone)     // IN
{
   ASSERT(!desktopId.empty());

   RequestState *req = new RequestState();
   req->requestOp = "rollback-checkout-desktop";
   req->responseOp = "rollback-checkout-desktop";
   req->args = "<desktop-id>" + Encode(desktopId) + "</desktop-id>";
   req->onAbort = onAbort;
   req->onDone.rollback = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Tunnel::Tunnel --
 *
 *      Constrctor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BrokerXml::Tunnel::Tunnel()
   : statusPort(-1),
     generation(-1),
     bypassTunnel(false)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::Desktop::Desktop --
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

BrokerXml::Desktop::Desktop()
   : offlineState(OFFLINE_NONE),
     resetAllowed(false),
     resetAllowedOnSession(false),
     inMaintenance(false),
     defaultProtocol(0),
     progressWorkDoneSoFar(0),
     progressTotalWork(0)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerXml::DesktopConnection::DesktopConnection --
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

BrokerXml::DesktopConnection::DesktopConnection()
   : port(-1),
     enableUSB(false),
     enableMMR(false)
{
}


} // namespace cdk
