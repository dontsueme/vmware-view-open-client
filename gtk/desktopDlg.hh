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
 * desktopDlg.hh --
 *
 *    Embeds a remote desktop application.
 *
 */

#ifndef DESKTOP_DLG_HH
#define DESKTOP_DLG_HH


#include <boost/signal.hpp>
#include <gtk/gtk.h>
#include <vector>


#include "dlg.hh"
#include "procHelper.hh"
#include "util.hh"


#include <gdk/gdkkeysyms.h>


namespace cdk {


class DesktopDlg
   : public Dlg
{
public:
   DesktopDlg(ProcHelper *procHelper, bool allowWMBindings);

   bool GetHasConnected() const { return mHasConnected; }

   bool GetInhibitCtrlEnter() const { return mInhibitCtrlEnter; }
   void SetInhibitCtrlEnter(bool inhibit) { mInhibitCtrlEnter = inhibit; }

   bool GetSendCADXMessage() const { return mSendCADXMessage; }
   void SetSendCADXMessage(bool send) { mSendCADXMessage = send; }

   bool GetResizable() const { return mResizable; }
   void SetResizable(bool resizable) { mResizable = resizable; }

   void SetInitialDesktopSize(int width, int height)
      { mInitialWidth = width; mInitialHeight = height; }

   Util::string GetWindowId() const;

   boost::signal0<void> onConnect;
   boost::signal0<bool> onCtrlAltDel;

private:
   void SendCADXMessage();
   static GdkFilterReturn PromptCtrlAltDelHandler(GdkXEvent *xevent,
                                                  GdkEvent *event,
                                                  gpointer data);

   void Press(unsigned int key);
   void Release(unsigned int key);
   void SendKeyEvent(int type, unsigned int key);
   void SendCtrlAltDel();
   void ReleaseCtrlAlt();

   static void OnPlugAdded(GtkSocket *s, gpointer userData);
   static gboolean OnPlugRemoved(GtkSocket *s, gpointer userData);
   static gboolean KeyboardGrab(gpointer userData);
   static void KeyboardUngrab(gpointer userData);
   static gboolean OnKeyPress(GtkWidget *widget, GdkEventKey *evt,
                              gpointer userData);
   static gboolean UpdateGrab(GtkWidget *widget, GdkEvent *evt,
                              gpointer userData);
   static void SetMetacityKeybindingsEnabled(bool enabled);
   static void OnMetacityMessageExit(ProcHelper *helper);
   bool GetDisableMetacityKeybindings();
   void ClearMetaKeys();

   static guint LookupKeyval(guint keyval, guint fallback = GDK_VoidSymbol);

   GtkSocket *mSocket;
   guint mGrabTimeoutId;
   bool mHasConnected;
   bool mIgnoreNextLeaveNotify;
   bool mInhibitCtrlEnter;
   bool mHandlingCtrlAltDel;
   bool mSendCADXMessage;
   bool mAllowWMBindings;
   bool mResizable;
   int mInitialWidth;
   int mInitialHeight;
};


} // namespace cdk


#endif // DESKTOP_DLG_HH
