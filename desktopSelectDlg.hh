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
   enum Action {
      ACTION_CONNECT,
      ACTION_RESET,
      ACTION_KILL_SESSION,
      ACTION_ROLLBACK
   };

   DesktopSelectDlg(std::vector<Desktop *> &desktops,
                    Util::string initalDesktop = "",
                    bool offerMultiMon = false
#ifdef VIEW_ENABLE_WINDOW_MODE
                    , bool offerWindowSizes = true
#endif // VIEW_ENABLE_WINDOW_MODE
                    );
   ~DesktopSelectDlg();

   void UpdateList(std::vector<Desktop *> &desktops, Util::string select = "");
   Desktop *GetDesktop();
#ifdef VIEW_ENABLE_WINDOW_MODE
   bool GetDesktopSize(GdkRectangle *geometry, bool *useAllMonitors = NULL);
#else
   bool GetUseAllMonitors() const;
#endif // VIEW_ENABLE_WINDOW_MODE
   bool IsResizable() const { return true; }

   boost::signal1<void, Action> action;

private:
   enum ListColumns {
      ICON_COLUMN,
      LABEL_COLUMN,
      NAME_COLUMN,
      DESKTOP_COLUMN,
      BUTTON_COLUMN,
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

   void ShowPopup(GdkEventButton *evt = NULL, bool customPosition = false);
   void KillPopup();
   inline bool PopupVisible()
      { return mPopup && GTK_WIDGET_VISIBLE(GTK_WIDGET(mPopup)); }
   void DestroyPopup();
   GtkTreePath *GetPathForButton(int x, int y);
   void CheckHover(int x, int y);
   void KillHover();

   void ConfirmAction(Action act);

   static void OnConnect(GtkButton *button, gpointer userData);
   static void OnResetDesktop(GtkMenuItem *item, gpointer data);
   static void OnKillSession(GtkMenuItem *item, gpointer data);
   static void OnRollback(GtkMenuItem *item, gpointer data);
   static void OnPopupDeactivate(GtkWidget *widget, gpointer data);
   static void OnPopupDetach(GtkWidget *widget, GtkMenu *popup);
   static gboolean OnPopupSignal(GtkWidget *widget, gpointer data);
   static gboolean OnButtonPress(GtkWidget *widget, GdkEventButton *button,
                                 gpointer data);
   static void PopupPositionFunc(GtkMenu *menu, int *x, int *y,
                                 gboolean *pushIn, gpointer data);
   static void ActivateToplevelDefault(GtkWidget *widget);
   static gboolean OnPointerMove(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer data);
   static gboolean OnPointerLeave(GtkWidget *widget, GdkEventCrossing *event,
                                  gpointer data);
   static void UpdateButton(DesktopSelectDlg *that);
#ifdef VIEW_ENABLE_WINDOW_MODE
   static void UpdateWindowSizes(gpointer data);
#endif // VIEW_ENABLE_WINDOW_MODE

   GtkVBox *mBox;
   GtkTreeView *mDesktopList;
   GtkListStore *mStore;
   GtkButton *mConnect;
#ifdef VIEW_ENABLE_WINDOW_MODE
   GtkComboBox *mWindowSize;
   bool mOfferMultiMon;
   bool mOfferWindowSizes;
#else
   GtkCheckButton *mMultiMon;
#endif // VIEW_ENABLE_WINDOW_MODE
   gboolean mInButtonPress;
   GtkMenu *mPopup;

   // ListView button members.
   GtkTreePath *mButtonPath;
   GtkTreeViewColumn *mButtonColumn;
   // ListView button pixbufs for three states.
   GdkPixbuf *mButtonNormal;
   GdkPixbuf *mButtonHover;
   GdkPixbuf *mButtonOpen;
};


} // namespace cdk


#endif // DESKTOP_SELECT_DLG_HH
