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
 * brokerDlg.cc --
 *
 *    Broker selection dialog.
 */

#include "brokerDlg.hh"
#include "prefs.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::BrokerDlg --
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

BrokerDlg::BrokerDlg(Util::string initialBroker) // IN/OPT
   : Dlg(),
     mTable(GTK_TABLE(gtk_table_new(7, 2, false))),
     mBroker(GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text())),
     mPortLabel(GTK_LABEL(gtk_label_new_with_mnemonic(_("_Port:")))),
     mPortEntry(GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 65535, 1))),
     mSecureToggle(GTK_CHECK_BUTTON(gtk_check_button_new_with_mnemonic(
                              _("_Use secure connection (SSL)")))),
     mAutoConnect(GTK_CHECK_BUTTON(gtk_check_button_new_with_mnemonic(
                                      _("_Always connect to this server at "
                                        "startup")))),
     mPort(443),
     mSecure(true),
     mFreezeState(FREEZE_NOTHING)
{
   GtkLabel *l;

   Init(GTK_WIDGET(mTable));
   gtk_container_set_border_width(GTK_CONTAINER(mTable), VM_SPACING);
   gtk_table_set_row_spacings(mTable, VM_SPACING);
   gtk_table_set_col_spacings(mTable, VM_SPACING);

   l = GTK_LABEL(gtk_label_new(_("Enter the host name or IP address of the "
                                 "View Connection Server.")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 2, 0, 1,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(_("A_ddress:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 1, 2,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mBroker));

   gtk_widget_show(GTK_WIDGET(mBroker));
   gtk_table_attach(mTable, GTK_WIDGET(mBroker), 1, 2, 1, 2,
                    (GtkAttachOptions)(GTK_FILL | GTK_EXPAND),
                    (GtkAttachOptions)0, 0, 0);
   g_signal_connect(G_OBJECT(mBroker), "changed",
                    G_CALLBACK(&BrokerDlg::OnBrokerChanged), this);
   // Child of a ComboBoxEntry is the entry, which has the "activate" signal.
   GtkEntry *entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(mBroker)));
   ASSERT(entry);
   gtk_entry_set_activates_default(entry, true);
   // http(s) 5 + :// 3 + hostname (255) + : 1 + port 5 = 269
   gtk_entry_set_max_length(entry, 269);
   SetFocusWidget(GTK_WIDGET(mBroker));
   AddSensitiveWidget(GTK_WIDGET(mBroker));

   GtkExpander *expander =
      GTK_EXPANDER(gtk_expander_new_with_mnemonic(_("_Options")));
   gtk_widget_show(GTK_WIDGET(expander));
   gtk_table_attach(mTable, GTK_WIDGET(expander), 0, 2, 2, 3,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);
   gtk_expander_set_expanded(expander, true);
   g_signal_connect(expander, "notify::expanded",
                    G_CALLBACK(OnOptionsExpanded), this);

   gtk_widget_show(GTK_WIDGET(mPortLabel));
   gtk_table_attach(mTable, GTK_WIDGET(mPortLabel), 0, 1, 3, 4,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(mPortLabel), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(mPortLabel, GTK_WIDGET(mPortEntry));

   // The spinner is packed in an hbox to make it align left, but not expand.
   GtkHBox *hbox = GTK_HBOX(gtk_hbox_new(false, 0));
   gtk_widget_show(GTK_WIDGET(hbox));
   gtk_table_attach(mTable, GTK_WIDGET(hbox), 1, 2, 3, 4,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);

   gtk_widget_show(GTK_WIDGET(mPortEntry));
   gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(mPortEntry), false, false, 0);
   AddSensitiveWidget(GTK_WIDGET(mPortEntry));
   gtk_spin_button_set_digits(mPortEntry, 0);
   gtk_spin_button_set_numeric(mPortEntry, true);
   gtk_spin_button_set_value(mPortEntry, mPort);
   g_signal_connect(mPortEntry, "value-changed", G_CALLBACK(OnPortChanged),
                    this);

   gtk_widget_show(GTK_WIDGET(mSecureToggle));
   gtk_table_attach(mTable, GTK_WIDGET(mSecureToggle), 1, 2, 4, 5,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);
   AddSensitiveWidget(GTK_WIDGET(mSecureToggle));
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mSecureToggle), mSecure);
   g_signal_connect(mSecureToggle, "toggled", G_CALLBACK(OnSecureChanged),
                    this);

   gtk_widget_show(GTK_WIDGET(mAutoConnect));
   gtk_table_attach(mTable, GTK_WIDGET(mAutoConnect), 1, 2, 5, 6,
                    (GtkAttachOptions)GTK_FILL, (GtkAttachOptions)0, 0, 0);
   AddSensitiveWidget(GTK_WIDGET(mAutoConnect));
   g_signal_connect(mAutoConnect, "toggled", G_CALLBACK(OnAutoChanged), NULL);

   gtk_expander_set_expanded(expander,
                             Prefs::GetPrefs()->GetDefaultShowBrokerOptions());

   if (!initialBroker.empty()) {
      gtk_combo_box_append_text(GTK_COMBO_BOX(mBroker), initialBroker.c_str());
   }

   // Load the broker MRU list from preferences
   std::vector<Util::string> brokerMru = Prefs::GetPrefs()->GetBrokerMRU();
   for (std::vector<Util::string>::iterator i = brokerMru.begin();
        i != brokerMru.end(); i++) {
      // Don't add passed-in broker twice.
      if (*i != initialBroker) {
         gtk_combo_box_append_text(GTK_COMBO_BOX(mBroker), (*i).c_str());
      }
   }

   // Select the first entry if we added any.
   if (brokerMru.size() > 0 || !initialBroker.empty()) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(mBroker), 0);
   }

   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mAutoConnect),
                                Prefs::GetPrefs()->GetAutoConnect());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::ParseBroker --
 *
 *      Parses the broker combo-entry.
 *
 * Results:
 *      The broker, port, and secure state are stored in mServer, mPort,
 *      and mSecure respectively.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::ParseBroker()
{
   bool secure;
   unsigned short port;
   mServer = Util::ParseHostLabel(Util::GetComboBoxEntryText(mBroker), &port,
				  &secure);
   if (!mServer.empty()) {
      // Don't change these unless the server was successfully parsed.
      mPort = port;
      mSecure = secure;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::UpdateUI --
 *
 *      Update the UI based on the state stored in this object.
 *
 *      Use freezeState when calling from an event handler.  This lets
 *      the event handlers avoid responding to changes made by this
 *      function, as well as not updating something that was just
 *      changed (we don't want to set the entry's text while the user
 *      is typing).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      UI shows correct host, port, and protocol.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::UpdateUI(FreezeState freezeState) // IN
{
   ASSERT(mFreezeState == FREEZE_NOTHING);
   mFreezeState = freezeState;

   if (mFreezeState != FREEZE_BROKER) {
      GtkWidget *w = gtk_bin_get_child(GTK_BIN(mBroker));
      ASSERT(GTK_IS_ENTRY(w));
      Util::string broker = Util::GetHostLabel(mServer, mPort, mSecure);
      gtk_entry_set_text(GTK_ENTRY(w), broker.c_str());
   }
   if (mFreezeState != FREEZE_PORT) {
      gtk_spin_button_set_value(mPortEntry, mPort);
   }
   if (mFreezeState != FREEZE_SECURE) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mSecureToggle), mSecure);
   }

   mFreezeState = FREEZE_NOTHING;
   UpdateForwardButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnBrokerChanged --
 *
 *      Callback for combo-entry changed signal.
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
BrokerDlg::OnBrokerChanged(GtkComboBox *combo, // IN/UNUSED
                           gpointer userData)  // IN
{
   BrokerDlg *that = reinterpret_cast<BrokerDlg*>(userData);
   ASSERT(that);
   if (that->mFreezeState == FREEZE_NOTHING) {
      that->ParseBroker();
      that->UpdateUI(FREEZE_BROKER);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnPortChanged --
 *
 *      Callback for spinner changed signal.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Port may be changed.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::OnPortChanged(GtkSpinButton *spin, // IN
                         gpointer userData)   // IN
{
   BrokerDlg *that = reinterpret_cast<BrokerDlg*>(userData);
   ASSERT(that);
   if (that->mFreezeState == FREEZE_NOTHING) {
      that->mPort = gtk_spin_button_get_value_as_int(spin);
      that->UpdateUI(FREEZE_PORT);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnSecureChanged --
 *
 *      Callback for toggle changed signal.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Protocol may be changed.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::OnSecureChanged(GtkToggleButton *toggle, // IN
                           gpointer userData)       // IN
{
   BrokerDlg *that = reinterpret_cast<BrokerDlg*>(userData);
   ASSERT(that);
   if (that->mFreezeState == FREEZE_NOTHING) {
      that->mSecure = gtk_toggle_button_get_active(toggle);
      // Negate mSecure here, to get the old value.
      if (that->mPort == (!that->mSecure ? 443 : 80)) {
         that->mPort = that->mSecure ? 443 : 80;
      }
      that->UpdateUI(FREEZE_SECURE);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnAutoChanged --
 *
 *      Callback for the autoconnect's toggled handler.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Autoconnect pref is updated.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::OnAutoChanged(GtkToggleButton *toggle, // IN
                         gpointer userData)       // IN/UNUSED
{
   Prefs::GetPrefs()->SetAutoConnect(
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::OnOptionsExpanded --
 *
 *      Callback when options expander is toggled.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Advanced options are shown or hidden.
 *
 *-----------------------------------------------------------------------------
 */

void
BrokerDlg::OnOptionsExpanded(GObject *expander,     // IN
                             GParamSpec *paramSpec, // IN/UNUSED
                             gpointer userData)     // IN
{
   BrokerDlg *that = reinterpret_cast<BrokerDlg*>(userData);
   ASSERT(that);

   bool expanded = gtk_expander_get_expanded(GTK_EXPANDER(expander));
   if (expanded) {
      gtk_widget_show(GTK_WIDGET(that->mPortLabel));
      // mPortEntry is in an hbox.
      gtk_widget_show(GTK_WIDGET(that->mPortEntry)->parent);
      gtk_widget_show(GTK_WIDGET(that->mSecureToggle));
      gtk_widget_show(GTK_WIDGET(that->mAutoConnect));
   } else {
      gtk_widget_hide(GTK_WIDGET(that->mPortLabel));
      // mPortEntry is in an hbox.
      gtk_widget_hide(GTK_WIDGET(that->mPortEntry)->parent);
      gtk_widget_hide(GTK_WIDGET(that->mSecureToggle));
      gtk_widget_hide(GTK_WIDGET(that->mAutoConnect));
   }
   /* If we don't kill the spacing, we get a huge blank space when the
    * widgets are hidden (that we must of course bring back when they
    * are shown).
    */
   guint spacing = expanded ? VM_SPACING : 0;
   gtk_table_set_row_spacing(that->mTable, 2, spacing);
   gtk_table_set_row_spacing(that->mTable, 3, spacing);
   gtk_table_set_row_spacing(that->mTable, 4, spacing);

   Prefs::GetPrefs()->SetDefaultShowBrokerOptions(expanded);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::IsValid --
 *
 *      Determines whether the forward button should be enabled.
 *
 * Results:
 *      true if the forward button should be enabled.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
BrokerDlg::IsValid()
{
   return !mServer.empty() && Dlg::IsValid();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BrokerDlg::SavePrefs --
 *
 *      Updates the Prefs file with choices made by user.
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
BrokerDlg::SavePrefs()
{
   Util::string text = Util::GetComboBoxEntryText(mBroker);

   if (!text.empty()) {
      Prefs::GetPrefs()->AddBrokerMRU(text);
   }
}


} // namespace cdk
