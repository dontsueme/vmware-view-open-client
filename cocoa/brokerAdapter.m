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
 * brokerAdapter.m --
 *
 *      Implemention of cdk::BrokerAdapter.
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import <Cocoa/Cocoa.h>


#import "brokerAdapter.hh"
#import "cdkBroker.h"
#import "cdkDesktop.h"
#import "cdkKeychain.h"
#import "cdkString.h"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestBroker --
 *
 *      Request the controller to display the broker dialog.
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
BrokerAdapter::RequestBroker()
{
   [[broker delegate] brokerDidRequestBroker:(CdkBroker *)broker];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestDisclaimer --
 *
 *      Request the controller to display the disclaimer dialog.
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
BrokerAdapter::RequestDisclaimer(const Util::string &disclaimer) // IN
{
   [[broker delegate] broker:broker
        didRequestDisclaimer:[NSString stringWithUtilString:disclaimer]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestPasscode --
 *
 *      Request that the controller displays the SecurID dialog.
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
BrokerAdapter::RequestPasscode(const Util::string &username, // IN
                               bool userSelectable)          // IN
{
   [[broker delegate] broker:(CdkBroker *)broker
          didRequestPasscode:[NSString stringWithUtilString:username]
              userSelectable:userSelectable];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestNextTokencode --
 *
 *      Request that the controller displays the next tokencode
 *      dialog.
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
BrokerAdapter::RequestNextTokencode(const Util::string &username)
{
   [[broker delegate] broker:broker
     didRequestNextTokencode:[NSString stringWithUtilString:username]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestPassword --
 *
 *      Request the controller to display the password dialog.
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
BrokerAdapter::RequestPassword(const Util::string &username,             // IN
                               bool readOnly,                            // IN
                               const std::vector<Util::string> &domains, // IN
                               const Util::string &suggestedDomain)      // IN
{
   NSMutableArray *a = [NSMutableArray arrayWithCapacity:domains.size()];
   for (std::vector<Util::string>::const_iterator i = domains.begin();
	i != domains.end(); i++) {
      [a addObject:[NSString stringWithUtilString:*i]];
   }
   [[broker delegate] broker:broker
          didRequestPassword:[NSString stringWithUtilString:username]
                    readOnly:readOnly
                     domains:a
             suggestedDomain:[NSString stringWithUtilString:suggestedDomain]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestChangePassword --
 *
 *      Request the controller for a new password.
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
BrokerAdapter::RequestPasswordChange(const Util::string &username, // IN
                                     const Util::string &domain)   // IN
{
   [[broker delegate] broker:broker
    didRequestPasswordChange:[NSString stringWithUtilString:username]
                      domain:[NSString stringWithUtilString:domain]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestPinChange --
 *
 *      Prompt the user for a new SecurID PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Pin change dialog is shown.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerAdapter::RequestPinChange(const Util::string &pin1,
                      const Util::string &message,
                      bool userSelectable)
{
   [[broker delegate] broker:broker
         didRequestPinChange:[NSString stringWithUtilString:pin1]
                     message:[NSString stringWithUtilString:message]
              userSelectable:userSelectable];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestDesktop --
 *
 *      Request the controller to display the desktop selection
 *      dialog.
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
BrokerAdapter::RequestDesktop()
{
   [[broker delegate] brokerDidRequestDesktop:broker];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::RequestLaunchDesktop --
 *
 *      Request the controller to launch a desktop.
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
BrokerAdapter::RequestLaunchDesktop(Desktop *desktop) // IN
{
   [[broker delegate] broker:broker
     didRequestLaunchDesktop:[CdkDesktop desktopWithDesktop:desktop]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::Disconnect --
 *
 *      Quit the application.
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
BrokerAdapter::Disconnect()
{
   [[broker delegate] brokerDidDisconnect:broker];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::TunnelDisconnected --
 *
 *      The tunnel has disconnected; display the message to the user.
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
BrokerAdapter::TunnelDisconnected(cdk::Util::string disconnectReason) // IN
{
   [[broker delegate] broker:broker
                      didDisconnectTunnelWithReason:
                         [NSString stringWithUtilString:disconnectReason]];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::GetCertAuthInfo --
 *
 *      Get a certificate and private key to use for authentication.
 *
 * Results:
 *      Returns a certificate, and privKey points to a private key, or
 *      both may be NULL on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerAdapter::RequestCertificate(std::list<Util::string> &trustedIssuers) // IN
{
   NSMutableArray *issuers =
      [NSMutableArray arrayWithCapacity:trustedIssuers.size()];

   for (std::list<Util::string>::iterator i = trustedIssuers.begin();
        i != trustedIssuers.end(); ++i) {
      [issuers addObject:[NSString stringWithUtilString:*i]];
   }

   [[broker delegate] broker:broker
                      didRequestCertificateWithIssuers:issuers];
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerAdapter::UpdateDesktops --
 *
 *      Tell the delegate to update its desktop list.
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
BrokerAdapter::UpdateDesktops()
{
   [[broker delegate] brokerDidRequestUpdateDesktops:broker];
}


} // namespace cdk
