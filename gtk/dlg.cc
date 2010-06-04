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
 * dlg.cc --
 *
 *    Base class for client dialogs.
 */


#include <gtk/gtksocket.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktreeview.h>


#include "dlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::Dlg --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Dlg::Dlg()
   : mContent(NULL),
     mFocusWidget(NULL),
     mSensitive(true),
     mCancelable(true)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::~Dlg --
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

Dlg::~Dlg()
{
   if (mContent) {
      gtk_widget_destroy(mContent);
      ASSERT(!mContent);
   }
   // these should be destroyed by the above widget_destroy call
   ASSERT(!mFocusWidget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::SetSensitive --
 *
 *      Sets mContent's parent sensitive or insensitive.
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
Dlg::SetSensitive(bool sensitive) // IN
{
   if (sensitive == mSensitive) {
      return;
   }
   mSensitive = sensitive;
   for (std::list<GtkWidget *>::iterator i = mSensitiveWidgets.begin();
        i != mSensitiveWidgets.end(); ++i) {
      gtk_widget_set_sensitive(*i, sensitive);
   }
   UpdateForwardButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::Init --
 *
 *      Sets the main widget of this dialog. Connects signal handler to the
 *      widget's "parent-set" signal for proper focus grabbing. Can only be
 *      called once.
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
Dlg::Init(GtkWidget *widget) // IN
{
   ASSERT(mContent == NULL);
   mContent = widget;
   g_object_add_weak_pointer(G_OBJECT(mContent), (gpointer *)&mContent);
   g_signal_connect_after(G_OBJECT(mContent), "hierarchy-changed",
                          G_CALLBACK(&Dlg::OnContentHierarchyChanged), this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::SetFocusWidget --
 *
 *      Sets the widget in this dialog that should have focus.
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
Dlg::SetFocusWidget(GtkWidget *widget) // IN
{
   if (mFocusWidget) {
      g_object_remove_weak_pointer(G_OBJECT(mFocusWidget), (gpointer *)&mFocusWidget);
   }
   mFocusWidget = widget;
   if (mFocusWidget) {
      g_object_add_weak_pointer(G_OBJECT(mFocusWidget), (gpointer *)&mFocusWidget);
      GrabFocus();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::OnTreeViewRealizeGrabFocus --
 *
 *      Grab focus on a widget after it's realized, and remove the
 *      signal handler that got us here.
 *
 *      GtkTreeView seems to not grab the focus correctly unless it's
 *      realized.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Widget is focused.
 *
 *-----------------------------------------------------------------------------
 */

void
Dlg::OnTreeViewRealizeGrabFocus(GtkWidget *widget) // IN
{
   gtk_widget_grab_focus(widget);
   g_signal_handlers_disconnect_by_func(
      widget, (gpointer)&Dlg::OnTreeViewRealizeGrabFocus, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::AddRequiredEntry --
 *
 *      Adds a GtkEntry that must have text in it for mForwardButton to be
 *      sensitized.
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
Dlg::AddRequiredEntry(GtkEntry *entry) // IN
{
   mRequiredEntries.push_back(entry);
   g_signal_connect_swapped(G_OBJECT(entry), "changed",
                    G_CALLBACK(&Dlg::UpdateForwardButton), this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::IsValid --
 *
 *      Is this dialog's content valid?  If yes, the continue button
 *      should be enabled.
 *
 *      This implementation simply goes through all the required text
 *      fields, and if any are empty, returns false.
 *
 * Results:
 *      true if the continue button should be enabled.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Dlg::IsValid()
{
   for (std::list<GtkEntry *>::iterator i = mRequiredEntries.begin();
        i != mRequiredEntries.end(); i++) {
      if (!(*gtk_entry_get_text(*i))) {
         return false;
      }
   }
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::UpdateForwardButton --
 *
 *      Callback for changes to any text entry.  Emits the update signal to
 *      indicate a change to the sensitivity of the forward button.
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
Dlg::UpdateForwardButton(Dlg *that) // IN
{
   ASSERT(that);

   that->updateForwardButton(that->IsSensitive() && that->IsValid(),
                             that->GetForwardVisible());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::GrabFocus --
 *
 *      Grabs focus for mFocusWidget, if set. If mFocusWidget's "can-focus"
 *      property is false, sets it true.
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
Dlg::GrabFocus()
{
   if (mFocusWidget) {
      // Some widgets aren't focusable by default, like GtkSocket.
      if (GTK_IS_SOCKET(mFocusWidget) && !GTK_WIDGET_CAN_FOCUS(mFocusWidget)) {
         g_object_set(G_OBJECT(mFocusWidget), "can-focus", true, NULL);
      }
      if (GTK_IS_TREE_VIEW(mFocusWidget) &&
          !GTK_WIDGET_REALIZED(mFocusWidget)) {
         g_signal_connect_after(mFocusWidget, "realize",
                                G_CALLBACK(&Dlg::OnTreeViewRealizeGrabFocus), NULL);
      } else {
         gtk_widget_grab_focus(mFocusWidget);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Dlg::OnContentHierarchyChanged --
 *
 *      Callback for when mContent's toplevel changes. mFocusWidget's
 *      focus won't persist after this change, so calls GrabFocus to
 *      re-assert it.
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
Dlg::OnContentHierarchyChanged(GtkWidget *widget,      // IN
                               GtkWidget *oldToplevel, // IN
                               gpointer userData)      // IN
{
   Dlg *that = reinterpret_cast<Dlg*>(userData);
   ASSERT(that);
   GtkWidget *win = gtk_widget_get_toplevel(widget);
   if (GTK_IS_WINDOW(win)) {
      that->GrabFocus();
   }
}


} // namespace cdk
