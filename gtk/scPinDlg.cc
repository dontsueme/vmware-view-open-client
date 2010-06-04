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
 * scPinDlg.cc --
 *
 *      Prompts the user for a smart card PIN.
 */


#include "scCertDetailsDlg.hh"
#include "scPinDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScPinDlg::ScPinDlg --
 *
 *      ScPinDlg constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ScPinDlg::ScPinDlg()
   : mLabel(GTK_LABEL(gtk_label_new(""))),
     mPin(GTK_ENTRY(gtk_entry_new())),
     mX509(NULL)
{
   GtkVBox *vbox = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   Init(GTK_WIDGET(vbox));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), VM_SPACING);

   gtk_widget_show(GTK_WIDGET(mLabel));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(mLabel), false, true, 0);
   gtk_misc_set_alignment(GTK_MISC(mLabel), 0.0, 0.5);

   GtkHBox *hbox = GTK_HBOX(gtk_hbox_new(false, VM_SPACING));
   gtk_widget_show(GTK_WIDGET(hbox));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hbox), false, false, 0);

   GtkLabel *l = GTK_LABEL(gtk_label_new_with_mnemonic(_("_PIN:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(l), false, false, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mPin));

   gtk_widget_show(GTK_WIDGET(mPin));
   gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(mPin), true, true, 0);
   gtk_entry_set_visibility(mPin, false);
   AddSensitiveWidget(GTK_WIDGET(mPin));
   gtk_entry_set_activates_default(mPin, true);
   AddRequiredEntry(mPin);
   SetFocusWidget(GTK_WIDGET(mPin));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScPinDlg::SetTokenName --
 *
 *      Update the label to contain the name of the token.
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
ScPinDlg::SetTokenName(const Util::string &tokenName) // IN
{
   gtk_label_set_text(mLabel,
                      Util::Format(_("A PIN is required to log in using the "
                                     "token named %s."),
                                   tokenName.c_str()).c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScPinDlg::SetCertificate --
 *
 *      Set the certificate that we are getting a PIN for.  
 *
 * Results:
 *      None
 *
 * Side effects:
 *      If x509 is non-NULL, the View Certificate button is enabled.
 *
 *-----------------------------------------------------------------------------
 */

void
ScPinDlg::SetCertificate(const X509 *x509) // IN
{
   mX509 = x509;
   enableViewCert(x509 != NULL);
}


} // namespace cdk
