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
 * scPinDlg.hh --
 *
 *      Prompts the user for a smart card PIN.
 */


#ifndef SC_PIN_DLG_HH
#define SC_PIN_DLG_HH


#include <boost/signal.hpp>
#include <gtk/gtk.h>
#include <openssl/x509.h>


#include "dlg.hh"
#include "util.hh"
#include "certViewer.hh"


namespace cdk {


class ScPinDlg
   : public Dlg,
     public CertViewer
{
public:
   ScPinDlg();
   ~ScPinDlg() { }

   virtual const X509 *GetCertificate() const { return mX509; }
   const char *GetPin() const { return gtk_entry_get_text(mPin); }
   void SetTokenName(const Util::string &tokenName);
   void SetCertificate(const X509 *x509);

private:
   GtkLabel *mLabel;
   GtkEntry *mPin;
   const X509 *mX509;
};


} // namespace cdk


#endif // SC_PIN_DLG_HH
