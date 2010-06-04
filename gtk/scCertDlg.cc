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
 * scCertDlg.cc --
 *
 *      A dialog to let the user select a certificate.
 */


#include "scCertDetailsDlg.hh"
#include "scCertDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::ScCertDlg --
 *
 *      ScCertDlg constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ScCertDlg::ScCertDlg()
   : mCertList(GTK_TREE_VIEW(gtk_tree_view_new()))
{
   GtkVBox *vbox = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   Init(GTK_WIDGET(vbox));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), VM_SPACING);

   GtkLabel *l =
      GTK_LABEL(gtk_label_new_with_mnemonic(_("Choo_se a certificate:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(l), false, true, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mCertList));

   GtkScrolledWindow *swin =
      GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
   gtk_widget_show(GTK_WIDGET(swin));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(swin), true, true, 0);
   g_object_set(swin, "height-request", 200, NULL);
   gtk_scrolled_window_set_policy(swin, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(swin, GTK_SHADOW_IN);

   gtk_widget_show(GTK_WIDGET(mCertList));
   gtk_container_add(GTK_CONTAINER(swin), GTK_WIDGET(mCertList));
   gtk_tree_view_set_headers_visible(mCertList, false);
   gtk_tree_view_set_reorderable(mCertList, false);
   gtk_tree_view_set_rules_hint(mCertList, true);
   AddSensitiveWidget(GTK_WIDGET(mCertList));
   SetFocusWidget(GTK_WIDGET(mCertList));
   g_signal_connect(G_OBJECT(mCertList), "row-activated",
                    G_CALLBACK(ActivateToplevelDefault), NULL);

   /*
    * On Gtk 2.8, we need to set the columns before we can select a
    * row.  See bugzilla #291580.
    */
   GtkTreeViewColumn *column;
   GtkCellRenderer *renderer;

   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes("XXX", renderer,
                                                     "markup", SUBJECT_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(mCertList, column);

   GtkTreeSelection *sel = gtk_tree_view_get_selection(mCertList);
   gtk_tree_selection_set_mode(sel, GTK_SELECTION_BROWSE);
   g_signal_connect_swapped(G_OBJECT(sel), "changed",
                            G_CALLBACK(OnSelectionChanged), this);

   GtkListStore *store = gtk_list_store_new(N_COLUMNS,
                                            G_TYPE_STRING,   // SUBJECT_COLUMN
                                            G_TYPE_POINTER); // X509_COLUMN
   gtk_tree_view_set_model(mCertList, GTK_TREE_MODEL(store));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::SetCertificates --
 *
 *      Set the certificates this dialog should display.
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
ScCertDlg::SetCertificates(std::list<X509 *> &certs) // IN
{
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(mCertList));
   GtkTreeIter iter;
   for (std::list<X509 *>::iterator i = certs.begin();
        i != certs.end(); i++) {
      char *subject = GetCommonName(X509_get_subject_name(*i));
      char *issuer = GetCommonName(X509_get_issuer_name(*i));

      char *label = g_markup_printf_escaped(
         _("<b>%s</b>\n<span size=\"smaller\">Issued by %s</span>"),
         subject, issuer);

      OPENSSL_free(subject);
      OPENSSL_free(issuer);

      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter,
                         SUBJECT_COLUMN, label,
                         X509_COLUMN, *i,
                         -1);
      g_free(label);

      if (i == certs.begin()) {
         gtk_tree_selection_select_iter(
            gtk_tree_view_get_selection(mCertList), &iter);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::GetCertificate --
 *
 *      Gets the selected certificate.
 *
 * Results:
 *      The X509 certificate that's selected, or NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

const X509 *
ScCertDlg::GetCertificate()
   const
{
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(
          gtk_tree_view_get_selection(mCertList),
          &model,
          &iter)) {
      return NULL;
   }
   X509 *x509;
   gtk_tree_model_get(model, &iter, X509_COLUMN, &x509, -1);
   return x509;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::ActivateToplevelDefault --
 *
 *      Activate the toplevel's default button.
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
cdk::ScCertDlg::ActivateToplevelDefault(GtkWidget *widget) // IN
{
   GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
   if (GTK_IS_WINDOW(toplevel)) {
      gtk_window_activate_default(GTK_WINDOW(toplevel));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::GetCommonName --
 *
 *      Get the common name of the passed-in name, or a suitable
 *      substitute.
 *
 * Results:
 *      A string that should be freed with OPENSSL_free().
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

char *
ScCertDlg::GetCommonName(X509_NAME *name) // IN
{
   int foundIdx = -1;
   int idx;
   while (true) {
      idx = X509_NAME_get_index_by_NID(name, NID_commonName, foundIdx);
      if (idx == -1) {
         break;
      }
      foundIdx = idx;
   }
   // These functions return NULL on NULL or -1 inputs.
   ASN1_STRING *cn =
      X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, foundIdx));

   char *ret = NULL;
   if (!cn || 0 > ASN1_STRING_to_UTF8((unsigned char **)&ret, cn)) {
      ret = X509_NAME_oneline(name, NULL, 0);
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::SetSensitive --
 *
 *      Overrides Dlg::SetSensitive.
 *
 *      Emits the enable signal to indicate a change to the view cert button.
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
ScCertDlg::SetSensitive(bool sensitive) // IN
{
   Dlg::SetSensitive(sensitive);
   enableViewCert(sensitive && GetCertificate());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDlg::OnSelectionChanged --
 *
 *      Callback for selection change - update the view certificate
 *      button.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Button states are updated.
 *
 *-----------------------------------------------------------------------------
 */

void
ScCertDlg::OnSelectionChanged(gpointer userData)
{
   ScCertDlg *that = reinterpret_cast<ScCertDlg *>(userData);
   ASSERT(that);

   that->SetSensitive(that->IsSensitive());
}


} // namespace cdk
