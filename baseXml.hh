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
 * baseXml.hh --
 *
 *    Base XML API support.
 */

#ifndef BASE_XML_HH
#define BASE_XML_HH

#if defined(_WIN32) && !defined(__MINGW32__)
#include <atlbase.h>
#endif

#include <boost/function.hpp>
#include <boost/signal.hpp>
#include <libxml/tree.h>
#include <list>
#include <openssl/ssl.h>
#include <vector>


#include "basicHttp.h"
#include "util.hh"


namespace cdk {

class BaseXml
{
public:
   enum Version {
      VERSION_1 = 1,
      VERSION_2,
      VERSION_3,
      VERSION_4,
      VERSION_4_5,
   };

   enum DefaultPort {
      PORT_HTTP = 80,
      PORT_HTTP_SSL = 443
   };

   struct Result
   {
      Util::string result;
      Util::string errorCode;
      Util::string errorMessage;
      Util::string userMessage;

      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   struct Param
   {
      Util::string name;
      std::vector<Util::string> values;
      bool readOnly;

      Param() : readOnly(false) { }
      bool Parse(xmlNode *parentNode, Util::AbortSlot onAbort);
   };

   typedef boost::function2<void, Result&, Util::string> RawSlot;
   typedef boost::function2<void, xmlDoc *, BasicHttpResponseCode> QueuedRequestsDoneSlot;

   BaseXml(const Util::string &docName, const Util::string &hostname, int port,
           bool secure, const Util::string &sslCAPath = "");
   virtual ~BaseXml();

   Util::string GetHostname() const { return mHostname; }
   int GetPort() const { return mPort; }
   bool GetSecure() const { return mSecure; }
   unsigned int GetRequestId() const { return mRequestId; }

   Version GetProtocolVersion() const { return mVersion; }
   void SetProtocolVersion(Version version) { mVersion = version; }

   void SendRawCommand(Util::string command,
                       Util::string response,
                       Util::string args,
                       Util::AbortSlot onAbort,
                       RawSlot onDone);

   void QueueRequests();
   bool QueueingRequests() const { return mMulti != NULL; }
   bool SendQueuedRequests(Util::AbortSlot onAbort = Util::AbortSlot(),
                           QueuedRequestsDoneSlot onDone = QueuedRequestsDoneSlot());

   int CancelRequests();
   void ForgetCookies();
   void SetCookieFile(const Util::string &cookieFile);
   void ResetConnections();

   boost::signal3<int, SSL *, X509 **, EVP_PKEY **> certificateRequested;

   static Util::string Encode(const Util::string &val);

#ifdef  VMX86_DEBUG
   Util::string CensorXml(const Util::string &xml);
#endif  // VMX86_DEBUG

protected:
   struct RequestState
   {
      Util::string requestOp;
      Util::string responseOp;
      std::vector<Util::string> extraHeaders;
      bool alwaysDispatchResponse;
      bool isRaw;
      Util::string args;
      Util::AbortSlot onAbort;
      RawSlot onDoneRaw;
      BasicHttpRequest *request;
      BasicHttpResponse *response;
      Util::string proxy;
      BasicHttpProxyType proxyType;
      unsigned long connectTimeoutSec;

      RequestState() :
         alwaysDispatchResponse(false),
         isRaw(false),
         request(NULL),
         response(NULL),
         proxyType(BASICHTTP_PROXY_NONE),
         connectTimeoutSec(0)
      {
      }

      virtual ~RequestState() { }
   };

   struct MultiRequestState
      : public RequestState
   {
      ~MultiRequestState();
      std::list<RequestState *> requests;
      QueuedRequestsDoneSlot onQueuedDone;
   };

   static Util::string GetContent(xmlNode *parentNode);
   static xmlNode *GetChild(xmlNode *parentNode, const char *targetName);
   static Util::string GetChildContent(xmlNode *parentNode, const char *targetName);
   static int GetChildContentInt(xmlNode *parentNode, const char *targetName);
   static bool GetChildContentBool(xmlNode *parentNode, const char *targetName);
   static uint64 GetChildContentUInt64(xmlNode *parentNode,
                                       const char *targetName);

   static void InvokeAbortOnConnectError(BasicHttpErrorCode errorCode,
                                         BasicHttpResponseCode responseCode,
                                         RequestState *state);
   bool SendRequest(RequestState *req);
   virtual bool SendHttpRequest(RequestState *req, const Util::string &body);

   virtual bool ResponseDispatch(xmlNode *operationNode,
                                 RequestState &state,
                                 Result &result) { return false; }

   virtual Util::string GetDocumentElementTag() const;
   Util::string GetProtocolVersionStr() const;
   void SetDocElementName(const Util::string &elemName)
   { ASSERT(!elemName.empty()); mDocElementName = elemName; }

   unsigned long CalculateConnectTimeout(const RequestState *req) const;

private:
   static void OnResponse(BasicHttpRequest *request,
                          BasicHttpResponse *response,
                          void *data);
   static void OnIdleProcessResponses(void *data);
   static void OnSslCtx(BasicHttpRequest *request, void *sslctx,
                        void *clientData);
   static int OnCertificateRequest(SSL *ssl, X509 **x509, EVP_PKEY **pkey);

   bool ProcessResponse(RequestState *state);

   std::list<RequestState *> mActiveRequests;
   Util::string mHostname;
   unsigned short mPort;
   bool mSecure;
   BasicHttpCookieJar *mCookieJar;
   Version mVersion;
   MultiRequestState *mMulti;
   bool *mResetWatch;
   Util::string mDocElementName;
   Util::string mSslCAPath;
   unsigned int mRequestId;
};


} // namespace cdk


#endif // BASE_XML_HH
