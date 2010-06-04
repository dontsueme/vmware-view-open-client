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
 * brokerDlg.hh --
 *
 *    Broker selection dialog.
 */

#ifndef BROKER_DLG_HH
#define BROKER_DLG_HH


#include <boost/signal.hpp>
#include <gtk/gtk.h>


#include "dlg.hh"
#include "util.hh"


namespace cdk {


class BrokerDlg
   : public Dlg
{
public:
   BrokerDlg(Util::string initialBroker = "");
   ~BrokerDlg() { }

   Util::string GetBroker() const { return mServer; }
   void SavePrefs();
   short unsigned int GetPort() const { return mPort; }
   bool GetSecure() const { return mSecure; }
   // overrides Dlg::IsValid()
   virtual bool IsValid();
   virtual Util::string GetHelpContext() { return "connect"; }

private:
   enum FreezeState {
      FREEZE_NOTHING,
      FREEZE_BROKER,
      FREEZE_PORT,
      FREEZE_SECURE
   };

   static void OnBrokerChanged(GtkComboBox *combo, gpointer userData);
   static void OnPortChanged(GtkSpinButton *spin, gpointer userData);
   static void OnSecureChanged(GtkToggleButton *toggle, gpointer userData);
   static void OnAutoChanged(GtkToggleButton *toggle, gpointer userData);
   static void OnOptionsExpanded(GObject *object, GParamSpec *paramSpec,
                                 gpointer userData);

   void ParseBroker();
   void UpdateUI(FreezeState freezeState);

   GtkTable *mTable;
   GtkComboBoxEntry *mBroker;
   GtkLabel *mPortLabel;
   GtkSpinButton *mPortEntry;
   GtkCheckButton *mSecureToggle;
   GtkCheckButton *mAutoConnect;

   Util::string mServer;
   short unsigned int mPort;
   bool mSecure;

   FreezeState mFreezeState;
};


} // namespace cdk


#endif // BROKER_DLG_HH
