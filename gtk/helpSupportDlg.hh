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
 * helpSupportDlg.hh --
 *
 *    Help and support dialog.
 */

#ifndef HELP_SUPPORT_DLG_HH
#define HELP_SUPPORT_DLG_HH


#include <gtk/gtk.h>


#include "util.hh"


extern "C" {
#include "productState.h"
}


namespace cdk {


class HelpSupportDlg
{
public:
   HelpSupportDlg();
   virtual ~HelpSupportDlg();

   virtual void Run();

   void SetParent(GtkWindow *window) { mParent = window; }
   void SetHelpContext(Util::string context) { mHelpContext = context; }
   void SetSupportFile(Util::string path) { mSupportFile = path; }
   void SetBrokerHostName(Util::string hostname) { mBrokerHostName = hostname; }

protected:
   virtual GtkWidget *CreateHelpTab();
   virtual void InsertHelpText();

   virtual GtkWidget *CreateSupportTab();
   virtual void CreateProductInformationSection(GtkTable *table);
   virtual void CreateHostInformationSection(GtkTable *table);
   virtual void CreateConnectionInformationSection(GtkTable *table);

   GtkLabel *CreateLabel(const Util::string &text);
   guint AppendRow(GtkTable *table);
   void AddTitle(GtkTable *table, const Util::string &text);
   void AddPair(GtkTable *table, const Util::string &label,
                const Util::string &data);
   void AddWidget(GtkTable *table, GtkWidget *widget);
   GtkTextView *GetSupportView(const Util::string &supportFilePath);

   virtual bool ShowLogLocation() { return true; }
   virtual Util::string GetVersionString() const
      { return ProductState_GetVersion(); }

   GtkDialog *GetDialog() { return mDialog; }
   GtkTextView *GetTextView() { return mHelpTextView; }
   GtkWindow *GetParent() { return mParent; }
   Util::string GetHelpContext() { return mHelpContext; }
   Util::string GetSupportFile() { return mSupportFile; }
   Util::string GetBrokerHostName() { return mBrokerHostName; }

private:
   Util::string ReadHelpFile();
   bool GetHelpContents(const char *directory,
                        const char *locale,
                        char **helpText);

   GtkDialog *mDialog;
   GtkTextView *mHelpTextView;
   GtkWindow *mParent;
   Util::string mHelpContext;
   Util::string mSupportFile;
   Util::string mBrokerHostName;
};


} // namespace cdk


#endif // HELP_SUPPORT_DLG_HH
