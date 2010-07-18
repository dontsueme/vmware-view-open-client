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
 * desktopDlg.cc --
 *
 *    Implementation of DesktopDlg.
 */


#include <boost/bind.hpp>
#include <gtk/gtkmain.h>
#include <stdlib.h>


#include "desktopDlg.hh"
#include "prefs.hh"


// Here to avoid conflict with vm_basic_types.h
#include <gdk/gdkx.h>


#define GRAB_RETRY_TIMEOUT_MS 250
#define SLED_10_SP2_PATCHLEVEL 2
#define PATCHLEVEL_STR "PATCHLEVEL = "
#define PATCHLEVEL_LEN strlen(PATCHLEVEL_STR)
#define CTRL_ALT_MASK (GDK_CONTROL_MASK | GDK_MOD1_MASK)

#define VMW_EXEC_CTRL_ALT_DEL   "_VMW_EXEC_CTRL_ALT_DEL"
#define VMW_PROMPT_CTRL_ALT_DEL "_VMW_PROMPT_CTRL_ALT_DEL"

#define LOOKUP_KEYVAL_LR(key) LookupKeyval(key##_L, key##_R)


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::DesktopDlg --
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

DesktopDlg::DesktopDlg(ProcHelper *procHelper, bool allowWMBindings)
   : mSocket(GTK_SOCKET(gtk_socket_new())),
     mGrabTimeoutId(0),
     mHasConnected(false),
     mIgnoreNextLeaveNotify(false),
     mInhibitCtrlEnter(false),
     mHandlingCtrlAltDel(false),
     mAllowWMBindings(allowWMBindings),
     mResizable(false),
     mInitialWidth(0),
     mInitialHeight(0)
{
   ASSERT(procHelper);

   Init(GTK_WIDGET(mSocket));
   SetFocusWidget(GTK_WIDGET(mSocket));

   // Avoid a grey->black transition while rdesktop is starting
   GtkStyle *style = gtk_widget_get_style(GTK_WIDGET(mSocket));
   ASSERT(style);
   gtk_widget_modify_bg(GTK_WIDGET(mSocket), GTK_STATE_NORMAL, &style->black);

   /*
    * Set the socket "hidden" initially.  We don't want this widget to
    * resize our window before it sets itself as "fullscreen"; see bug
    * #329941.
    */
   gtk_widget_set_size_request(GTK_WIDGET(mSocket), 0, 0);

   g_signal_connect_after(G_OBJECT(mSocket), "plug_added",
                          G_CALLBACK(OnPlugAdded), this);
   g_signal_connect_after(G_OBJECT(mSocket), "plug_removed",
                          G_CALLBACK(OnPlugRemoved), this);

   g_signal_connect(G_OBJECT(mSocket), "key-press-event",
                    G_CALLBACK(OnKeyPress), this);

   if (!mAllowWMBindings) {
      g_signal_connect(mSocket, "focus-in-event", G_CALLBACK(UpdateGrab), this);
      g_signal_connect(mSocket, "focus-out-event", G_CALLBACK(UpdateGrab), this);
      g_signal_connect(mSocket, "enter-notify-event", G_CALLBACK(UpdateGrab),
                       this);
      g_signal_connect(mSocket, "leave-notify-event", G_CALLBACK(UpdateGrab),
                       this);
   }

   g_object_add_weak_pointer(G_OBJECT(mSocket), (gpointer *)&mSocket);

   if (GetDisableMetacityKeybindings()) {
      procHelper->onExit.connect(boost::bind(SetMetacityKeybindingsEnabled, true));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::GetWindowId --
 *
 *      Return the X Window ID of our socket, so that things can embed
 *      themselves in us.
 *
 * Results:
 *      A string representation of the X Window ID.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
DesktopDlg::GetWindowId()
   const
{
   return Util::Format("%d", gtk_socket_get_id(mSocket));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::OnPlugAdded --
 *
 *      Callback for "plug_added" GtkSocket signal, fired when the
 *      remote desktop embeds its window as a child of mSocket.  Calls
 *      KeyboardGrab to grab X keyboard focus, so the window manager
 *      does not capture keys that should be sent to rdesktop.
 *
 *      If KeyboardGrab did not grab input, add a timeout callback to reinvoke
 *      it until successful, every GRAB_RETRY_TIMEOUT_MS.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Keyboard is grabbed.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopDlg::OnPlugAdded(GtkSocket *s,      // IN
                        gpointer userData) // IN: this
{
   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(userData);
   ASSERT(that);

   that->mIgnoreNextLeaveNotify = true;

   that->mHasConnected = true;
   that->onConnect();

   /*
    * This needs to happen after we are realized, and now is as good a
    * time as any, really.
    */
   gtk_widget_add_events(GTK_WIDGET(that->mSocket),
                         GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK
                            | GDK_FOCUS_CHANGE_MASK);

   /*
    * Now that onConnect() is called, the window should be fullscreen,
    * and we should allocate our full size.
    */
   gtk_widget_set_size_request(GTK_WIDGET(s), -1, -1);

   if (that->mInitialWidth && that->mInitialHeight) {
      GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(that->mSocket));
      if (GTK_IS_WINDOW(parent)) {
         gtk_window_resize(GTK_WINDOW(parent), that->mInitialWidth,
                           that->mInitialHeight);
         gtk_widget_size_allocate(GTK_WIDGET(that->mSocket),
                                  &parent->allocation);
      }
   }

   if (that->mGrabTimeoutId > 0) {
      g_source_remove(that->mGrabTimeoutId);
      that->mGrabTimeoutId = 0;
   }

   gdk_window_add_filter(GTK_WIDGET(that->mSocket)->window,
                         DesktopDlg::PromptCtrlAltDelHandler, that);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::OnPlugRemoved --
 *
 *      Callback for "plug_removed" GtkSocket signal, fired when the rdesktop's
 *      embedded window disappears. Ungrabs X keyboard focus.
 *
 * Results:
 *      false to let other handlers run.  The default destroys the socket.
 *
 * Side effects:
 *      Keyboard is ungrabbed.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopDlg::OnPlugRemoved(GtkSocket *s,      // IN
                          gpointer userData) // IN: this
{
   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(userData);
   ASSERT(that);
   KeyboardUngrab(that);
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::KeyboardGrab --
 *
 *      Timeout callback to call gdk_keyboard_grab for rdesktop's GtkSocket.
 *      Specify false for owner_events so that hooked keys on the root window
 *      are not forwarded (e.g. SuSE's Computer menu).  The GtkSocket will
 *      receive all key events and forward them on to the embedded rdesktop
 *      window.
 *
 * Results:
 *      true if grabbing failed (reinvoke callback), false if successful.
 *
 * Side effects:
 *      Keyboard may be grabbed.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopDlg::KeyboardGrab(gpointer userData) // IN: this
{
   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(userData);
   ASSERT(that);

   if (that->mAllowWMBindings) {
      return false;
   }

   GdkGrabStatus res = gdk_keyboard_grab(GTK_WIDGET(that->mSocket)->window,
                                         false, GDK_CURRENT_TIME);
   if (res == GDK_GRAB_SUCCESS) {
      if (that->mGrabTimeoutId != 0) {
         Log("Keyboard grab retry success.\n");
         that->mGrabTimeoutId = 0;
      }
      if (that->GetDisableMetacityKeybindings()) {
         SetMetacityKeybindingsEnabled(false);
      }
      return false; // success
   }
   if (that->mGrabTimeoutId == 0) {
      Log("Keyboard grab failed, reason %#x; will retry every %u ms.\n",
          res, GRAB_RETRY_TIMEOUT_MS);
      that->mGrabTimeoutId =
         g_timeout_add(GRAB_RETRY_TIMEOUT_MS, KeyboardGrab, that);
   }
   return true; // retry
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::KeyboardUngrab --
 *
 *      Ungrab the keyboard, or remove our ungrab timeout.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Keyboard is ungrabbed.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopDlg::KeyboardUngrab(gpointer userData) // IN
{
   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(userData);
   ASSERT(that);

   if (that->mAllowWMBindings) {
      /* nothing... */;
   } else if (that->mGrabTimeoutId > 0) {
      g_source_remove(that->mGrabTimeoutId);
      that->mGrabTimeoutId = 0;
   } else {
      gdk_keyboard_ungrab(GDK_CURRENT_TIME);
      if (that->GetDisableMetacityKeybindings()) {
         SetMetacityKeybindingsEnabled(true);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::SendKeyEvent --
 *
 *      Send a key event to the rdesktop window using XSendEvent.
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
DesktopDlg::SendKeyEvent(int type,  // IN
                 unsigned int key)  // IN
{
   ASSERT(mSocket);
   ASSERT(mSocket->plug_window);

   // Based on gtksocket-x11.c:_gtk_socket_windowing_send_key_event().
   GdkScreen *screen = gdk_drawable_get_screen(mSocket->plug_window);
   XKeyEvent xkey;

   memset(&xkey, 0, sizeof(xkey));
   xkey.window = GDK_WINDOW_XWINDOW(mSocket->plug_window);
   xkey.root = GDK_WINDOW_XWINDOW(gdk_screen_get_root_window(screen));
   xkey.subwindow = None;
   xkey.time = 0;
   xkey.x = 0;
   xkey.y = 0;
   xkey.x_root = 0;
   xkey.y_root = 0;
   xkey.same_screen = True;/* FIXME ? */

   // rdesktop doesn't send this to Windows, so it doesn't matter.
   xkey.state = 0;

   xkey.type = type;
   xkey.keycode = key;
   XSendEvent(GDK_WINDOW_XDISPLAY(mSocket->plug_window),
              GDK_WINDOW_XWINDOW(mSocket->plug_window),
              False,
              KeyPressMask,
              (XEvent *)&xkey);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::Press --
 *
 *      Send a KeyPress event to the rdesktop window.
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
DesktopDlg::Press(unsigned int key) // IN
{
   SendKeyEvent(KeyPress, key);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::Release --
 *
 *      Send a KeyRelease event to the rdesktop window.
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
DesktopDlg::Release(unsigned int key) // IN
{
   SendKeyEvent(KeyRelease, key);
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::SendCtrlAltDel --
 *
 *      Send a Control-Alt-Delete key sequence to the rdesktop window.
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
DesktopDlg::SendCtrlAltDel()
{
   // X needs keycodes, Gdk only provides keyvals.
   guint control = LOOKUP_KEYVAL_LR(GDK_Control);
   guint alt = LOOKUP_KEYVAL_LR(GDK_Alt);
   guint del = LookupKeyval(GDK_Delete);

   if (!control || !alt || !del) {
      return;
   }

   Log("Synthesizing Ctrl-Alt-Del keypresses.\n");
   gdk_error_trap_push();

   Press(control);
   Press(alt);
   Press(del);

   Release(del);
   Release(alt);
   Release(control);

   GdkScreen *screen = gdk_drawable_get_screen(mSocket->plug_window);
   gdk_display_sync(gdk_screen_get_display(screen));
   gdk_error_trap_pop();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::ReleaseCtrlAlt --
 *
 *      Send a Control-Alt key >release< sequence to the rdesktop window.
 *
 * Results:
 *      Nonde
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopDlg::ReleaseCtrlAlt()
{
   // X needs keycodes, Gdk only provides keyvals.
   guint control = LOOKUP_KEYVAL_LR(GDK_Control);
   guint alt = LOOKUP_KEYVAL_LR(GDK_Alt);

   if (!control || !alt) {
      return;
   }

   Log("Synthesizing Ctrl-Alt key releases.\n");
   gdk_error_trap_push();

   Release(alt);
   Release(control);

   GdkScreen *screen = gdk_drawable_get_screen(mSocket->plug_window);
   gdk_display_sync(gdk_screen_get_display(screen));
   gdk_error_trap_pop();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::LookupKeyval --
 *
 *      Lookup the keycode for a given Gdk keyval.
 *
 * Results:
 *      Keycode corresponding to keyval or 0 if unable to lookup the key.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

guint
DesktopDlg::LookupKeyval(guint keyval,   // IN
                         guint fallback) // IN/OPT
{
   GdkKeymapKey *keys;
   int n_keys;
   if (!gdk_keymap_get_entries_for_keyval(NULL, keyval, &keys, &n_keys) &&
       (fallback == GDK_VoidSymbol ||
          !gdk_keymap_get_entries_for_keyval(NULL, fallback, &keys, &n_keys))) {
      Log("Unable to lookup key %s.\n", gdk_keyval_name(fallback));
      return 0;
   }

   ASSERT(n_keys > 0);
   guint ret = keys[0].keycode;
   g_free(keys);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::OnKeyPress --
 *
 *      Handle keypress events on the GtkSocket window.  Passes through all
 *      events except Ctrl-Alt-Enter, which will cause rdesktop to crash.
 *
 * Results:
 *      true to stop other handlers from being invoked for the event.
 *      false to propogate the event further.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopDlg::OnKeyPress(GtkWidget *widget, // IN
                       GdkEventKey *evt,  // IN
                       gpointer userData) // IN
{
   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(userData);
   ASSERT(that);

   switch (evt->keyval) {
   case GDK_Return:
      /*
       * NOTE: rdesktop checks for Ctrl_L/R and Alt_L/R non-exclusively, so we
       * match this behavior here. Unfortunately, this means we'll inhibit more
       * events than we would prefer.
       */
      if (that->mInhibitCtrlEnter &&
          (evt->state & CTRL_ALT_MASK) == CTRL_ALT_MASK) {
         Util::UserWarning(_("Inhibiting Ctrl-Alt-Enter keypress, to avoid "
                             "rdesktop exit.\n"));
         return true;
      }
      break;
   case GDK_Delete:
      if ((evt->state & CTRL_ALT_MASK) == CTRL_ALT_MASK) {
         // Ignore additional presses.
         if (!that->mHandlingCtrlAltDel) {
            that->mHandlingCtrlAltDel = true;
            /*
             * "Cancel" the ctrl & alt key presses which have already been
             * sent to rdesktop before delete was pressed. (Otherwise, the
             * remote ctrl/alt keys will appear latched down to the remote
             * desktop, because the corresponding key releases are not sent.)
             */
            that->ReleaseCtrlAlt();
            bool handled = that->onCtrlAltDel();
            // If the dialog disconneceted, mSocket will be NULL.
            if (that->mSocket) {
               // Make sure we re-grab the keyboard.
               KeyboardGrab(that);
            }
            if (handled) {
               Log("Ctrl-Alt-Delete was handled externally; inhibiting.\n");
            } else if (that->mSendCADXMessage) {
               that->SendCADXMessage();
            } else {
               that->SendCtrlAltDel();
            }
            that->mHandlingCtrlAltDel = false;
         }
         return true;
      }
      break;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::UpdateGrab --
 *
 *      Callback for FocusIn, FocusOut, EnterNotify, and LeaveNotify
 *      events.  Decide whether we should grab or ungrab the keyboard.
 *      This is similar to what rdesktop would do were it full screen
 *      and grabbing.
 *
 * Results:
 *      false to continue processing event.
 *
 * Side effects:
 *      Keyboard may be grabbed or ungrabbed.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopDlg::UpdateGrab(GtkWidget *widget, // IN
                       GdkEvent *event,   // IN
                       gpointer userData) // IN
{
   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(userData);
   ASSERT(that);

   if (!that->mHasConnected) {
      return false;
   }

   if (!that->mSocket || !that->mSocket->plug_window) {
      KeyboardUngrab(that);
      return false;
   }

   bool grab = false;
   bool ungrab = false;

   switch (event->type) {
   case GDK_ENTER_NOTIFY: {
      GtkWidget *top = gtk_widget_get_toplevel(widget);
      grab = gtk_window_has_toplevel_focus(GTK_WINDOW(top));
      /*
       * In certain circumstances it is possible for us to not receive the extra
       * LEAVE notification mIgnoreNextLeaveNotify is meant to filter out, e.g.,
       * when the view window is realized and the pointer is outside of our
       * window frame. If that occurs and we then get a valid ENTER notification
       * mIgnoreNextLeaveNotify being true will cause us to skip the next (valid)
       * LEAVE, resulting in the keyboard not being ungrabbed when the user leaves
       * our window.  And that can manifest as "focus stealing", as the app they
       * give focus to won't receive keypresses.  So, whenever we have a valid
       * ENTER notification, ensure we don't ignore its subsequent LEAVE.
       */
      that->mIgnoreNextLeaveNotify = false;
      break;
   }
   case GDK_LEAVE_NOTIFY:
      /*
       * We get this the first time we get our plug added, but we
       * don't want to ungrab quite yet, so just ignore this.
       */
      if (that->mIgnoreNextLeaveNotify) {
         that->mIgnoreNextLeaveNotify = false;
      } else {
         ungrab = true;
      }
      break;
   case GDK_FOCUS_CHANGE:
      /*
       * Only grab the keyboard if we're getting focus AND the mouse is in our
       * Window: this tends to provide a more intuitive user experience in cases
       * where we get focus indirectly due to Window Manager behavior, e.g.,
       * WM desktop switching, etc., as opposed to when we get focus directly,
       * due to the user clicking in/on our window.  If we don't grab we still
       * get keypresses but WM hot key sequences will still be effective. If
       * we're losing focus, ungrab the keyboard so WM keybindings will function
       * as the user expects. One side benefit of all of this is the user will
       * get the native Ctrl-Alt-Del dialog when we lose focus and the view
       * Ctrl-Alt-Del dialog when we gain focus, which is the UX I think users
       * will be less confused by, IMHO.
       */
      if (event->focus_change.in) {
         grab = gdk_window_at_pointer(NULL, NULL) == that->mSocket->plug_window;
      } else {
         /*
          * Always ungrab when we lose focus, otherwise apps selected for focus
          * by the WM during desktop switches will not get keypresses.
          */
         ungrab = true;

         /*
          * Whenever we lose focus, reset any meta-keys which could have been
          * pressed when we lost focus but were not yet released.  This prevents
          * use of local WM keybindings from leaving the remote desktop "thinking"
          * a meta key as still pressed, which can totally foul up keyboard input,
          * e.g., the alt keypress has latched the app's menu bar waiting for a
          * short cut key to be pressed, or the ctrl key appears pressed and the
          * app is waiting for one of the very few keys is has short cuts for
          * (ctrl-f for find), etc.
          */
         that->ClearMetaKeys();
      }
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }
   // We should not be both grabbing and ungrabbing here!
   ASSERT(!grab || !ungrab);
   if (grab) {
      KeyboardGrab(that);
   } else if (ungrab) {
      KeyboardUngrab(that);
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::SetMetacityKeybindingsEnabled --
 *
 *      Disable or enable Metacity's keybindings using the
 *      "metacity-message" tool.
 *
 *      This is due to metacity-dnd-swithing.diff in SLED 10 SP2
 *      (metacity-2.12.3-0.15).  This patch makes metacity use the XKB
 *      for some of its bindings, and it is difficult to prevent
 *      metacity from handling them while rdesktop is running.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Keybindings may be dis/enabled.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopDlg::SetMetacityKeybindingsEnabled(bool enabled) // IN
{
   Log("%s Metacity keybindings using metacity-message.\n",
       enabled ? "Enabling" : "Disabling");
   ProcHelper *mmsg = new ProcHelper();
   mmsg->onExit.connect(boost::bind(OnMetacityMessageExit, mmsg));
   std::vector<Util::string> args;
   args.push_back(enabled ? "enable-keybindings" : "disable-keybindings");
   mmsg->Start("metacity-message", "metacity-message", args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::OnMetacityMessageExit --
 *
 *      Exit handler for Metacity message ProcHelper: delete the
 *      helper.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Helper is deleted.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopDlg::OnMetacityMessageExit(ProcHelper *helper) // IN
{
   delete helper;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::GetDisableMetacityKeybindings --
 *
 *      Determines whether this system's Metacity is likely to be
 *      broken and needs its keybindings disabled manually.
 *
 * Results:
 *      true if Metacity has a broken keybindings patch.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
DesktopDlg::GetDisableMetacityKeybindings()
{
   static bool checked = false;
   static bool ret = false;
   if (!checked) {
      checked = true;
      if (Prefs::GetPrefs()->GetDisableMetacityKeybindingWorkaround() ||
          mAllowWMBindings) {
         // Save result in the static var for future calls.
         ret = false;
         return ret;
      }
      char *contents = NULL;
      if (g_file_get_contents("/etc/SuSE-release", &contents, NULL, NULL) &&
          strstr(contents, "SUSE Linux Enterprise Desktop 10")) {

         char *relStr = strstr(contents, PATCHLEVEL_STR);
         if (relStr) {
            uint32 rel = strtoul(relStr + PATCHLEVEL_LEN, NULL, 10);
            ret = rel == SLED_10_SP2_PATCHLEVEL;
         }
         if (ret) {
            Util::UserWarning(_("Metacity keybindings will be temporarily "
                                "disabled on SLED 10 SP2.\n"));
         }
      }
      g_free(contents);
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::SendCADXMessage --
 *
 *      Send Request Ctrl-Alt-Del message to our plugin for execution.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Remote desktop should execute a native CAD operation.
 *
 *-----------------------------------------------------------------------------
 */
void
DesktopDlg::SendCADXMessage()
{
   gdk_error_trap_push();

   Display *display = GDK_WINDOW_XDISPLAY(mSocket->plug_window);

   XClientMessageEvent xclient = { 0 };
   xclient.type = ClientMessage;
   /*
    * Set window param to the socket window so container can verify
    * that this event is coming from a friendly source.
    */
   xclient.window = GDK_WINDOW_XWINDOW(GTK_WIDGET(mSocket)->window);
   xclient.message_type = XInternAtom(display, VMW_EXEC_CTRL_ALT_DEL, false);
   xclient.format = 32;

   XSendEvent(display, GDK_WINDOW_XWINDOW(mSocket->plug_window),
              false, 0, (XEvent *)&xclient);

   int err = gdk_error_trap_pop();
   if (err != 0) {
      Log("DesktopDlg::SendCADXMessage generated gdk error: %d\n", err);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::PromptCtrlAltDelHandler --
 *
 *    X event handler for displaying our standard ctrl-alt-del dialog
 *    and sending the result back to the effective remote desktop as
 *    another X event.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Locks out processing of additional prompt ctrl alt del events
 *      until the one being processed is complete (prevents stacking
 *      of the CAD dialog and perhaps other unwanted effects.)
 *
 *-----------------------------------------------------------------------------
 */

GdkFilterReturn
DesktopDlg::PromptCtrlAltDelHandler(GdkXEvent *xevent, // IN/UNUSED
                                    GdkEvent *event,   // IN/OUT/UNUSED
                                    gpointer data)     // IN
{
   XClientMessageEvent *cmevent = (XClientMessageEvent *)xevent;

   if (cmevent->type != ClientMessage) {
      return GDK_FILTER_CONTINUE;
   }

   DesktopDlg *that = reinterpret_cast<DesktopDlg *>(data);
   ASSERT(that);

   Display *display = GDK_WINDOW_XDISPLAY(that->mSocket->plug_window);
   if (cmevent->message_type !=
       XInternAtom(display, VMW_PROMPT_CTRL_ALT_DEL, FALSE)) {
      return GDK_FILTER_CONTINUE;
   }

   if (!that->mHandlingCtrlAltDel) {
      that->mHandlingCtrlAltDel = true;
      if (!that->onCtrlAltDel()) {
         // User wants a CAD generated at the remote desktop.
         ASSERT(that->mSendCADXMessage);
         that->SendCADXMessage();
      }
      that->mHandlingCtrlAltDel = false;
   }

   return GDK_FILTER_REMOVE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDlg::ClearMetaKeys --
 *
 *    Ensure meta-keys are not pressed: forcibly release them.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Remote desktop meta-keys are pressed and released: should be
 *      innocuous at worst and at best prevent the keys from appearing
 *      latched down to the remote desktop.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopDlg::ClearMetaKeys()
{
   /*
    * To fully disengage the ctrl and alt keys, they must be pressed and
    * and released in the following order, so that any app on the guest
    * with focus will not have its menu bar or control action stuck on.
    */
   guint control = LOOKUP_KEYVAL_LR(GDK_Control);
   guint alt = LOOKUP_KEYVAL_LR(GDK_Alt);

   if (!control || !alt) {
      return;
   }

   Press(control);
   Press(alt);
   Release(alt);
   Release(control);
}


} // namespace cdk
