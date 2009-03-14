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
 * desktopSelectDlg.cc --
 *
 *    DesktopSelect selection dialog.
 */


#include <boost/bind.hpp>


#include "app.hh"
#include "desktopSelectDlg.hh"
#include "icons/desktop_remote32x.h"
#include "prefs.hh"
#include "util.hh"


#ifdef VIEW_ENABLE_WINDOW_MODE
// Shorter versions of these defines.
#define ALL_SCREENS Prefs::SIZE_ALL_SCREENS
#define FULL_SCREEN Prefs::SIZE_FULL_SCREEN
#endif


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::DesktopSelectDlg --
 *
 *      Constructor.  Lists the available desktops passed in the constructor in
 *      a ListView, and fires the onConnect signal when the "Connect" button is
 *      clicked.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

DesktopSelectDlg::DesktopSelectDlg(std::vector<Desktop*> desktops, // IN
                                   Util::string initialDesktop     // IN/OPT
#ifdef VIEW_ENABLE_WINDOW_MODE
                                   , bool offerWindowSizes         // IN/OPT
#endif // VIEW_ENABLE_WINDOW_MODE
                                   )
   : Dlg(),
     mBox(GTK_VBOX(gtk_vbox_new(false, VM_SPACING))),
     mDesktopList(GTK_TREE_VIEW(gtk_tree_view_new())),
     mConnect(Util::CreateButton(GTK_STOCK_OK,
        CDK_MSG(connectDesktopSelectDlg, "C_onnect"))),
#ifdef VIEW_ENABLE_WINDOW_MODE
     mWindowSize(NULL),
     mOfferWindowSizes(offerWindowSizes),
#endif // VIEW_ENABLE_WINDOW_MODE
     mInButtonPress(false)
{
   GtkLabel *l;

   Init(GTK_WIDGET(mBox));
   gtk_container_set_border_width(GTK_CONTAINER(mBox), VM_SPACING);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(
      CDK_MSG(availableComputers, "_Available Desktops:").c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_box_pack_start(GTK_BOX(mBox), GTK_WIDGET(l), false, true, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mDesktopList));

   GtkScrolledWindow *swin =
      GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
   gtk_widget_show(GTK_WIDGET(swin));
   gtk_box_pack_start(GTK_BOX(mBox), GTK_WIDGET(swin), true, true, 0);
   g_object_set(swin, "height-request", 100, NULL);
   gtk_scrolled_window_set_policy(swin, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(swin, GTK_SHADOW_IN);

   gtk_widget_show(GTK_WIDGET(mDesktopList));
   gtk_container_add(GTK_CONTAINER(swin), GTK_WIDGET(mDesktopList));
   gtk_tree_view_set_headers_visible(mDesktopList, false);
   gtk_tree_view_set_reorderable(mDesktopList, false);
   gtk_tree_view_set_rules_hint(mDesktopList, true);
   AddSensitiveWidget(GTK_WIDGET(mDesktopList));
   g_signal_connect(G_OBJECT(mDesktopList),
                    "row-activated",
                    G_CALLBACK(&DesktopSelectDlg::ActivateToplevelDefault),
                    NULL);
   g_signal_connect(G_OBJECT(mDesktopList),
                    "popup-menu",
                    G_CALLBACK(&DesktopSelectDlg::OnPopupSignal),
                    this);
   g_signal_connect(G_OBJECT(mDesktopList),
                    "button-press-event",
                    G_CALLBACK(&DesktopSelectDlg::OnPopupEvent),
                    this);

   SetFocusWidget(GTK_WIDGET(mDesktopList));

   /*
    * On Gtk 2.8, we need to set the columns before we can select a
    * row.  See bugzilla #291580.
    */
   GtkTreeViewColumn *column;
   GtkCellRenderer *renderer;

   renderer = gtk_cell_renderer_pixbuf_new();
   column = gtk_tree_view_column_new_with_attributes("XXX", renderer,
                                                     "pixbuf", ICON_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(mDesktopList, column);

   renderer = gtk_cell_renderer_text_new();
   column = gtk_tree_view_column_new_with_attributes("XXX",
                                                     renderer,
                                                     "markup", NAME_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(mDesktopList, column);

   GtkTreeSelection *sel = gtk_tree_view_get_selection(mDesktopList);
   gtk_tree_selection_set_mode(sel, GTK_SELECTION_BROWSE);

   GtkListStore *store = gtk_list_store_new(N_COLUMNS,
                                            GDK_TYPE_PIXBUF, // ICON_COLUMN
                                            G_TYPE_STRING, // NAME_COLUMN
                                            G_TYPE_POINTER); // DESKTOP_COLUMN
   gtk_tree_view_set_model(mDesktopList, GTK_TREE_MODEL(store));

   GdkPixbuf *pb = gdk_pixbuf_new_from_inline(-1, desktop_remote32x, false,
                                              NULL);
   GtkTreeIter iter;
   for (std::vector<Desktop*>::iterator i = desktops.begin();
        i != desktops.end(); i++) {
      Util::string name = (*i)->GetName();

      char *label = g_markup_printf_escaped(
         "<b>%s</b>\n<span size=\"smaller\">%s</span>",
         name.c_str(),
         (*i)->GetSessionID().empty()
            ? CDK_MSG(desktopHasSession,
                       "Log in to new session").c_str()
            : CDK_MSG(desktopNoSession,
                       "Reconnect to existing session").c_str());

      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter,
                         ICON_COLUMN, pb,
                         NAME_COLUMN, label,
                         DESKTOP_COLUMN, *i,
                         -1);

      g_free(label);

      if (name == initialDesktop || i == desktops.begin()) {
         gtk_tree_selection_select_iter(sel, &iter);
      }
   }
   if (pb) {
      g_object_unref(pb);
   }

#ifdef VIEW_ENABLE_WINDOW_MODE
   if (mOfferWindowSizes) {
      GtkHBox *box = GTK_HBOX(gtk_hbox_new(0, VM_SPACING));
      gtk_widget_show(GTK_WIDGET(box));
      gtk_box_pack_start(GTK_BOX(mBox), GTK_WIDGET(box), false, false, 0);

      mWindowSize = GTK_COMBO_BOX(gtk_combo_box_new());
      gtk_widget_show(GTK_WIDGET(mWindowSize));
      gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(mWindowSize), false, false, 0);

      renderer = gtk_cell_renderer_text_new();
      gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(mWindowSize), renderer, true);
      gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(mWindowSize), renderer,
                                     "text", SIZE_LABEL_COLUMN, NULL);

      GtkListStore *store = gtk_list_store_new(N_WINDOW_COLUMNS,
                                               G_TYPE_STRING, // SIZE_LABEL_COLUMN
                                               G_TYPE_INT,    // HEIGHT_COLUMN
                                               G_TYPE_INT);   // WIDTH_COLUMN
      gtk_combo_box_set_model(mWindowSize, GTK_TREE_MODEL(store));

      g_signal_connect_swapped(mWindowSize, "hierarchy-changed",
                               G_CALLBACK(UpdateWindowSizes), this);
      g_signal_connect_swapped(mWindowSize, "screen-changed",
                               G_CALLBACK(UpdateWindowSizes), this);

      l = GTK_LABEL(gtk_label_new_with_mnemonic("_Display:"));
      gtk_widget_show(GTK_WIDGET(l));
      gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(l), false, false, 0);
      gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mWindowSize));
   }
#endif // VIEW_ENABLE_WINDOW_MODE

   gtk_widget_show(GTK_WIDGET(mConnect));
   GTK_WIDGET_SET_FLAGS(mConnect, GTK_CAN_DEFAULT);
   SetForwardButton(mConnect);
   g_signal_connect(G_OBJECT(mConnect), "clicked",
                    G_CALLBACK(&DesktopSelectDlg::OnConnect), this);

   GtkWidget *actionArea = Util::CreateActionArea(mConnect, GetCancelButton(),
                                                  NULL);
   gtk_widget_show(actionArea);
   gtk_box_pack_start(GTK_BOX(mBox), actionArea, false, true, 0);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetDesktop --
 *
 *      Return the selected Desktop in the ListView.
 *
 * Results:
 *      Currently selected Desktop, or NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Desktop*
DesktopSelectDlg::GetDesktop()
{
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(
          gtk_tree_view_get_selection(mDesktopList),
          &model,
          &iter)) {
      GValue value = { 0 };
      gtk_tree_model_get_value(model, &iter, DESKTOP_COLUMN, &value);
      ASSERT(G_VALUE_HOLDS_POINTER(&value));
      Desktop *ret = reinterpret_cast<Desktop*>(g_value_get_pointer(&value));
      ASSERT(ret);
      return ret;
   }
   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnConnect --
 *
 *      Callback for Connect button click. Emits the connect signal.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      connect signal emitted.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::OnConnect(GtkButton *button, // IN/UNUSED
                            gpointer userData) // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(userData);
   ASSERT(that);

   if (gtk_tree_selection_count_selected_rows(
          gtk_tree_view_get_selection(that->mDesktopList)) > 0) {
#ifdef VIEW_ENABLE_WINDOW_MODE
      GdkRectangle geom;
      that->GetDesktopSize(&geom);
      Prefs::GetPrefs()->SetDefaultDesktopWidth(geom.width);
      Prefs::GetPrefs()->SetDefaultDesktopHeight(geom.height);
#endif // VIEW_ENABLE_WINDOW_MODE
      that->connect();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnKillSession --
 *
 *      User selected the "Logout" menu item; try to kill our session.
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
DesktopSelectDlg::OnKillSession(GtkMenuItem *item, // IN/UNUSED
                                gpointer data)     // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   that->SetSensitive(false);
   that->GetDesktop()->KillSession(
      boost::bind(&DesktopSelectDlg::OnKillSessionAbort, that, _1, _2),
      boost::bind(&DesktopSelectDlg::OnKillSessionDone, that));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnKillSessionAbort --
 *
 *      Display kill-session error message.
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
DesktopSelectDlg::OnKillSessionAbort(bool canceled,       // IN
                                     Util::exception err) // IN
{
   if (!canceled) {
      App::ShowDialog(GTK_MESSAGE_ERROR, "%s", err.what());
   }
   SetSensitive(true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetDesktopSize --
 *
 *      Get the window size the user has selected, and whether they
 *      want full screen.
 *
 * Results:
 *      returns whether full screen mode was selected
 *      geometry is filled with the window size (could be negative)
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef VIEW_ENABLE_WINDOW_MODE
bool
DesktopSelectDlg::GetDesktopSize(GdkRectangle *geometry) // OUT
{
   ASSERT(geometry != NULL);

   if (!mWindowSize) {
      geometry->width = geometry->height = FULL_SCREEN;
      return true;
   }

   GtkTreeIter iter;
   if (gtk_combo_box_get_active_iter(mWindowSize, &iter)) {
      gtk_tree_model_get(gtk_combo_box_get_model(mWindowSize), &iter,
                         WIDTH_COLUMN, &geometry->width,
                         HEIGHT_COLUMN, &geometry->height,
                         -1);
   } else {
      geometry->width = Prefs::GetPrefs()->GetDefaultDesktopWidth();
      geometry->height = Prefs::GetPrefs()->GetDefaultDesktopHeight();
      // Make sure width and height agree, and are allowed by our caller.
      if (geometry->width == ALL_SCREENS || geometry->height == ALL_SCREENS) {
         geometry->width = geometry->height = FULL_SCREEN;
      } else if (!mOfferWindowSizes || geometry->width == FULL_SCREEN ||
                 geometry->height == FULL_SCREEN) {
         geometry->width = geometry->height = FULL_SCREEN;
      }
   }
   return geometry->width == FULL_SCREEN;
}
#endif // VIEW_ENABLE_WINDOW_MODE


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnKillSessionDone --
 *
 *      Success handler for kill-session; refresh the desktop list.
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
DesktopSelectDlg::OnKillSessionDone()
{
#if 0 // disabled until we can reload the desktop list...
   // Show desktop selector
   mBroker->LoadDesktops(boost::bind(&DesktopSelectDlg::OnLoadDesktopsAbort, this, _1, _2),
                         boost::bind(&DesktopSelectDlg::OnLoadDesktopsDone, this));
#else
   SetSensitive(true);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnResetDesktop --
 *
 *      User selected "Restart" menu item; confirm that they wish to
 *      do this, and then start a reset-desktop RPC.
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
DesktopSelectDlg::OnResetDesktop(GtkMenuItem *item, // IN/UNUSED
                                 gpointer data)     // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(that->mDesktopList));
   GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(top),
      GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION,
      GTK_BUTTONS_NONE,
      CDK_MSG(resetDesktopQuestion,
              "Are you sure you want to restart %s?\n\n"
              "Any unsaved data may be lost.").c_str(),
      that->GetDesktop()->GetName().c_str());
   gtk_window_set_title(GTK_WINDOW(dialog),
                        gtk_window_get_title(GTK_WINDOW(top)));
   gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                          CDK_MSG(restartButton, "Restart").c_str(),
                          GTK_RESPONSE_ACCEPT,
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                          NULL);
   if (GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(dialog))) {
      that->SetSensitive(false);
      that->GetDesktop()->ResetDesktop(
         boost::bind(&DesktopSelectDlg::OnResetDesktopAbort, that, _1, _2),
         boost::bind(&DesktopSelectDlg::OnResetDesktopDone, that));
   }
   gtk_widget_destroy(dialog);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnResetDesktopAbort --
 *
 *      Display error from reset-desktop RPC
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
DesktopSelectDlg::OnResetDesktopAbort(bool canceled,       // IN
                                      Util::exception err) // IN
{
   if (!canceled) {
      App::ShowDialog(GTK_MESSAGE_ERROR, "%s", err.what());
   }
   SetSensitive(true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnResetDesktopDone --
 *
 *      Success handler for reset-desktop RPC; refresh the desktop
 *      list.
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
DesktopSelectDlg::OnResetDesktopDone()
{
#if 0 // disabled until we can reload the desktop list...
   // Show desktop selector
   mBroker->LoadDesktops(boost::bind(&DesktopSelectDlg::OnLoadDesktopsAbort, this, _1, _2),
                         boost::bind(&DesktopSelectDlg::OnLoadDesktopsDone, this));
#else
   SetSensitive(true);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::ShowPopup --
 *
 *      Display a context menu for a desktop with "advanced" commands
 *      such as resetting a session, and VMDI options, etc.
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
DesktopSelectDlg::ShowPopup(GdkEventButton *evt) // IN
{
   Desktop *desktop = GetDesktop();
   if (!desktop) {
      return;
   }

   GtkWidget *menu = gtk_menu_new();
   gtk_widget_show(menu);
   gtk_menu_attach_to_widget(GTK_MENU(menu), GTK_WIDGET(mDesktopList), NULL);
   g_signal_connect_after(menu, "deactivate",
                          G_CALLBACK(&DesktopSelectDlg::OnPopupDeactivate),
                          NULL);

   GtkWidget *item = gtk_menu_item_new_with_mnemonic(
      CDK_MSG(connectPopup, "C_onnect").c_str());
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   g_signal_connect(G_OBJECT(item), "activate",
                    G_CALLBACK(&DesktopSelectDlg::OnConnect),
                    this);

   item = gtk_separator_menu_item_new();
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

   item = gtk_menu_item_new_with_mnemonic(
      CDK_MSG(menuLogOff, "_Log Off").c_str());
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   if (!desktop->GetSessionID().empty()) {
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(&DesktopSelectDlg::OnKillSession),
                       this);
   } else {
      gtk_widget_set_sensitive(item, FALSE);
   }

   item = gtk_menu_item_new_with_mnemonic(
      CDK_MSG(menuRestart, "_Restart").c_str());
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   if (desktop->CanReset() && desktop->CanResetSession()) {
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(&DesktopSelectDlg::OnResetDesktop),
                       this);
   } else {
      gtk_widget_set_sensitive(item, FALSE);
   }

   gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                  evt ? evt->button : 0,
                  evt ? evt->time : gtk_get_current_event_time());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnPopupSignal --
 *
 *      Handler for the "popup-menu" signal; display the context menu.
 *
 * Results:
 *      true -> We handled this event.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopSelectDlg::OnPopupSignal(GtkWidget *widget, // IN/UNUSED
                                gpointer data)     // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   that->ShowPopup();
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnPopupEvent --
 *
 *      Handler for "button-press-event" signal; display the context
 *      menu if the correct button is pressed.
 *
 *      For an expletive-laden rant about Gtk's API here, and why this
 *      function is so crazy, see:
 *
 *      http://markmail.org/message/jy6t3uyze2qlsr3q
 *
 * Results:
 *      true -> We handled this event.
 *      false -> Continue propagating event.
 *
 * Side effects:
 *      Menu popped up, sometimes.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopSelectDlg::OnPopupEvent(GtkWidget *widget,   // IN
                               GdkEventButton *evt, // IN
                               gpointer data)       // IN
{
   // The selection is fully updated by now.
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   if (that->mInButtonPress) {
      return false; // We re-entered.
   }

   if (evt->button != 3 || evt->type != GDK_BUTTON_PRESS) {
      return false; // Let the normal handler run.
   }

   that->mInButtonPress = true;
   gboolean handled = gtk_widget_event(widget, (GdkEvent *)evt);
   that->mInButtonPress = false;

   if (!handled) {
      return false;
   }

   that->ShowPopup(evt);
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnPopupDeactivate --
 *
 *      Handler for "deactivate" signal on the menu; it has gone away.
 *      Queue an idle handler to destroy the widget (if we destory it
 *      now, the menu item's "activate" signal will not be emitted).
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
DesktopSelectDlg::OnPopupDeactivate(GtkWidget *widget, // IN/UNUSED
                                    gpointer data)     // IN
{
   g_idle_add(&DesktopSelectDlg::OnPopupDeactivateIdle, widget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnPopupDeactivateIdle --
 *
 *      Destroy the menu.
 *
 * Results:
 *      false -> Remove this source.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopSelectDlg::OnPopupDeactivateIdle(gpointer data) // IN
{
   gtk_widget_destroy(GTK_WIDGET(data));
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDelectDlg::ActivateToplevelDefault --
 *
 *      Callback which activates the default widget on this widget's
 *      window.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Default widget may be activated
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::ActivateToplevelDefault(GtkWidget *widget) // IN
{
   GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
   if (GTK_IS_WINDOW(toplevel)) {
      gtk_window_activate_default(GTK_WINDOW(toplevel));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::UpdateWindowSizes --
 *
 *      Update the list of available resolutions.  This is called on
 *      hierarchy changed (so we have a toplevel, and therefore a
 *      screen) as well as on screen changed (although this is not the
 *      same as monitor change, and in fact we don't update when we
 *      just switch monitors).
 *
 *      Populates the window sizes list with resolutions smaller than
 *      the current monitor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Window sizes list is updated.
 *
 *-----------------------------------------------------------------------------
 */

#ifdef VIEW_ENABLE_WINDOW_MODE
void
DesktopSelectDlg::UpdateWindowSizes(gpointer data) // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(that->mWindowSize));
   if (!GTK_WIDGET_TOPLEVEL(top)) {
      return;
   }

   GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(top));
   ASSERT(screen);
   GdkRectangle screenGeom;
   gdk_screen_get_monitor_geometry(
      screen,
      gdk_screen_get_monitor_at_window(screen, top->window),
      &screenGeom);

   /*
    * This handles both the initial selection, and keeping the current
    * selection if we move screens.
    */
   GdkRectangle geom;
   that->GetDesktopSize(&geom);

   GtkListStore *store =
      GTK_LIST_STORE(gtk_combo_box_get_model(that->mWindowSize));
   gtk_list_store_clear(store);

   GtkTreeIter iter;
#define APPEND_SIZE(s, w, h)                                            \
   if (screenGeom.width > w && screenGeom.height > h) {                 \
      gtk_list_store_append(store, &iter);                              \
      gtk_list_store_set(store, &iter,                                  \
                         SIZE_LABEL_COLUMN, s,                          \
                         HEIGHT_COLUMN, h,                              \
                         WIDTH_COLUMN, w,                               \
                         -1);                                           \
      if (w == geom.width && h == geom.height) {                        \
         gtk_combo_box_set_active_iter(that->mWindowSize, &iter);       \
      }                                                                 \
   }

   APPEND_SIZE("Full Screen", FULL_SCREEN, FULL_SCREEN);
   if (that->mOfferWindowSizes) {
      APPEND_SIZE("640 x 480", 640, 480);
      APPEND_SIZE("800 x 600", 800, 600);
      APPEND_SIZE("1024 x 768", 1024, 768);
      APPEND_SIZE("1280 x 854", 1280, 854);
      APPEND_SIZE("1280 x 1024", 1280, 1024);
      APPEND_SIZE("1440 x 900", 1440, 900);
      APPEND_SIZE("1600 x 1200", 1600, 1200);
      APPEND_SIZE("1680 x 1050", 1680, 1050);
      APPEND_SIZE("1920 x 1200", 1920, 1200);
   }
#undef APPEND_SIZE

   if (gtk_combo_box_get_active(that->mWindowSize) < 0) {
      // Set it to full screen by default!
      gtk_combo_box_set_active(that->mWindowSize, 0);
   }
}
#endif // VIEW_ENABLE_WINDOW_MODE


} // namespace cdk
