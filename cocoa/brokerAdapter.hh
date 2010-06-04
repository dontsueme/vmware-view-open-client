/*********************************************************
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
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
 * brokerAdapter.hh --
 *
 *      This is really an adapter for a cdk::Broker::Delegate - it
 *      passes on C++ calls to an Objective-C delegate.
 */

#ifndef BROKER_ADAPTER_HH
#define BROKER_ADAPTER_HH


#import "broker.hh"


@class CdkBroker;


namespace cdk {


class BrokerAdapter
   : virtual public Broker::Delegate
{
public:
   BrokerAdapter() : broker(NULL) { }

   void SetBroker(CdkBroker *aBroker) { broker = aBroker; }
   CdkBroker *GetBroker() const { return broker; }

   virtual void Disconnect();

   // State change notifications
   virtual void RequestBroker();
   virtual void RequestDisclaimer(const Util::string &disclaimer);
   virtual void RequestCertificate(std::list<Util::string> &trustedIssuers);
   virtual void RequestPasscode(const Util::string &username,
                                bool userSelectable);
   virtual void RequestNextTokencode(const Util::string &username);
   virtual void RequestPinChange(const Util::string &pin,
                                 const Util::string &message,
                                 bool userSelectable);
   virtual void RequestPassword(const Util::string &username,
                                bool readOnly,
                                const std::vector<Util::string> &domains,
                                const Util::string &domain);
   virtual void RequestPasswordChange(const Util::string &username,
                                      const Util::string &domain);
   virtual void RequestDesktop();
   // virtual void RequestTransition(const Util::string &message);
   virtual void RequestLaunchDesktop(Desktop *desktop);
   virtual void TunnelDisconnected(Util::string disconnectReason);
   virtual void UpdateDesktops();

private:
   CdkBroker *broker;
};


} // namespace cdk


#endif // BROKER_ADAPTER_HH
