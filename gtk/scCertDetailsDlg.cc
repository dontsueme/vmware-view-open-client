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
 * scCertDetailsDlg.cc --
 *
 *      Display the details of a certificate.
 */


#include <gtk/gtk.h>
#include <time.h>


#include "scCertDetailsDlg.hh"


#define TIME_BUFFER_LEN 256


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::ScCertDetailsDlg --
 *
 *      Constructor: create and show the dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A new dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

ScCertDetailsDlg::ScCertDetailsDlg(GtkWindow *parent, // IN
                                   const X509 *x509)  // IN

   : mDialog(gtk_dialog_new_with_buttons(gtk_window_get_title(parent), parent,
                                         GTK_DIALOG_NO_SEPARATOR,
                                         GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                         NULL))
{
   g_signal_connect(mDialog, "destroy", G_CALLBACK(OnDestroy), this);
   g_signal_connect(mDialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);

   GtkTable *table = GTK_TABLE(gtk_table_new(1, 2, false));
   gtk_widget_show(GTK_WIDGET(table));
   gtk_box_pack_start(GTK_BOX(GTK_DIALOG(mDialog)->vbox), GTK_WIDGET(table),
                      false, false, 0);
   gtk_table_set_row_spacings(table, VM_SPACING);
   // Don't set col spacings as we use x-padding for some of the rows.
   gtk_container_set_border_width(GTK_CONTAINER(table), VM_SPACING);

   /* A SHA-1 fingerprint is 160 bits = 20 bytes.  We print 2 hex
    * chars plus a space or NULL per byte, so we will need at least 60
    * bytes here.
    */
   GString *strBuf = g_string_sized_new(60);

   /*
    * XXX: This section needs a couple of fixes before it can go in:
    *
    * 1. We need to trust its issuer; right now we don't verify server
    * certs, so it's a little fishy here.
    *
    * 2. Need to figure out what these usages actually are.
    *
    * The issued to/by etc. are probably useful enough on their own to
    * go in without waiting for these other bits.
    */
#if 0
   AppendLabel(table, _("<b>This certificate has been verified for the following "
                  "uses:</b>"), USE_MARKUP);

   AppendLabel(table, _("SSL Client Certificate"), SELECTABLE);
   AppendLabel(table, _("SSL Server Certificate"), SELECTABLE);
   AppendLabel(table, _("Status Responder Certificate"), SELECTABLE);

   GtkWidget *sep = gtk_hseparator_new();
   gtk_widget_show(sep);
   gtk_table_resize(table, ++row, 2);
   gtk_table_attach(table, sep, 0, 2, row - 1, row,
                   (GtkAttachOptions)0, (GtkAttachOptions)0, 0, 0);
#endif

   X509_NAME *name = X509_get_subject_name((X509 *)x509);
   AppendLabel(table, _("<b>Issued To</b>"), USE_MARKUP);
   for (int i = X509_NAME_entry_count(name) - 1; i >= 0; i--) {
      AppendNameEntry(table, X509_NAME_get_entry(name, i));
   }

   ASN1_INTEGER *serial = x509->cert_info->serialNumber;
   g_string_truncate(strBuf, 0);
   for (int i = 0; i < serial->length; i++) {
      g_string_append_printf(strBuf, "%02X%c", serial->data[i],
                             i + 1 == serial->length ? '\0' : ' ');
   }
   AppendPair(table, _("Serial Number:"), strBuf->str);

   name = X509_get_issuer_name((X509 *)x509);
   AppendLabel(table, _("<b>Issued By</b>"), USE_MARKUP);
   for (int i = X509_NAME_entry_count(name) - 1; i >= 0; i--) {
      AppendNameEntry(table, X509_NAME_get_entry(name, i));
   }

   AppendLabel(table, _("<b>Validity</b>"), USE_MARKUP);

   AppendPair(table, _("Not Valid Before:"),
              GetAsn1Time(x509->cert_info->validity->notBefore).c_str());

   AppendPair(table, _("Not Valid After:"),
              GetAsn1Time(x509->cert_info->validity->notAfter).c_str());

   AppendLabel(table, _("<b>Fingerprints</b>"), USE_MARKUP);

   unsigned int n;
   unsigned char md[EVP_MAX_MD_SIZE];
   X509_digest(x509, EVP_sha1(), md, &n);
   g_string_truncate(strBuf, 0);
   for (unsigned int i = 0; i < n; i++) {
      g_string_append_printf(strBuf, "%02X%c", md[i], i + 1 == n ? '\0' : ' ');
   }
   AppendPair(table, _("SHA1 Fingerprint:"), strBuf->str);

   X509_digest(x509, EVP_md5(), md, &n);
   g_string_truncate(strBuf, 0);
   for (unsigned int i = 0; i < n; i++) {
      g_string_append_printf(strBuf, "%02X%c", md[i], i + 1 == n ? '\0' : ' ');
   }
   AppendPair(table, _("MD5 Fingerprint:"), strBuf->str);

   gtk_widget_show(mDialog);

   g_string_free(strBuf, true);
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::GetAsn1Time --
 *      Converts an ASN1 time into a localized string.
 *
 * Results:
 *    The date.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
ScCertDetailsDlg::GetAsn1Time(ASN1_GENERALIZEDTIME *tm) // IN
{
   char timeBuf[TIME_BUFFER_LEN];
   char *v;
   int gmt = 0;
   int i;
   int y = 0;
   int M = 0;
   int d = 0;
   int h = 0;
   int m = 0;
   int s = 0;

   i = tm->length;
   v = (char *)tm->data;

   if (tm->type == V_ASN1_UTCTIME) {
      // This mess copied verbatim from ASN1_UTCTIME_print in t_x509.c
      if (i < 10) goto err;
      if (v[i-1] == 'Z') gmt=1;
      for (i=0; i<10; i++)
         if ((v[i] > '9') || (v[i] < '0')) goto err;
      y= (v[0]-'0')*10+(v[1]-'0');
      if (y < 50) y+=100;
      M= (v[2]-'0')*10+(v[3]-'0');
      if ((M > 12) || (M < 1)) goto err;
      d= (v[4]-'0')*10+(v[5]-'0');
      h= (v[6]-'0')*10+(v[7]-'0');
      m=  (v[8]-'0')*10+(v[9]-'0');
      if (	(v[10] >= '0') && (v[10] <= '9') &&
            (v[11] >= '0') && (v[11] <= '9'))
         s=  (v[10]-'0')*10+(v[11]-'0');

   } else if (tm->type == V_ASN1_GENERALIZEDTIME) {
      // This mess copied verbatim from ASN1_GENERALIZEDTIME_print in t_x509.c
      if (i < 12) goto err;
      if (v[i-1] == 'Z') gmt=1;
      for (i=0; i<12; i++)
         if ((v[i] > '9') || (v[i] < '0')) goto err;
      y= (v[0]-'0')*1000+(v[1]-'0')*100 + (v[2]-'0')*10+(v[3]-'0');
      M= (v[4]-'0')*10+(v[5]-'0');
      if ((M > 12) || (M < 1)) goto err;
      d= (v[6]-'0')*10+(v[7]-'0');
      h= (v[8]-'0')*10+(v[9]-'0');
      m=  (v[10]-'0')*10+(v[11]-'0');
      if (	(v[12] >= '0') && (v[12] <= '9') &&
            (v[13] >= '0') && (v[13] <= '9'))
         s=  (v[12]-'0')*10+(v[13]-'0');

      y -= 1900;
   }

   struct tm myTime;
   myTime.tm_sec = s;
   myTime.tm_min = m;
   myTime.tm_hour = h;
   myTime.tm_mday = d;
   myTime.tm_mon = M - 1;
   myTime.tm_year = y;
   myTime.tm_isdst = -1;
   mktime(&myTime);

   if(strftime(timeBuf, TIME_BUFFER_LEN, "%c", &myTime) == 0) {
      Warning("Error converting time to string: Buffer too small.\n");
      return "";
   }

   return timeBuf;
err:
   Warning("Error parsing ASN1_TIME %s\n", v);
   return "";
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::~ScCertDetailsDlg --
 *
 *      Destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ScCertDetailsDlg::~ScCertDetailsDlg()
{
   ASSERT(!mDialog);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::OnDestroy --
 *
 *      Signal handler for when our dialog is destroyed.  delete our
 *      C++ object.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      that is deleted.
 *
 *-----------------------------------------------------------------------------
 */

void
ScCertDetailsDlg::OnDestroy(GtkObject *object, // IN/UNUSED
                            gpointer userData) // IN
{
   ScCertDetailsDlg *that = reinterpret_cast<ScCertDetailsDlg *>(userData);
   ASSERT(that);
   ASSERT(that->mDialog);
   // Don't use GTK_WIDGET() here as the object has been destroyed.
   ASSERT(that->mDialog == (GtkWidget *)object);
   // Don't try to re-destroy this widget.
   that->mDialog = NULL;
   delete that;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::AppendRow --
 *
 *      Append a row to a table, and return the index of the appended
 *      row.
 *
 * Results:
 *      Index of the new row.
 *
 * Side effects:
 *      table has an extra row.
 *
 *-----------------------------------------------------------------------------
 */

guint
ScCertDetailsDlg::AppendRow(GtkTable *table) // IN
{
   guint rows;
   guint cols;
   g_object_get(table, "n-rows", &rows, "n-columns", &cols, NULL);
   gtk_table_resize(table, rows + 1, cols);
   return rows;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::AppendLabel --
 *
 *      Append a label to a table.
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
ScCertDetailsDlg::AppendLabel(GtkTable *table,   // IN
                              const char *label, // IN
                              int flags)         // IN
{
   GtkWidget *l = gtk_label_new(label);
   gtk_widget_show(l);
   guint row = AppendRow(table);
   gtk_table_attach(table, l, 0, 2, row, row + 1, (GtkAttachOptions)GTK_FILL,
                    (GtkAttachOptions)0, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
   gtk_label_set_selectable(GTK_LABEL(l), flags & SELECTABLE);
   gtk_label_set_use_markup(GTK_LABEL(l), flags & USE_MARKUP);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::AppendPair --
 *
 *      Append two labels to a table.  One is a label, the second is
 *      data.
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
ScCertDetailsDlg::AppendPair(GtkTable *table,    // IN
                             const char *label1, // IN
                             const char *label2) // IN
{
   GtkWidget *l = gtk_label_new(label1);
   gtk_widget_show(l);
   guint row = AppendRow(table);
   gtk_table_attach(table, l, 0, 1, row, row + 1, (GtkAttachOptions)GTK_FILL,
                    (GtkAttachOptions)0, VM_SPACING, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);

   l = gtk_label_new(label2);
   gtk_widget_show(l);
   gtk_table_attach(table, l, 1, 2, row, row + 1,
                    (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions)0, 0, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
   gtk_label_set_selectable(GTK_LABEL(l), true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::AppendNameEntry --
 *
 *      Append an X509 name entry to the table.  This adds both a
 *      label for the NID type in the entry, and a label for the
 *      value.
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
ScCertDetailsDlg::AppendNameEntry(GtkTable *table,        // IN
                                  X509_NAME_ENTRY *entry) // IN
{
   int nid = OBJ_obj2nid(X509_NAME_ENTRY_get_object(entry));
   /*
    * Translators: this is just the format of a label's text; the
    * actual text of the label is translated elsewhere.
    */
   char *label = g_strdup_printf(_("%s:"), GetNidName(nid));

   // These functions return NULL on NULL or -1 inputs.
   ASN1_STRING *cn = X509_NAME_ENTRY_get_data(entry);
   char *val = NULL;
   if (cn) {
      ASN1_STRING_to_UTF8((unsigned char **)&val, cn);
   }

   AppendPair(table, label, val ? val : "");

   g_free(label);
   OPENSSL_free(val);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ScCertDetailsDlg::GetNidName --
 *
 *      Get a localized, human readbable form of the NID name. We fall
 *      back to OpenSSL's built-in names for unknown values, but add
 *      better versions for some.
 *
 * Results:
 *      Name of this NID.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

const char *
ScCertDetailsDlg::GetNidName(int nid) // IN
{
   switch (nid) {
   case NID_commonName:
      return _("Common Name");
   case NID_countryName:
      return _("Country");
   case NID_localityName:
      return _("Locality");
   case NID_stateOrProvinceName:
      return _("State or Province");
   case NID_organizationName:
      return _("Organization");
   case NID_organizationalUnitName:
      return _("Organizational Unit");
   case NID_pkcs9_emailAddress:
      return _("Email Address");
   case NID_givenName:
      return _("Given Name");
   case NID_surname:
      return _("Surname");
   case NID_domainComponent:
      return _("Domain Component");
   default:
      break;
   }
   return _(OBJ_nid2ln(nid));
}


} // namespace cdk
