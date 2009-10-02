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
 * scCertDlg.hh --
 *
 *      A dialog to let the user select a certificate.
 */


#ifndef SC_CERT_DLG_HH
#define SC_CERT_DLG_HH


#include <boost/signal.hpp>
#include <gtk/gtk.h>
#include <list>
#include <openssl/x509.h>


#include "dlg.hh"
#include "util.hh"
#include "certViewer.hh"


namespace cdk {


class ScCertDlg
   : public Dlg,
     public CertViewer
{
public:
   ScCertDlg();
   ~ScCertDlg() { }

   virtual const X509 *GetCertificate() const;
   void SetCertificates(std::list<X509 *> &certs);

   // Overrides Dlg::SetSensitive
   virtual void SetSensitive(bool sensitive);

private:
   enum {
      SUBJECT_COLUMN,
      X509_COLUMN,
      N_COLUMNS
   };
   static void ActivateToplevelDefault(GtkWidget *widget);
   static void OnSelectionChanged(gpointer userData);
   static char *GetCommonName(X509_NAME *name);

   GtkTreeView *mCertList;
};


} // namespace cdk


#endif // SC_CERT_DLG_HH
