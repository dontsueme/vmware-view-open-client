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
 * securIDDlg.cc --
 *
 *    SecurID authentication dialog.
 */


#include <gtk/gtkbbox.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktable.h>


#include "securIDDlg.hh"
#include "util.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::SecurIDDlg::SecurIDDlg --
 *
 *      Constructor. SetState should be called before showing to put the
 *      dialog in a presentable state.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

SecurIDDlg::SecurIDDlg()
   : Dlg(),
     mLabel(GTK_LABEL(gtk_label_new("XXX"))),
     mFirstLabel(GTK_LABEL(gtk_label_new("XXX"))),
     mFirstEntry(GTK_ENTRY(gtk_entry_new())),
     mSecondLabel(GTK_LABEL(gtk_label_new("XXX"))),
     mSecondEntry(GTK_ENTRY(gtk_entry_new()))
{
   GtkTable *table = GTK_TABLE(gtk_table_new(4, 2, false));
   Init(GTK_WIDGET(table));
   gtk_container_set_border_width(GTK_CONTAINER(table), VM_SPACING);
   gtk_table_set_row_spacings(table, VM_SPACING);
   gtk_table_set_col_spacings(table, VM_SPACING);

   gtk_widget_show(GTK_WIDGET(mLabel));
   gtk_table_attach_defaults(table, GTK_WIDGET(mLabel), 0, 2, 0, 1);

   gtk_widget_show(GTK_WIDGET(mFirstLabel));
   gtk_table_attach(table, GTK_WIDGET(mFirstLabel), 0, 1, 1, 2,
                    GTK_FILL, GTK_FILL, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(mFirstLabel), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(mFirstLabel, GTK_WIDGET(mFirstEntry));

   gtk_widget_show(GTK_WIDGET(mFirstEntry));
   gtk_table_attach_defaults(table, GTK_WIDGET(mFirstEntry), 1, 2, 1, 2);
   AddSensitiveWidget(GTK_WIDGET(mFirstEntry));
   AddRequiredEntry(mFirstEntry);

   gtk_widget_show(GTK_WIDGET(mSecondLabel));
   gtk_table_attach(table, GTK_WIDGET(mSecondLabel), 0, 1, 2, 3,
                    GTK_FILL, GTK_FILL, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(mSecondLabel), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(mSecondLabel, GTK_WIDGET(mSecondEntry));

   gtk_widget_show(GTK_WIDGET(mSecondEntry));
   gtk_table_attach_defaults(table, GTK_WIDGET(mSecondEntry), 1, 2, 2, 3);
   gtk_entry_set_visibility(mSecondEntry, false);
   AddSensitiveWidget(GTK_WIDGET(mSecondEntry));
   AddRequiredEntry(mSecondEntry);

   gtk_entry_set_activates_default(mFirstEntry, true);
   gtk_entry_set_activates_default(mSecondEntry, true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::SecurIDDlg::SetState --
 *
 *      Sets the state of the dialog, either for regular passcode entry,
 *      next-token entry, or PIN change/confirmation. The "first" argument
 *      is the server-provided PIN, if any, for PIN change/confirmation.
 *      Otherwise it is the initial or locked username. The "message"
 *      and "userSelectable" arugments are only used for PIN
 *      change/confirmation.
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
SecurIDDlg::SetState(State state,          // IN
                     Util::string first,   // IN
                     bool userSelectable,  // IN/OPT
                     Util::string message) // IN/OPT
{
   Util::string usernameMsg = _("_Username:");

   gtk_entry_set_text(mFirstEntry, first.c_str());
   userSelectable = userSelectable || first.empty();

   switch(state) {
   case STATE_PASSCODE:
      gtk_label_set_text(mLabel,
                         _("Enter your RSA SecurID user name and passcode."));

      gtk_label_set_text_with_mnemonic(mFirstLabel, usernameMsg.c_str());

      gtk_widget_set_sensitive(GTK_WIDGET(mFirstEntry), userSelectable);
      gtk_entry_set_visibility(mFirstEntry, true);

      gtk_label_set_text_with_mnemonic(mSecondLabel, _("_Passcode:"));

      gtk_entry_set_visibility(mSecondEntry, false);
      gtk_entry_set_text(mSecondEntry, "");

      SetFocusWidget(first.empty() ? GTK_WIDGET(mFirstEntry) :
                     GTK_WIDGET(mSecondEntry));
      break;
   case STATE_NEXT_TOKEN:
      gtk_label_set_text(mLabel, _("Wait until the next tokencode appears on "
                                   "your RSA SecurID token, then enter it."));

      gtk_label_set_text_with_mnemonic(mFirstLabel, usernameMsg.c_str());

      gtk_widget_set_sensitive(GTK_WIDGET(mFirstEntry), false);
      gtk_entry_set_visibility(mFirstEntry, true);

      gtk_label_set_text_with_mnemonic(mSecondLabel, _("_Tokencode:"));

      gtk_entry_set_visibility(mSecondEntry, false);
      gtk_entry_set_text(mSecondEntry, "");

      SetFocusWidget(GTK_WIDGET(mSecondEntry));
      break;
   case STATE_SET_PIN: {
      Util::string labelText;
      if (first.empty()) {
         labelText = _("Enter a new RSA SecurID PIN.");
      } else if (userSelectable) {
         labelText = _("Enter a new RSA SecurID PIN or accept the "
                       "system-generated PIN.");
      } else {
         labelText = _("Accept the system-generated RSA SecurID PIN.");
      }
      if (!message.empty()) {
         labelText += "\n\n" + message;
      }
      gtk_label_set_text(mLabel, labelText.c_str());

      gtk_label_set_text_with_mnemonic(mFirstLabel, _( "_PIN:"));

      gtk_widget_set_sensitive(GTK_WIDGET(mFirstEntry), userSelectable);
      gtk_entry_set_visibility(mFirstEntry, !first.empty());

      gtk_label_set_text_with_mnemonic(mSecondLabel, _("Con_firm PIN:"));

      gtk_entry_set_text(mSecondEntry, "");

      SetFocusWidget(userSelectable ? GTK_WIDGET(mFirstEntry) :
                     GTK_WIDGET(mSecondEntry));
      break;
   }
   default:
      NOT_IMPLEMENTED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::SecurIDDlg::GetPins --
 *
 *      Returns the entered values for both PINs.
 *
 * Results:
 *      Text in mFirstEntry and mSecondEntry.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::pair<const char *, const char *>
SecurIDDlg::GetPins()
   const
{
   return std::pair<const char *, const char *>(
      gtk_entry_get_text(mFirstEntry), gtk_entry_get_text(mSecondEntry));
}


} // namespace cdk
