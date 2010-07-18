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
 * baseXml.cc --
 *
 *    Base XML API support
 */


#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <gmodule.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>
#include <algorithm>
#include <list>

#if defined(_WIN32) && !defined(__MINGW32__)
#define _(String) (String)
#define NOMINMAX
#endif


#include "baseXml.hh"
#include "cdkProxy.h"


#define XML_V1_HDR "<?xml version=\"1.0\"?>"

#define HTTP_IS_SUCCESS(x) ((x) >= BASICHTTP_RESPONSE_OK && (x) < BASICHTTP_RESPONSE_MULTIPLECHOICES)

namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::BaseXml --
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

BaseXml::BaseXml(const Util::string &docName,  // IN
                 const Util::string &hostname, // IN
                 int port,                     // IN
                 bool secure,                  // IN
                 const Util::string &sslCAPath)// IN
   : mHostname(hostname),
     mPort(port),
     mSecure(secure),
     mCookieJar(BasicHttp_CreateCookieJar()),
     mVersion(VERSION_4_5),
     mMulti(NULL),
     mResetWatch(NULL),
     mDocElementName(docName),
     mSslCAPath(sslCAPath),
     mRequestId(0)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::~BaseXml --
 *
 *      Destructor.  Cancel all active requests.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BaseXml::~BaseXml()
{
   ResetConnections();
   BasicHttp_FreeCookieJar(mCookieJar);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetContent --
 *
 *       Get the text content from a named child node.
 *
 * Results:
 *       Content Util::string, possibly empty.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BaseXml::GetContent(xmlNode *parentNode) // IN
{
   if (parentNode) {
      for (xmlNode *currentNode = parentNode->children; currentNode;
           currentNode = currentNode->next) {
         if (XML_TEXT_NODE == currentNode->type) {
            return (const char  *)currentNode->content;
         }
      }
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetChild --
 *
 *       Find a child node with a given name.
 *
 *       From apps/lib/basicSOAP/basicSoapCommon.c.
 *
 * Results:
 *       xmlNode or NULL.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

xmlNode *
BaseXml::GetChild(xmlNode *parentNode,    // IN
                  const char *targetName) // IN
{
   if (parentNode) {
      for (xmlNode *currentNode = parentNode->children; currentNode;
           currentNode = currentNode->next) {
         if (XML_ELEMENT_NODE == currentNode->type) {
            const char *currentName = (const char*) currentNode->name;
            /*
             * Be careful. XML is normally case-sensitive, but I am
             * being generous and allowing case differences.
             */
            if (currentName && (0 == Str_Strcasecmp(currentName, targetName))) {
               return currentNode;
            }
         }
      }
   }
   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetChildContent --
 *
 *       Get the text content from a named child node.
 *
 * Results:
 *       Content Util::string, possibly empty.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BaseXml::GetChildContent(xmlNode *parentNode,    // IN
                         const char *targetName) // IN
{
   if (parentNode) {
      xmlNode *node = GetChild(parentNode, targetName);
      if (node) {
         return GetContent(node);
      }
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetChildContentInt --
 *
 *       Get the int content from a named child node.
 *
 * Results:
 *       Integer value or 0 if invalid content or empty.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

int
BaseXml::GetChildContentInt(xmlNode *parentNode,    // IN
                            const char *targetName) // IN
{
   Util::string strval = GetChildContent(parentNode, targetName);
   if (strval.empty()) {
      return 0;
   }
   return strtol(strval.c_str(), NULL, 10);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetChildContentBool --
 *
 *       Get the bool content from a named child node.
 *
 * Results:
 *       true if XML value is "1", "TRUE", or "YES".  false, otherwise.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseXml::GetChildContentBool(xmlNode *parentNode,    // IN
                             const char *targetName) // IN
{
   Util::string strval = GetChildContent(parentNode, targetName);
   if (strval == "1" || strval == "true" || strval == "TRUE" ||
       strval == "yes" || strval == "YES") {
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::Result::Parse --
 *
 *       Parse the common <result> success/fault element returned in all
 *       requests.
 *
 * Results:
 *       true if parsed success result, false if parse failed or a fault result
 *       was received and the onAbort handler was invoked.
 *
 * Side effects:
 *       None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseXml::Result::Parse(xmlNode *parentNode,     // IN
                       Util::AbortSlot onAbort) // IN
{
   ASSERT(parentNode);

   result = GetChildContent(parentNode, "result");
   if (result.empty()) {
      onAbort(false, Util::exception(_("Invalid response"), "",
                                     _("Invalid \"result\" in XML.")));
      return false;
   }

   if (result == "ok") {
      errorCode.clear();
      errorMessage.clear();
      userMessage.clear();
   } else {
      // Non-ok is not necessarily a failure
      errorCode = GetChildContent(parentNode, "error-code");
      errorMessage = GetChildContent(parentNode, "error-message");
      userMessage = GetChildContent(parentNode, "user-message");
   }

   // Error code or message is always a failure
   if (!errorCode.empty() || !errorMessage.empty()) {
      if (!userMessage.empty()) {
         onAbort(false, Util::exception(userMessage, errorCode));
      } else if (!errorMessage.empty()) {
         onAbort(false, Util::exception(errorMessage, errorCode));
      } else {
         onAbort(false, Util::exception(Util::Format(_("Unknown error: %s"),
                                                       errorCode.c_str()),
                                        errorCode));
      }
      return false;
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::Param::Parse --
 *
 *       Parse a <param> node parentNode containing a name element and
 *       zero or more value elements.
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
BaseXml::Param::Parse(xmlNode *parentNode,     // IN
                      Util::AbortSlot onAbort) // IN
{
   name = GetChildContent(parentNode, "name");
   if (name.empty()) {
      onAbort(false, Util::exception(_("Invalid response"), "",
                                     _("Parameter with no name.")));
      return false;
   }

   readOnly = (GetChild(parentNode, "readonly") != NULL);

   xmlNode *valuesNode = GetChild(parentNode, "values");
   if (valuesNode) {
      for (xmlNode *valueNode = valuesNode->children; valueNode;
           valueNode = valueNode->next) {
         if (Str_Strcasecmp((const char *) valueNode->name, "value") == 0) {
            Util::string valueStr = GetContent(valueNode);
            if (!valueStr.empty()) {
               values.push_back(valueStr);
            }
         }
      }
   }

   if (values.size() == 0) {
      Util::string msg = Util::Format(_("Invalid response: "
                                        "Parameter \"%s\" has no value."),
                                      name.c_str());
      /*
       * XXX: When logging in with a cert, the broker sometimes is not
       * sending a value for the username (which we don't really care
       * about anyway).
       */
#if 0
      onAbort(false, Util::exception(msg));
      return false;
#else
      Warning("%s\n", msg.c_str());
#endif
   }

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::Encode --
 *
 *      Encode an XML text string, escaping entity characters correctly using
 *      xmlEncodeSpecialChars.
 *
 * Results:
 *      XML-safe encoded Util::string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BaseXml::Encode(const Util::string &val) // IN
{
   xmlChar *enc = xmlEncodeSpecialChars(NULL, (const xmlChar*)val.c_str());
   Util::string result = (const char*)enc;
   xmlFree(enc);
   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::SendRequest --
 *
 *      Post an XML API request using basicHttp.
 *
 * Results:
 *      true if request was queued successfully.
 *
 * Side effects:
 *      Takes ownership of 'req'.
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseXml::SendRequest(RequestState *req) // IN
{
   MultiRequestState *multi = dynamic_cast<MultiRequestState *>(req);

   if (multi) {
      ASSERT(!multi->requests.empty());
   } else {
      ASSERT(!req->requestOp.empty());
      ASSERT(!req->responseOp.empty());
      if (mMulti) {
         mMulti->requests.push_back(req);
         return true;
      }
   }

   Util::string body = GetDocumentElementTag();

#define REQUEST_BODY(aReq) \
   Util::Format((aReq)->args.empty() ? "<%s/>" : "<%s>%s</%s>",         \
                (aReq)->requestOp.c_str(), (aReq)->args.c_str(),        \
                (aReq)->requestOp.c_str());

   if (multi) {
      for (std::list<RequestState *>::iterator i = multi->requests.begin();
           i != multi->requests.end(); i++) {
         body += REQUEST_BODY(*i);
      }
   } else {
      body += REQUEST_BODY(req);
   }

   body += "</" + mDocElementName + ">";

   return SendHttpRequest(req, body);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::InvokeAbortOnConnectError --
 *
 *      Helper method to invoke onAbort handler when there is an HTTP error
 *      connecting to server.  This method is 'protected' therefore subclasses
 *      may invoke it from ResponseDispatch, if alwaysDispatchResponse is set on
 *      the RequestState.
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
BaseXml::InvokeAbortOnConnectError(BasicHttpErrorCode errorCode,       // IN
                                   BasicHttpResponseCode responseCode, // IN
                                   RequestState *state)                // IN
{
   Util::string code;
   Util::string detail;

#define ERR_CASE(c, d) case c: code = #c; detail = d; break

   /*
    * Treat unsuccessful HTTP responses (e.g. "503 Service unavailable")
    * as an HTTP errors.  This can also be done in basicHttp by using
    * CURLOPT_FAILONERROR.
    */
   if (errorCode == BASICHTTP_ERROR_NONE && !HTTP_IS_SUCCESS(responseCode)) {
      errorCode = BASICHTTP_ERROR_HTTP_RETURNED_ERROR;
   }

   switch (errorCode) {
   case BASICHTTP_ERROR_NONE:
      NOT_REACHED();
      break;
      ERR_CASE(BASICHTTP_ERROR_UNSUPPORTED_PROTOCOL,
               _("Unsupported protocol"));
      ERR_CASE(BASICHTTP_ERROR_URL_MALFORMAT,
               _("Invalid URL"));
      ERR_CASE(BASICHTTP_ERROR_COULDNT_RESOLVE_PROXY,
               _("The proxy could not be resolved"));
      ERR_CASE(BASICHTTP_ERROR_COULDNT_RESOLVE_HOST,
               _("The host could not be resolved"));
      ERR_CASE(BASICHTTP_ERROR_COULDNT_CONNECT,
               _("Could not connect to server"));
      ERR_CASE(BASICHTTP_ERROR_HTTP_RETURNED_ERROR,
               Util::Format(_("HTTP error %d"), responseCode));
      ERR_CASE(BASICHTTP_ERROR_OPERATION_TIMEDOUT,
               _("Connection timed out"));
      ERR_CASE(BASICHTTP_ERROR_SSL_CONNECT_ERROR,
               _("SSL connection error"));
      ERR_CASE(BASICHTTP_ERROR_TOO_MANY_REDIRECTS,
               _("Too many redirects"));

      /* n:1 mapped curl errors. */
      ERR_CASE(BASICHTTP_ERROR_TRANSFER,
               _("Transfer error"));
      ERR_CASE(BASICHTTP_ERROR_SSL_SECURITY,
               _("SSL security error"));

   default:
      /* generic error. */
      ERR_CASE(BASICHTTP_ERROR_GENERIC,
               _("Unknown error"));
   }

#undef ERR_CASE

   state->onAbort(
      false,
      Util::exception(
         _("The View Connection Server connection failed."), code.c_str(),
         Util::Format(_("%s.\n\n"
                        "Verify that the view connection server address, "
                        "port, network settings, and SSL settings are "
                        "correct and try again."), detail.c_str())));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::OnResponse --
 *
 *      Find the request's state, and set its response.  Add an idle
 *      callback to process the response after curl has processed its
 *      headers and freed this connection up in case we need to issue
 *      another RPC.
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
BaseXml::OnResponse(BasicHttpRequest *request,   // IN
                    BasicHttpResponse *response, // IN
                    void *data)                  // IN
{
   BaseXml *that = reinterpret_cast<BaseXml *>(data);
   ASSERT(that);

   for (std::list<RequestState *>::iterator i = that->mActiveRequests.begin();
        i != that->mActiveRequests.end(); i++) {
      RequestState *state = *i;
      if (state->request == request) {
         state->response = response;
         Poll_CallbackRemove(POLL_CS_MAIN, 0, OnIdleProcessResponses, that,
                             POLL_REALTIME);
         Poll_Callback(POLL_CS_MAIN, 0, OnIdleProcessResponses, that,
                       POLL_REALTIME, 0, NULL);
         break;
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::OnIdleProcessResponses --
 *
 *      Process any pending responses (probably at most one).
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
BaseXml::OnIdleProcessResponses(void *data) // IN
{
   BaseXml *that = reinterpret_cast<BaseXml *>(data);
   ASSERT(that);

   std::list<RequestState *>::iterator i = that->mActiveRequests.begin();
   while (i != that->mActiveRequests.end()) {
      RequestState *state = *i;
      if (!state->response) {
         ++i;
      } else {
         i = that->mActiveRequests.erase(i);
         bool wasDeleted = that->ProcessResponse(state);
         delete state;
         if (wasDeleted) {
            break;
         }
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::ProcessResponse --
 *
 *      Parse an XML API response based on the response operation.  Invokes the
 *      onAbort/onDone handler passed to the initial request.
 *
 * Results:
 *      TRUE if no more responses should be processed.
 *
 * Side effects:
 *      Callbacks called from this function may have deleted this
 *      object before they return.
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseXml::ProcessResponse(RequestState *state) // IN
{
   // XXX: keep code churn down (this was originally static)
   BaseXml *that = this;

   BasicHttpRequest *request = state->request;
   BasicHttpResponse *response = state->response;

   xmlDoc *doc = NULL;
   xmlNode *docNode;
   xmlNode *operationNode;
   Result result;
   bool wasReset = false;
   bool *resetWatch = &wasReset;

   that->mRequestId++;

   if (that->mResetWatch) {
      resetWatch = that->mResetWatch;
   } else {
      that->mResetWatch = resetWatch;
   }

   /*
    * If we've been redirected and we're not using a proxy, then we can run into
    * long delays due to cURL leaving connections open.  Thus, we need to use
    * the redirected protocol and port in the future.  See bz 513320.
    */
#if !defined(_WIN32) || defined(__MINGW32__)
   if (response->effectiveURL) {
      unsigned short port;
      bool secure;

      Util::string host =
         Util::ParseHostLabel(response->effectiveURL, &port, &secure);
      if (!host.empty()) {
         mHostname = host;
         mPort = port;
         mSecure = secure;
      }
   }
#endif // !defined(_WIN32) || defined(__MINGW32)

   MultiRequestState *multi = dynamic_cast<MultiRequestState *>(state);
   std::list<RequestState *> requests;

   if (response->errorCode != BASICHTTP_ERROR_NONE
       || !HTTP_IS_SUCCESS(response->responseCode)) {
      Log("Could not connect to server. (BasicHttp error=%u, response=%ld)\n",
          response->errorCode, response->responseCode);
      /*
       * If alwaysDispatchResponse is true, dispatch response without a node and
       * with an empty Result object.  Only call abort slot if dispatch response
       * somehow does not process response.
       */
      bool doAbort = true;
      if (state->alwaysDispatchResponse) {
         doAbort = !(that->ResponseDispatch(NULL, *state, result));
      }
      if (doAbort) {
         InvokeAbortOnConnectError(response->errorCode,
                                   response->responseCode, state);
      }
      goto exit;
   }

   DEBUG_ONLY(Warning("BROKER RESPONSE: %s\n", response->content));

   doc = xmlReadMemory(response->content, strlen(response->content),
                       "notused.xml", NULL, 0);

   if (!doc) {
      Warning("The response could not be parsed as XML.\n");
      goto malformedXml;
   }

   docNode = xmlDocGetRootElement(doc);
   if (!docNode ||
       Str_Strcasecmp((const char*)docNode->name,
                      that->mDocElementName.c_str()) != 0) {
      Warning("No <%s> root element found in document.\n",
              that->mDocElementName.c_str());
      goto malformedXml;
   }

   // Protocol-level errors mean no operation node
   if (GetChildContent(docNode, "result") == "error") {
      Util::string errCode = GetChildContent(docNode, "error-code");
      Log("%s XML general error: %s\n", that->mDocElementName.c_str(),
          errCode.c_str());
      if (result.Parse(docNode, state->onAbort)) {
         state->onAbort(false, Util::exception(_("Invalid response"), "",
                                               _("General error.")));
      }
      goto exit;
   }

   if (multi) {
      requests = multi->requests;
   } else {
      requests.push_back(state);
   }

   for (std::list<RequestState *>::iterator i = requests.begin();
        i != requests.end(); i++) {
      RequestState *curState = *i;

      /*
       * XXX: this assumes we don't have more than one of a given
       * responseOp per RPC.
       */
      operationNode = GetChild(docNode, curState->responseOp.c_str());
      if (!operationNode) {
         Warning("No <%s> child of <%s>\n", curState->responseOp.c_str(),
                 that->mDocElementName.c_str());
         Util::string msg = Util::Format(
            _("Invalid response: Unknown response \"%s\"."),
            curState->responseOp.c_str());
         curState->onAbort(false, Util::exception(msg));
         if (*resetWatch) {
            goto exit;
         }
         continue;
      }

      if (!result.Parse(operationNode, curState->onAbort) && !*resetWatch) {
         continue;
      }
      if (*resetWatch) {
         goto exit;
      }

      // In case responseDispatch() wants to use the request and/or response.
      curState->request = request;
      curState->response = response;

      if (curState->isRaw) {
#if LIBXML_VERSION >= 20623
         xmlBuffer *xmlBuf = xmlBufferCreate();
         xmlSaveCtxt *ctxt = xmlSaveToBuffer(xmlBuf, "UTF-8", (xmlSaveOption)0);
         if (-1 == xmlSaveTree(ctxt, operationNode)) {
            // XXX: this error message is not the best.
            Util::string msg = Util::Format(
               _("Unable to save command text for %s."), curState->responseOp.c_str());
            curState->onAbort(false, Util::exception(msg));
         } else {
            xmlSaveFlush(ctxt);
            Util::string responseStr = Util::Format("%s%s</%s>",
                                             that->GetDocumentElementTag().c_str(),
                                             (const char *)xmlBuf->content,
                                             that->mDocElementName.c_str());
            curState->onDoneRaw(result, responseStr);
         }
         xmlSaveClose(ctxt);
         xmlBufferFree(xmlBuf);
#else
         NOT_IMPLEMENTED();
#endif
      } else if (!that->ResponseDispatch(operationNode, *curState, result) &&
                 !*resetWatch) {
         Util::string msg = Util::Format(
            _("Invalid response: Unknown response \"%s\"."),
            curState->responseOp.c_str());
         curState->onAbort(false, Util::exception(msg));
      }
      if (*resetWatch) {
         goto exit;
      }
   }

   if (multi && !multi->onQueuedDone.empty()) {
      multi->onQueuedDone(doc, response->responseCode);
   }
   goto exit;

malformedXml:
   {
      Util::string msg = Util::Format(
         _("The server \"%s\" may not be a compatible View Connection "
           "Server. Check the server address and try again."),
         that->mHostname.c_str());
      state->onAbort(false, Util::exception(msg));
   }

exit:
   if (!*resetWatch) {
      if (resetWatch == &wasReset) {
         that->mResetWatch = NULL;
      }
      BasicHttp_FreeRequest(request);
      state->request = NULL;
      BasicHttp_FreeResponse(response);
      state->response = NULL;
   }
   xmlFreeDoc(doc);

   return *resetWatch;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::SendRawCommand --
 *
 *      Sends a raw XML command.  The response will not be processed in the
 *      normal fashion.  Instead, the XML string will be passed to the
 *      callback.
 *
 * Result:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
BaseXml::SendRawCommand(Util::string command,    // IN
                        Util::string response,   // IN
                        Util::string args,       // IN
                        Util::AbortSlot onAbort, // IN
                        RawSlot onDone)          // IN
{
   RequestState *req = new RequestState();
   req->requestOp = command;
   req->responseOp = response;
   req->isRaw = true;
   req->args = args;
   req->onAbort = onAbort;
   req->onDoneRaw = onDone;
   SendRequest(req);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::CancelRequests --
 *
 *      Cancel pending HTTP requests.
 *
 * Results:
 *      int - number of cancelled requests.
 *
 * Side effects:
 *      Request onAbort handlers are run with cancelled = true.
 *
 *-----------------------------------------------------------------------------
 */

int
BaseXml::CancelRequests()
{
   // Remove any pending completed responses.
   Poll_CallbackRemove(POLL_CS_MAIN, 0, OnIdleProcessResponses, this,
                       POLL_REALTIME);

   std::list<RequestState*> &pendingRequests = mMulti ? mMulti->requests
                                                      : mActiveRequests;

   std::list<Util::AbortSlot> slots;
   /*
    * It is extremely likely that an onAbort() handler will delete
    * this object, which will re-enter here and double-free things, so
    * clear the list, and then call the abort handlers.
    */
   for (std::list<RequestState *>::iterator i = pendingRequests.begin();
        i != pendingRequests.end(); i++) {
      BasicHttp_FreeRequest((*i)->request);
      slots.push_back((*i)->onAbort);
      delete *i;
   }
   pendingRequests.clear();
   delete mMulti;
   mMulti = NULL;

   Log("Cancelling %d %s XML requests.\n", (int)slots.size(),
       mDocElementName.c_str());
   for (std::list<Util::AbortSlot>::iterator i = slots.begin();
        i != slots.end(); i++) {
      (*i)(true, Util::exception(_("Request cancelled by user.")));
   }
   return slots.size();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::ForgetCookies --
 *
 *      Forget all stored cookies.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Cookie jar is recreated.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseXml::ForgetCookies()
{
   BasicHttp_NewCookieSession(mCookieJar);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::SetCookieFile --
 *
 *      This loads and subsequently saves the cookies to the passed-in
 *      file.
 *
 *      Note that cookies aren't actually loaded until a connection is
 *      made.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A cookie file will be used.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseXml::SetCookieFile(const Util::string &cookieFile) // IN
{
   BasicHttp_FreeCookieJar(mCookieJar);
   mCookieJar = BasicHttp_CreateCookieFile(cookieFile.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::ResetConnections --
 *
 *      Force cURL to close all connections.  This resets BasicHttp,
 *      which is a bit heavy handed, but we are wading through a lot
 *      of layers of abstractions, and this works.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      BasicHttp is reinitialized.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseXml::ResetConnections()
{
   CancelRequests();
   BasicHttp_Shutdown();
   BasicHttp_Init(Poll_Callback, Poll_CallbackRemove);
   if (mResetWatch) {
      *mResetWatch = true;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::OnSslCtx --
 *
 *      Callback from basicHttp when an SSL context is set up by cURL.
 *      Add our certificate request handler.
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
BaseXml::OnSslCtx(BasicHttpRequest *request, // IN
                  void *sslctx,              // IN
                  void *clientData)          // IN
{
   BaseXml *that = reinterpret_cast<BaseXml*>(clientData);
   ASSERT(that);
   SSL_CTX *ctx = (SSL_CTX *)sslctx;
   SSL_CTX_set_app_data(ctx, that);

   /*
    * SSL_CTX_set_client_cert_cb was turned into a real function in
    * 0.9.8e. Prior to that it was just a macro to set a member of the
    * SSL_CTX directly.  This tries to use the function if it's
    * available, or sets the member directly if not.
    */
   static bool certCbChecked = false;
   static gboolean hasCertCb = false;
   if (!certCbChecked) {
      GModule *self = g_module_open(NULL, (GModuleFlags)0);
      gpointer func = NULL;
      hasCertCb = g_module_symbol(self, "SSL_CTX_set_client_cert_cb", &func);
      g_module_close(self);
      DEBUG_ONLY(Log("Has SSL_CTX_set_client_cert_cb: %d\n", hasCertCb));
   }
   if (hasCertCb) {
      SSL_CTX_set_client_cert_cb(ctx, OnCertificateRequest);
   } else {
      ctx->client_cert_cb = OnCertificateRequest;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::OnCertificateRequest --
 *
 *      Callback when the server requests a certificate.  Emit the
 *      certificateRequested signal.
 *
 * Results:
 *      Those of the certificate requested signal.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
BaseXml::OnCertificateRequest(SSL *ssl,           // IN
                              X509 **x509,        // OUT
                              EVP_PKEY **privKey) // OUT
{
   BaseXml *that =
      reinterpret_cast<BaseXml *>(SSL_CTX_get_app_data(ssl->ctx));
   ASSERT(that);
   return that->certificateRequested(ssl, x509, privKey);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::MultiRequestState::~MultiRequestState --
 *
 *      Destructor - frees all child request states.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BaseXml::MultiRequestState::~MultiRequestState()
{
   for (std::list<RequestState *>::iterator i = requests.begin();
        i != requests.end(); i++) {
      delete *i;
   }
   requests.clear();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::QueueRequests --
 *
 *      Begin creating a multi-command request.  Future calls to
 *      SendRequest with a singular request will have them queued,
 *      rather than them being sent.
 *
 *      Note that this does not support queueing multiple requests of
 *      the same command.
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
BaseXml::QueueRequests()
{
   ASSERT(!mMulti);
   mMulti = new MultiRequestState();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::SendQueuedRequests --
 *
 *      Create and send a request containing the commands that have
 *      been queued.  Subsequent calls to SendRequest() will not be
 *      queued without an additional call to QueueRequests().
 *
 *      The onDone slot is invoked on receiving the response from the broker
 *      and after all the requests have been processed.
 *
 * Results:
 *      See SendRequest() for results.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseXml::SendQueuedRequests(Util::AbortSlot onAbort,        // IN/OPT:
                            QueuedRequestsDoneSlot onDone)  // IN/OPT:
{
   ASSERT(mMulti);
   MultiRequestState *multi = mMulti;
   mMulti = NULL;
   /*
    * If an AbortSlot for the multi-rpc is not specified,
    * forward HTTP-level errors to the first request, as happens in a
    * single request.
    */
   multi->onAbort = onAbort.empty() ? multi->requests.front()->onAbort
                                    : onAbort;
   multi->onQueuedDone = onDone;

   bool rv = SendRequest(multi);
   if (!rv) {
      delete multi;
   }
   return rv;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::SendHttpRequest --
 *
 *     Makes an HTTP request with 'body' as the body of the request.
 *
 * Results:
 *     true if request sent successfully, false otherwise.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseXml::SendHttpRequest(RequestState *req,        // IN:
                         const Util::string &body) // IN:
{
#ifdef VMX86_DEBUG
   xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
   xmlDocPtr tmpdoc;

   if (ctxt) {
      tmpdoc = xmlCtxtReadMemory(ctxt, body.c_str(), body.length(), "noname.xml",
                                 NULL, XML_PARSE_DTDVALID);
      if (tmpdoc) {
         if (!ctxt->wellFormed) {
            Warning("XML is not well formed\n");
         }
         xmlFreeDoc(tmpdoc);
      } else {
         Warning("Failed to parse the XML\n");
      }
      xmlFreeParserCtxt(ctxt);
   } else {
      Warning("No parser context, skipping XML validation...\n");
   }
#endif

   // NOTE: We get a 404 if we access "/<base name>/xml/"
   Util::string url = Util::Format("%s://%s:%hu/%s/xml",
                                   mSecure ? "https" : "http",
                                   mHostname.c_str(), mPort,
                                   mDocElementName.c_str());

#ifdef VMX86_DEBUG
   Warning("BROKER REQUEST: %s\n", CensorXml(body).c_str());
#endif

   const char *sslCAPath = !mSslCAPath.empty() ? mSslCAPath.c_str() : NULL;

   /*
    * Note that sslCAPath, if non-null, is used by BasicHttp for certificate
    * verificaiton. Also note that BasicHttp is currently implemented so that it
    * never checks the hostname with the CN on the certificate.
    *
    * The CN on the default self-signed certificate during connection broker
    * installation is "VMware View", not the hostname.  This will remain so
    * until a customer requests their own certificate to fit into their PKI
    * infrastructure.  Therefore, hostname is not verified.
    */
   req->request = BasicHttp_CreateRequestWithSSL(url.c_str(),
                                                 BASICHTTP_METHOD_POST,
                                                 mCookieJar, NULL, body.c_str(),
                                                 sslCAPath);
   ASSERT_MEM_ALLOC(req->request);

   BasicHttp_SetSslCtxProc(req->request, OnSslCtx);

   if (req->proxy.empty()) {
      CdkProxyType proxyType = CDK_PROXY_NONE;
      char *proxy = CdkProxy_GetProxyForUrl(url.c_str(), &proxyType);
      if (proxy) {
         switch (proxyType) {
         case CDK_PROXY_HTTP:
            req->proxyType = BASICHTTP_PROXY_HTTP;
	    break;
         case CDK_PROXY_SOCKS4:
            req->proxyType = BASICHTTP_PROXY_SOCKS4;
	    break;
         default:
	    NOT_REACHED();
	    break;
         }
         req->proxy = proxy;
         free(proxy);
      } else {
         req->proxyType = BASICHTTP_PROXY_NONE;
      }
   }
   if (req->proxyType != BASICHTTP_PROXY_NONE) {
      ASSERT(!req->proxy.empty());
      BasicHttp_SetProxy(req->request, req->proxy.c_str(), req->proxyType);
   }

   BasicHttp_SetConnectTimeout(req->request, CalculateConnectTimeout(req));

   for (size_t i = 0; i < req->extraHeaders.size(); i++) {
      BasicHttp_AppendRequestHeader(req->request,
                                    req->extraHeaders[i].c_str());
   }

   Bool success = BasicHttp_SendRequest(req->request, &BaseXml::OnResponse,
                                        this);
   if (success) {
      mActiveRequests.push_back(req);
   }

   return success ? true : false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetProtocolVersionStr --
 *
 *      Convert current XML API protocol version to a string.
 *
 * Results:
 *      XML-safe Util::string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BaseXml::GetProtocolVersionStr()
   const
{
   switch (mVersion) {
   case VERSION_1:   return "1.0";
   case VERSION_2:   return "2.0";
   case VERSION_3:   return "3.0";
   case VERSION_4:   return "4.0";
   case VERSION_4_5: return "4.5";
   }
   NOT_IMPLEMENTED();
}


/*
 *-----------------------------------------------------------------------------
 *
 *  cdk::BaseXml::CalculateConnectTimeout --
 *
 *     Calculates the minimum request timeout of all the requests in a multi-request.
 *
 * Results:
 *     The timeout to be used for the entire multi-rpc. 0 implies no timeout.
 *
 * Side effects:
 *     None.
 *
 *-----------------------------------------------------------------------------
 */

unsigned long
BaseXml::CalculateConnectTimeout(const cdk::BaseXml::RequestState *req)  // IN:
   const
{
   unsigned long timeoutSec = 0;

   if (const MultiRequestState *multiReq =
         dynamic_cast<const MultiRequestState *>(req)) {
      /*
       * This is a multi-request.
       * Compute the minimum non-zero timeout of all the requests in the multi-request
       */
      BOOST_FOREACH(const RequestState *reqState, multiReq->requests) {
         if (reqState->connectTimeoutSec != 0) {
            timeoutSec = timeoutSec == 0 ? reqState->connectTimeoutSec
                                         : std::min(timeoutSec,
                                                    reqState->connectTimeoutSec);
         }
      }
   } else {
      // Uni-request. Simply return the request timeout from reqState.
      timeoutSec = req->connectTimeoutSec;
   }

   return timeoutSec;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetDocumentElementTag --
 *
 *     Gets the appropriate XML header string for the XML-API
 *     requests/responses.  The header string depends on the broker version.
 *
 * Results:
 *     String containing the XML header.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
BaseXml::GetDocumentElementTag()
   const
{
   return Util::Format(XML_V1_HDR "<%s version=\"%s\">",
                       mDocElementName.c_str(),
		       GetProtocolVersionStr().c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::GetChildContentUInt64 --
 *
 *     Get the uint64 content from a named child node.
 *
 * Results:
 *     Integer value or 0 if invalid content or empty.
 *
 * Side effects:
 *     None.
 *
 *-----------------------------------------------------------------------------
 */
uint64
BaseXml::GetChildContentUInt64(xmlNode *parentNode,    // IN
                               const char *targetName) // IN
{
   Util::string strVal = GetChildContent(parentNode, targetName);
   if (strVal.empty()) {
      return 0;
   }

   return g_ascii_strtoull(strVal.c_str(), NULL, 10);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseXml::CensorXml --
 *
 *      Mask sensitive data within XML content (input as an in memory string.)
 *
 * Preconditions:
 *      XML input string is well formed, i.e., matching begin/end tags, etc.
 *
 * Results:
 *     Copy of input string APPENDED to output string with sensitive
 *     values masked, i.e., censored, suitable for use in logging, etc.
 *     Returns the censored string passed in by the caller.
 *
 *-----------------------------------------------------------------------------
 */
#ifdef  VMX86_DEBUG
Util::string
BaseXml::CensorXml(const Util::string &xmlStr) // IN
{
    Util::string censored;

    if (xmlStr.c_str()[0] == '\0') {
        return censored;
    }

    const char *xml = xmlStr.c_str();

    static const char *params[] = {
                                    "<name>password</name>",
                                    "<name>passcode</name>",
                                    "<name>pin",
                                    "<name>smartCardPIN</name>"
    };
    static const char *startValueTag = "<value>";
    static const char *endValueTag = "</value>";

    /*
     * Scan the input xml and build a list of pointers into
     * the input demarking the safe text sections around the
     * text segments to be censored.
     */
    std::vector<const char*> safe_text;

    // safe text starts at the beginning of the input text.
    safe_text.push_back(xml);

    for (size_t i = 0; i < G_N_ELEMENTS(params); ++i) {
        const char *param = strstr(xml, params[i]);
        while (param) {
            /*
             * We found a password or pin parameter - now find
             * its assocated value tag.
             */
            const char *startValue = strstr(param, startValueTag);
            if (startValue) {
                // now find the value end tag - pwd/pin is between these tags.
                const char *endCensor = strstr(startValue, endValueTag);
                if (endCensor) {
                    const char *startCensor = startValue + strlen(startValueTag);
                    safe_text.push_back(startCensor);
                    safe_text.push_back(endCensor);
                }
            }

            /*
             * Continue processing until all params of
             * this kind have been added to safe_text.
             */
            param = strstr(param + 1, params[i]);
        }
    }
    // safe text ends at the end of the input.
    safe_text.push_back(xml + strlen(xml));

    /*
     * Since we gathered the safe text segments in order of param
     * "type", not in sequential order as they occur in the input,
     * we need to sort the safe_text pointers so we can then copy
     * the text they delineate into the result string in order.
     */
    std::sort(safe_text.begin(), safe_text.end());

    /*
     * Traverse the safe_text pointers copying the safe text segments
     * into the result string, replacing each section they demark with
     * a mask character, thus censoring the input text.
     */
    for(int i = 0, n = safe_text.size(); i < n; i += 2) {
        censored.append(safe_text[i], safe_text[i + 1] - safe_text[i]);
        if (i < (n - 2)) {
            censored.append("*");
        }
    }
    return censored;
}
#endif  // VMX86_DEBUG


} // namespace cdk
