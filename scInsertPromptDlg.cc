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
 * scInsertPromptDlg.cc --
 *
 *      Prompt the user to insert a smart card.
 */

#include "scInsertPromptDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScInsertPromptDlg::ScInsertPromptDlg --
 *
 *      Constructor.  Build the dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ScInsertPromptDlg::ScInsertPromptDlg(Cryptoki *cryptoki) // IN
   : mLabel(GTK_LABEL(gtk_label_new(""))),
     mCryptoki(cryptoki),
     mTimeout(0)
{
   GtkVBox *vbox = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   Init(GTK_WIDGET(vbox));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), VM_SPACING);

   gtk_widget_show(GTK_WIDGET(mLabel));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(mLabel), true, true, VM_SPACING * 5);
   gtk_misc_set_alignment(GTK_MISC(mLabel), 0.5, 0.5);

   GtkButton *next = Util::CreateButton(GTK_STOCK_GO_FORWARD, _("_Next"));
   gtk_widget_show(GTK_WIDGET(next));
   GTK_WIDGET_SET_FLAGS(next, GTK_CAN_DEFAULT);
   SetForwardButton(next);
   g_signal_connect(G_OBJECT(next), "clicked", G_CALLBACK(OnNext), this);

   GtkButton *help = GetHelpButton();
   GtkWidget *actionArea = Util::CreateActionArea(help, next,
                                                  GetCancelButton(), NULL);

   gtk_widget_show(actionArea);
   gtk_box_pack_start(GTK_BOX(vbox), actionArea, false, false, 0);
   gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(actionArea),
                                      GTK_WIDGET(help), true);

   mTimeout = g_timeout_add(1000, UpdateLabelAndButton, this);

   // Call it now to update the UI
   UpdateLabelAndButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScInsertPromptDlg::~ScInsertPromptDlg --
 *
 *      Destructor.  Remove our timeout.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ScInsertPromptDlg::~ScInsertPromptDlg()
{
   g_source_remove(mTimeout);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScInsertPromptDlg::OnNext --
 *
 *      Click handler for next button.  Emit the next signals.
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
ScInsertPromptDlg::OnNext(GtkButton *button, // IN/UNUSED
                          gpointer userData) // IN
{
   ScInsertPromptDlg *that = reinterpret_cast<ScInsertPromptDlg *>(userData);
   ASSERT(that);
   that->next();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScInsertPromptDlg::UpdateLabelAndButton --
 *
 *      Timeout callback.  Check if the user has inserted a token.
 *
 * Results:
 *      true to keep timeout running.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
ScInsertPromptDlg::UpdateLabelAndButton(gpointer userData) // IN
{
   ScInsertPromptDlg *that = reinterpret_cast<ScInsertPromptDlg *>(userData);
   gtk_label_set_label(that->mLabel, that->IsValid()
                       ? _("A smart card has been inserted.")
                       : _("Insert a smart card to continue."));
   UpdateForwardButton(that);
   return true;
}


} // namespace cdk
