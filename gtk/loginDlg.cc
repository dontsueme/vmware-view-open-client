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
 * loginDlg.cc --
 *
 *    Login control.
 */


#include "loginDlg.hh"
#include "util.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::LoginDlg --
 *
 *      Constructor.  Assemble the login widgets in the table.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LoginDlg::LoginDlg()
   : Dlg(),
     mTable(GTK_TABLE(gtk_table_new(4, 2, false))),
     mUsername(GTK_ENTRY(gtk_entry_new())),
     mPasswd(GTK_ENTRY(gtk_entry_new())),
     mDomain(GTK_COMBO_BOX(gtk_combo_box_new_text()))
{
   GtkLabel *l;

   Init(GTK_WIDGET(mTable));
   gtk_container_set_border_width(GTK_CONTAINER(mTable), VM_SPACING);
   gtk_table_set_row_spacings(mTable, VM_SPACING);
   gtk_table_set_col_spacings(mTable, VM_SPACING);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(_("_Username:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 0, 1, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mUsername));

   gtk_widget_show(GTK_WIDGET(mUsername));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mUsername), 1, 2, 0, 1);
   gtk_entry_set_activates_default(mUsername, true);
   AddRequiredEntry(mUsername);
   g_signal_connect(G_OBJECT(mUsername), "changed",
                    G_CALLBACK(OnUsernameChanged), this);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(_("_Password:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 1, 2, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mPasswd));

   gtk_widget_show(GTK_WIDGET(mPasswd));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mPasswd), 1, 2, 1, 2);
   gtk_entry_set_visibility(mPasswd, false);
   // See http://technet.microsoft.com/en-us/library/cc512606.aspx
   gtk_entry_set_max_length(mPasswd, 127);
   AddSensitiveWidget(GTK_WIDGET(mPasswd));
   gtk_entry_set_activates_default(mPasswd, true);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(_("_Domain:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_table_attach(mTable, GTK_WIDGET(l), 0, 1, 2, 3, GTK_FILL, GTK_FILL,
                    0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mDomain));

   gtk_widget_show(GTK_WIDGET(mDomain));
   gtk_table_attach_defaults(mTable, GTK_WIDGET(mDomain), 1, 2, 2, 3);
   AddSensitiveWidget(GTK_WIDGET(mDomain));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::LoginDlg --
 *
 *      Protected constructor for subclasses. Calls the Dlg() constructor
 *      and sets member values as given.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

LoginDlg::LoginDlg(GtkTable *table,
                   GtkEntry *username,
                   GtkEntry *passwd,
                   GtkComboBox *domain,
                   bool userReadOnly)
   : Dlg(),
     mTable(table),
     mUsername(username),
     mPasswd(passwd),
     mDomain(domain),
     mUserReadOnly(userReadOnly)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::SetFields --
 *
 *      Sets the widgets of the dialog with username, password, and domain
 *      values as necessary. Username field may be read-only. Sets focus
 *      to the first widget whose value wasn't supplied.
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
LoginDlg::SetFields(Util::string user,                 // IN
                    bool userReadOnly,                 // IN
                    const char *password,              // IN
                    std::vector<Util::string> domains, // IN
                    Util::string domain)               // IN
{
   gtk_entry_set_text(mUsername, user.c_str());
   mUserReadOnly = userReadOnly;
   SetSensitive(IsSensitive());

   gtk_entry_set_text(mPasswd, password);

   unsigned int selectIndex = 0;
   unsigned int i;
   for (i = 0; i < domains.size(); i++) {
      // Add domains from the list; remember domain to select.
      gtk_combo_box_insert_text(mDomain, i, domains[i].c_str());
      if (domains[i] == domain) {
         selectIndex = i;
      }
   }
   // There must be something in the combo if we incremented i; select it.
   if (i > 0) {
      gtk_combo_box_set_active(mDomain, selectIndex);
   }

   SetFocusWidget(GTK_WIDGET(user.empty() ? mUsername : mPasswd));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::ClearAndFocusPassword --
 *
 *      Clears and focuses the password entry, so the user can try again.
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
LoginDlg::ClearAndFocusPassword()
{
   gtk_entry_set_text(mPasswd, "");
   SetFocusWidget(GTK_WIDGET(mPasswd));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::GetDomain --
 *
 *      Accessor for the domain combo-entry.
 *
 * Results:
 *      The entered/selected domain, or "" if no element of the combo is active.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
LoginDlg::GetDomain()
   const
{
   return Util::GetComboBoxText(mDomain);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::SetSensitive --
 *
 *      Overrides Dlg::SetSensitive() since we need to handle
 *      mUsername's sensitivity ourself according to the most recent
 *      SetFields() call.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Widgets are sensitive - or not
 *
 *-----------------------------------------------------------------------------
 */

void
LoginDlg::SetSensitive(bool sensitive) // IN
{
   Dlg::SetSensitive(sensitive);
   gtk_widget_set_sensitive(GTK_WIDGET(mUsername),
                            !mUserReadOnly && IsSensitive());
   gtk_widget_set_sensitive(GTK_WIDGET(mDomain),
                            GetUsername().find("@") == Util::string::npos &&
                            IsSensitive());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::OnUsernameChanged --
 *
 *      When the username is changed, recalculate whether the domain
 *      field is sensitive.
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
LoginDlg::OnUsernameChanged(GtkEntry *entry,   // IN/UNUSED
                            gpointer userData) // IN
{
   LoginDlg *that = reinterpret_cast<LoginDlg*>(userData);
   ASSERT(that);
   that->SetSensitive(that->IsSensitive());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::LoginDlg::IsValid --
 *
 *      Determines whether or not the login dialog's user name field contains
 *      one of our reserved user names.
 *
 * Results:
 *      true IFF user name is not a reserved name AND our parent says the
 *      name is valid too.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
LoginDlg::IsValid()
{
   const size_t clientMacPrefixLen = sizeof(CLIENT_MAC) - 1;
   const size_t clientMacAddrLen = sizeof(CLIENT_MAC "00:00:00:00:00:00") - 1;

   if (GetUsername().empty()) {
      return false;
   }

   bool valid = true;
   char *username = g_strstrip(g_strdup(GetUsername().c_str()));

   if (!g_strncasecmp(username, CLIENT_CUSTOM, sizeof(CLIENT_CUSTOM) - 1)) {
      valid = false;
   } else if (!g_strncasecmp(username, CLIENT_MAC, clientMacPrefixLen) &&
              strlen(username) == clientMacAddrLen) {

      // filters a MAC address w/any combination of ':' or '_' seperators.
      const char *s = username + clientMacPrefixLen;
      bool isMac = true;
      for (int i = 0; isMac && s[i]; ++i) {
         isMac = (i % 3 == 2) ? (s[i] == ':' || s[i] == '_') : isxdigit(s[i]);
      }
      valid = !isMac;
   }
   g_free(username);

   return valid && Dlg::IsValid();
}


} // namespace cdk
