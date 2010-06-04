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
 * dlg.hh --
 *
 *    Base class for client dialogs.
 */

#ifndef DLG_HH
#define DLG_HH


#include <list>
#include <boost/signal.hpp>

#include <gtk/gtkentry.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkbutton.h>


#include "util.hh"


namespace cdk {


class Dlg
{
public:
   Dlg();
   virtual ~Dlg();

   GtkWidget *GetContent() const { return mContent; }
   virtual void SetSensitive(bool sensitive);
   bool GetForwardEnabled() { return IsSensitive() && IsValid(); }
   virtual bool GetForwardVisible() { return true; }
   virtual bool GetHelpVisible() { return true; }
   bool IsSensitive() const { return mSensitive; }
   virtual bool IsValid();
   virtual void SavePrefs() { }
   // The majority of our dlgs are Auth dlgs, so return login by default.
   virtual Util::string GetHelpContext() { return "login"; }

   boost::signal2<void, bool, bool> updateForwardButton;

   void SetCancelable(bool cancelable) { mCancelable = cancelable; }
   bool GetCancelable() const { return mCancelable; }

protected:
   static void UpdateForwardButton(Dlg *that);

   void Init(GtkWidget *widget);
   void SetFocusWidget(GtkWidget *widget);
   virtual void AddSensitiveWidget(GtkWidget *widget)
      { mSensitiveWidgets.push_back(widget); }
   void AddRequiredEntry(GtkEntry *entry);

private:
   static void OnContentHierarchyChanged(GtkWidget *widget,
                                         GtkWidget *oldToplevel,
                                         gpointer userData);
   static void OnTreeViewRealizeGrabFocus(GtkWidget *widget);

   void GrabFocus();

   GtkWidget *mContent;
   GtkWidget *mFocusWidget;
   std::list<GtkEntry *> mRequiredEntries;
   std::list<GtkWidget *> mSensitiveWidgets;
   bool mSensitive;
   bool mCancelable;
};


} // namespace cdk


#endif // DLG_HH
