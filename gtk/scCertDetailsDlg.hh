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
 * scCertDetailsDlg.hh --
 *
 *      Display the details of a certificate.
 */

#ifndef SC_CERT_DETAILS_DLG_HH
#define SC_CERT_DETAILS_DLG_HH


#include <gtk/gtk.h>
#include <openssl/x509.h>


#include "util.hh"


namespace cdk {


class ScCertDetailsDlg
{
public:
   ScCertDetailsDlg(GtkWindow *parent, const X509 *x509);
private:
   ~ScCertDetailsDlg();
   static void OnDestroy(GtkObject *object, gpointer userData);
   static guint AppendRow(GtkTable *table);
   enum {
      SELECTABLE = 1 << 0,
      USE_MARKUP = 1 << 1
   };
   static void AppendLabel(GtkTable *table, const char *label, int flags);
   static void AppendPair(GtkTable *table, const char *label1,
                          const char *label2);
   static void AppendNameEntry(GtkTable *table, X509_NAME_ENTRY *entry);

   static const char *GetNidName(int nid);

   static Util::string GetAsn1Time(ASN1_GENERALIZEDTIME *tm);

   GtkWidget *mDialog;
};


} // namespace cdk


#endif // SC_CERT_DETAILS_DLG_HH
