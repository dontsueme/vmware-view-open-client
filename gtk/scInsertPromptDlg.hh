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
 * scInsertPromptDlg.hh --
 *
 *      Prompt the user to insert a smart card.
 */

#ifndef SC_INSERT_PROMPT_DLG_HH
#define SC_INSERT_PROMPT_DLG_HH


#include <gtk/gtk.h>


#include "cryptoki.hh"
#include "dlg.hh"


namespace cdk {


class ScInsertPromptDlg
   : public Dlg
{
public:
   ScInsertPromptDlg(Cryptoki *cryptoki);
   ~ScInsertPromptDlg();

private:
   static gboolean UpdateLabelAndButton(gpointer data);

   // overrides Dlg::IsValid()
   virtual bool IsValid() { return mCryptoki->GetHasTokens(); }

   GtkLabel *mLabel;
   Cryptoki *mCryptoki;
   guint mTimeout;
};


} // namespace cdk


#endif // SC_INSERT_PROMPT_DLG_HH
