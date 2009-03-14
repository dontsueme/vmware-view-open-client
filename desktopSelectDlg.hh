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
 * desktopSelectDlg.hh --
 *
 *    DesktopSelect selection dialog.
 */

#ifndef DESKTOP_SELECT_DLG_HH
#define DESKTOP_SELECT_DLG_HH


#include <boost/signal.hpp>

extern "C" {
#include <gtk/gtk.h>
}


#include "broker.hh"
#include "desktop.hh"
#include "dlg.hh"
#include "util.hh"


namespace cdk {


class DesktopSelectDlg
   : public Dlg
{
public:
   DesktopSelectDlg(std::vector<Desktop*> desktops,
                    Util::string initalDesktop = ""
#ifdef VIEW_ENABLE_WINDOW_MODE
                    , bool offerWindowSizes = true
#endif // VIEW_ENABLE_WINDOW_MODE
                    );
   ~DesktopSelectDlg() { }

   Desktop *GetDesktop();
#ifdef VIEW_ENABLE_WINDOW_MODE
   bool GetDesktopSize(GdkRectangle *geometry);
#endif // VIEW_ENABLE_WINDOW_MODE
   bool IsResizable() const { return true; }

   boost::signal0<void> connect;

private:
   enum ListColumns {
      ICON_COLUMN,
      NAME_COLUMN,
      DESKTOP_COLUMN,
      N_COLUMNS,
   };

#ifdef VIEW_ENABLE_WINDOW_MODE
   enum WindowSizeColumns {
      SIZE_LABEL_COLUMN,
      HEIGHT_COLUMN,
      WIDTH_COLUMN,
      N_WINDOW_COLUMNS
   };
#endif
   void ShowPopup(GdkEventButton *evt = NULL);

   void OnResetDesktopAbort(bool canceled, Util::exception err);
   void OnResetDesktopDone();

   void OnKillSessionDone();
   void OnKillSessionAbort(bool canceled, Util::exception err);

   static void OnConnect(GtkButton *button, gpointer userData);
   static void OnResetDesktop(GtkMenuItem *item, gpointer data);
   static void OnKillSession(GtkMenuItem *item, gpointer data);
   static void OnPopupDeactivate(GtkWidget *widget, gpointer data);
   static gboolean OnPopupDeactivateIdle(gpointer data);
   static gboolean OnPopupSignal(GtkWidget *widget, gpointer data);
   static gboolean OnPopupEvent(GtkWidget *widget, GdkEventButton *button,
                                gpointer data);
   static void ActivateToplevelDefault(GtkWidget *widget);
#ifdef VIEW_ENABLE_WINDOW_MODE
   static void UpdateWindowSizes(gpointer data);
#endif // VIEW_ENABLE_WINDOW_MODE

   GtkVBox *mBox;
   GtkTreeView *mDesktopList;
   GtkButton *mConnect;
#ifdef VIEW_ENABLE_WINDOW_MODE
   GtkComboBox *mWindowSize;
   bool mOfferWindowSizes;
#endif // VIEW_ENABLE_WINDOW_MODE
   gboolean mInButtonPress;
};


} // namespace cdk


#endif // DESKTOP_SELECT_DLG_HH
