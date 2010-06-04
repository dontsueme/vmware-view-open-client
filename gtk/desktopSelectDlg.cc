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
#include "icons/desktop_checkin_32x.h"
#include "icons/desktop_checkin_pause32x.h"
#include "icons/desktop_checkout_32x.h"
#include "icons/desktop_checkout_pause32x.h"
#include "icons/desktop_local_rollback_32x.h"
#include "icons/desktop_local32x.h"
#include "icons/desktop_local32xdisabled.h"
#include "icons/desktop_remote32x.h"
#include "icons/desktop_remote32x_disabled.h"
#include "icons/list_button_normal.h"
#include "icons/list_button_hover.h"
#include "icons/list_button_open.h"
#include "prefs.hh"
#include "protocols.hh"
#include "util.hh"
#include "windowSizeDlg.hh"


#define BUTTON_SIZE 16
#define DIALOG_DATA_KEY "cdk-dialog"

/*
 * The pango version we link to as of view 4.5 is 1.4.1 and it does not define
 * PANGO_ELLIPSIZE_END, so we must define it for ourselves.
 */
#define VIEW_PANGO_ELLIPSIZE_END   3


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::DesktopSelectDlg --
 *
 *      Constructor.  Lists the available desktops passed in the constructor in
 *      a ListView, and fires the onConnect signal when the "Connect" button is
 *      clicked. If initialDesktop is given, selects it.
 *      If offerMultiMon is true, creates and shows the "Use all monitors"
 *      checkbox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

DesktopSelectDlg::DesktopSelectDlg(std::vector<Desktop *> &desktops, // IN
                                   Util::string initialDesktop,      // IN/OPT
                                   bool offerMultiMon,               // IN/OPT
                                   bool offerWindowSizes)            // IN/OPT
   : Dlg(),
     mBox(GTK_VBOX(gtk_vbox_new(false, VM_SPACING))),
     mDesktopList(GTK_TREE_VIEW(gtk_tree_view_new())),
     mStore(gtk_list_store_new(N_COLUMNS,
                               GDK_TYPE_PIXBUF,   // ICON_COLUMN
                               G_TYPE_STRING,     // LABEL_COLUMN
                               G_TYPE_STRING,     // NAME_COLUMN
                               G_TYPE_POINTER,    // DESKTOP_COLUMN
                               GDK_TYPE_PIXBUF)), // BUTTON_COLUMN
     mWindowSize(NULL),
     mInButtonPress(false),
     mPopup(NULL),
     mButtonPath(NULL),
     mIsOffline(false)
{
   GtkLabel *l;

   Init(GTK_WIDGET(mBox));
   gtk_container_set_border_width(GTK_CONTAINER(mBox), VM_SPACING);

   l = GTK_LABEL(gtk_label_new_with_mnemonic(_("_Desktops:")));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_box_pack_start(GTK_BOX(mBox), GTK_WIDGET(l), false, true, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
   gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mDesktopList));

   GtkScrolledWindow *swin =
      GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
   gtk_widget_show(GTK_WIDGET(swin));
   gtk_box_pack_start(GTK_BOX(mBox), GTK_WIDGET(swin), true, true, 0);
   g_object_set(swin, "height-request", 130, NULL);
   gtk_scrolled_window_set_policy(swin, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(swin, GTK_SHADOW_IN);

   gtk_widget_show(GTK_WIDGET(mDesktopList));
   gtk_container_add(GTK_CONTAINER(swin), GTK_WIDGET(mDesktopList));
   gtk_tree_view_set_headers_visible(mDesktopList, false);
   gtk_tree_view_set_reorderable(mDesktopList, false);
   gtk_tree_view_set_rules_hint(mDesktopList, true);
   AddSensitiveWidget(GTK_WIDGET(mDesktopList));
   g_signal_connect(G_OBJECT(mDesktopList), "row-activated",
                    G_CALLBACK(&DesktopSelectDlg::ActivateToplevelDefault),
                    NULL);
   g_signal_connect(G_OBJECT(mDesktopList), "popup-menu",
                    G_CALLBACK(&DesktopSelectDlg::OnPopupSignal), this);
   g_signal_connect(G_OBJECT(mDesktopList), "button-press-event",
                    G_CALLBACK(&DesktopSelectDlg::OnButtonPress), this);
   g_signal_connect(G_OBJECT(mDesktopList), "motion-notify-event",
                    G_CALLBACK(&DesktopSelectDlg::OnPointerMove), this);
   g_signal_connect(G_OBJECT(mDesktopList), "leave-notify-event",
                    G_CALLBACK(&DesktopSelectDlg::OnPointerLeave), this);
   // Widget needs to remember us in OnPopupDetach.
   g_object_set_data(G_OBJECT(mDesktopList), DIALOG_DATA_KEY, this);

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
   if (g_object_class_find_property(
         G_OBJECT_GET_CLASS(G_OBJECT(renderer)), "ellipsize")) {
      g_object_set(G_OBJECT(renderer), "ellipsize",
                   VIEW_PANGO_ELLIPSIZE_END, NULL);
   } else {
      Log("BZ 547730: This GTK version does not support the 'ellipsize' property.\n");
   }
   column = gtk_tree_view_column_new_with_attributes("XXX",
                                                     renderer,
                                                     "markup", LABEL_COLUMN,
                                                     NULL);
   gtk_tree_view_append_column(mDesktopList, column);
   gtk_tree_view_column_set_expand(column, true);
   gtk_tree_view_column_set_resizable(column, true);

   renderer = gtk_cell_renderer_pixbuf_new();
   mButtonColumn =
      gtk_tree_view_column_new_with_attributes("XXX", renderer,
                                               "pixbuf", BUTTON_COLUMN,
                                               NULL);
   gtk_tree_view_append_column(mDesktopList, mButtonColumn);
   gtk_tree_view_column_set_sizing(mButtonColumn,
                                   GTK_TREE_VIEW_COLUMN_FIXED);
   gtk_tree_view_column_set_fixed_width(mButtonColumn,
                                        VM_SPACING * 2 + BUTTON_SIZE);

   mButtonNormal = gdk_pixbuf_new_from_inline(-1, list_button_normal, false,
                                              NULL);
   mButtonHover = gdk_pixbuf_new_from_inline(-1, list_button_hover, false,
                                             NULL);
   mButtonOpen = gdk_pixbuf_new_from_inline(-1, list_button_open, false, NULL);

   GtkTreeSelection *sel = gtk_tree_view_get_selection(mDesktopList);
   gtk_tree_selection_set_mode(sel, GTK_SELECTION_BROWSE);
   g_signal_connect_swapped(G_OBJECT(sel), "changed",
                            G_CALLBACK(&DesktopSelectDlg::UpdateButton), this);

   gtk_tree_view_set_model(mDesktopList, GTK_TREE_MODEL(mStore));

   UpdateList(desktops, initialDesktop);

   if (offerWindowSizes || offerMultiMon) {
      GtkHBox *box = GTK_HBOX(gtk_hbox_new(0, VM_SPACING));
      gtk_widget_show(GTK_WIDGET(box));
      gtk_box_pack_start(GTK_BOX(mBox), GTK_WIDGET(box), false, false, 0);

      mWindowSize = GTK_COMBO_BOX(gtk_combo_box_new());
      gtk_widget_show(GTK_WIDGET(mWindowSize));
      gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(mWindowSize), false, false, 0);
      g_signal_connect(G_OBJECT(mWindowSize), "changed",
                       G_CALLBACK(OnSizeChanged), this);

      renderer = gtk_cell_renderer_text_new();
      gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(mWindowSize), renderer, true);
      gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(mWindowSize), renderer,
                                     "text", SIZE_LABEL_COLUMN, NULL);

      GtkListStore *store = gtk_list_store_new(N_WINDOW_COLUMNS,
                                               G_TYPE_STRING, // SIZE_LABEL_COLUMN
                                               G_TYPE_INT);   // SIZE_VALUE_COLUMN
      gtk_combo_box_set_model(mWindowSize, GTK_TREE_MODEL(store));

      GtkTreeIter iter;

      if (offerMultiMon) {
         gtk_list_store_append(store, &iter);
         gtk_list_store_set(store, &iter,
                            SIZE_LABEL_COLUMN, _("All Monitors"),
                            SIZE_VALUE_COLUMN, Prefs::ALL_SCREENS,
                            -1);
      }

      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter,
                         SIZE_LABEL_COLUMN, _("Full Screen"),
                         SIZE_VALUE_COLUMN, Prefs::FULL_SCREEN,
                         -1);

      if (offerWindowSizes) {
         gtk_list_store_append(store, &iter);
         gtk_list_store_set(store, &iter,
                            SIZE_LABEL_COLUMN, _("Large Window"),
                            SIZE_VALUE_COLUMN, Prefs::LARGE_WINDOW,
                            -1);

         gtk_list_store_append(store, &iter);
         gtk_list_store_set(store, &iter,
                            SIZE_LABEL_COLUMN, _("Small Window"),
                            SIZE_VALUE_COLUMN, Prefs::SMALL_WINDOW,
                            -1);

         gtk_list_store_append(store, &iter);
         gtk_list_store_set(store, &iter,
                            SIZE_LABEL_COLUMN, "",
                            SIZE_VALUE_COLUMN, Prefs::CUSTOM_SIZE,
                            -1);
         UpdateCustomSize();

         gtk_list_store_append(store, &iter);
         gtk_list_store_set(store, &iter,
                            SIZE_LABEL_COLUMN, _("Custom..."),
                            SIZE_VALUE_COLUMN, -1,
                            -1);
      }
      SetDesktopSize(Prefs::GetPrefs()->GetDefaultDesktopSize());

      l = GTK_LABEL(gtk_label_new_with_mnemonic(_("D_isplay:")));
      gtk_widget_show(GTK_WIDGET(l));
      gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(l), false, false, 0);
      gtk_label_set_mnemonic_widget(l, GTK_WIDGET(mWindowSize));
   }
   /*
    * We need to manually update the size here:
    *
    * If the saved pref is not full screen, and --fullscreen was
    * used, and all monitors is not available, then the
    * SetDesktopSize() above won't trigger a size changed, since
    * the UI state was already full screen.
    *
    * See bug 485605.
    */
   Prefs::GetPrefs()->SetDefaultDesktopSize(GetDesktopSize());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::~DesktopSelectDlg --
 *
 *      Destructor. Unrefs the button pixbufs and frees mButtonPath if it
 *      exists.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

DesktopSelectDlg::~DesktopSelectDlg()
{
   DestroyPopup();
   g_object_unref(mButtonNormal);
   g_object_unref(mButtonHover);
   g_object_unref(mButtonOpen);
   if (mButtonPath) {
      gtk_tree_path_free(mButtonPath);
      mButtonPath = NULL;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::IsValid --
 *
 *      Overridden from Dlg::IsValid.  Indicates whether a valid desktop has
 *      been selected.
 *
 * Results:
 *      true if the current choose is an 'active' desktop
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
DesktopSelectDlg::IsValid()
{
   Desktop *desktop = GetDesktop();
   if (desktop && desktop->CanConnect()) {
      switch (desktop->GetStatus()) {
      case Desktop::STATUS_AVAILABLE_REMOTE:
      case Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN:
      case Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT:
         return !mIsOffline;
      default:
         return true;
      }
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::UpdateButton --
 *
 *      Updates the sensitivity of the Connect button.
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
DesktopSelectDlg::UpdateButton(DesktopSelectDlg *that) // IN
{
   ASSERT(that);

   that->updateForwardButton(that->IsValid(), that->GetForwardVisible());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::UpdateList --
 *
 *      Clears and rebuilds the list of desktops, preserving selection.
 *      Calls UpdateButton to update the Connect button state.
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
DesktopSelectDlg::UpdateList(std::vector<Desktop *> &desktops, // IN
                             Util::string select)              // IN/OPT
{
   GtkTreeIter iter;
   // If we weren't given a desktop to select, remember what's selected now.
   if (select.empty()) {
      // We can't use GetDesktop() now; the Desktop pointer may be invalid.
      if (gtk_tree_selection_get_selected(
             gtk_tree_view_get_selection(mDesktopList),
             NULL,
             &iter)) {
         gchar *tmpName;
         gtk_tree_model_get(GTK_TREE_MODEL(mStore), &iter,
                            NAME_COLUMN, &tmpName,
                            -1);
         select = tmpName;
         g_free(tmpName);
      }
   }

   // Clear the list store and rebuild
   gtk_list_store_clear(mStore);

   for (std::vector<Desktop*>::iterator i = desktops.begin();
        i != desktops.end(); i++) {
      Desktop *desktop = *i;
      gtk_list_store_append(mStore, &iter);

      Util::string name = desktop->GetName();
      Util::string status = desktop->GetStatusMsg(mIsOffline);
      GdkPixbuf *pb = GetDesktopIcon(desktop->GetStatus());

      char *label = g_markup_printf_escaped(
         "<b>%s</b>\n<span size=\"smaller\">%s</span>",
         name.c_str(),
         status.c_str());
      gtk_list_store_set(mStore, &iter,
                         ICON_COLUMN, pb,
                         LABEL_COLUMN, label,
                         NAME_COLUMN, name.c_str(),
                         DESKTOP_COLUMN, desktop,
                         BUTTON_COLUMN, desktop->IsCVP() ? NULL : mButtonNormal,
                         -1);
      g_free(label);

      if (name == select || i == desktops.begin()) {
         gtk_tree_selection_select_iter(
            gtk_tree_view_get_selection(mDesktopList),
            &iter);
      }
      if (pb) {
         g_object_unref(pb);
      }
   }
   UpdateButton(this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetDesktopIcon --
 *
 *      Given a desktop status, returns a GdkPixbuf representing that status.
 *
 * Results:
 *      GdkPixbuf * which must be unrefed by the user.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GdkPixbuf*
DesktopSelectDlg::GetDesktopIcon(Desktop::Status status) // IN
{
   const guint8 *data;

   switch (status) {
   case Desktop::STATUS_ROLLING_BACK:
   case Desktop::STATUS_SERVER_ROLLBACK:
   case Desktop::STATUS_HANDLING_SERVER_ROLLBACK:
      data = desktop_local_rollback_32x;
      break;
   case Desktop::STATUS_CHECKED_OUT_DISABLED:
      data = desktop_local32xdisabled;
      break;
   case Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_IN:
      // XXX: Need to add an icon for when we are offline.
      data = desktop_checkin_pause32x;
      break;
   case Desktop::STATUS_NONBACKGROUND_TRANSFER_CHECKING_OUT:
      // XXX: Need to add an icon for when we are offline.
      data = desktop_checkout_pause32x;
      break;
   case Desktop::STATUS_AVAILABLE_LOCAL:
      data = desktop_local32x;
      break;
   case Desktop::STATUS_LOGGED_ON:
   case Desktop::STATUS_AVAILABLE_REMOTE:
      data = mIsOffline ? desktop_remote32x_disabled : desktop_remote32x;
      break;
   default:
      data = desktop_remote32x_disabled;
      break;
   }

   return gdk_pixbuf_new_from_inline(-1, data, false, NULL);
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
      return GetDesktopAt(&iter);
   }
   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetDesktopAt --
 *
 *      Return the Desktop in the tree model pointed to by the
 *      iterator.
 *
 * Results:
 *      Desktop pointed to by iter.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */
Desktop *
DesktopSelectDlg::GetDesktopAt(GtkTreeIter *iter) // IN
{
   GValue value = { 0 };
   gtk_tree_model_get_value(GTK_TREE_MODEL(mStore), iter, DESKTOP_COLUMN,
                            &value);
   ASSERT(G_VALUE_HOLDS_POINTER(&value));
   Desktop *ret = reinterpret_cast<Desktop *>(g_value_get_pointer(&value));
   ASSERT(ret);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnSizeChanged --
 *
 *      Callback for the user selecting a size.  A -1 value here
 *      denotes that they chose the "custom" item, in which case we
 *      show the dialog and then possibly update the custom size.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop & custom desktop size prefs may be updated.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::OnSizeChanged(GtkComboBox *widget, // IN
                                gpointer data)       // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg *>(data);
   ASSERT(that);

   if (that->GetDesktopSize() != -1) {
      Prefs::GetPrefs()->SetDefaultDesktopSize(that->GetDesktopSize());
      return;
   }

   WindowSizeDlg dlg(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(widget))));
   GdkRectangle size;
   if (dlg.Run(&size)) {
      Prefs::GetPrefs()->SetDefaultCustomDesktopSize(&size);
      that->UpdateCustomSize();
      that->SetDesktopSize(Prefs::CUSTOM_SIZE);
   } else {
      that->SetDesktopSize(Prefs::GetPrefs()->GetDefaultDesktopSize());
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetIterForDesktopSize --
 *
 *      Sets iter to the corresponding iterator for the given size.
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
DesktopSelectDlg::GetIterForDesktopSize(Prefs::DesktopSize size, // IN
                                        GtkTreeIter *iter)       // OUT
{
   Prefs::DesktopSize iterSize;
   GtkTreeModel *model = gtk_combo_box_get_model(mWindowSize);

   gtk_tree_model_get_iter_first(model, iter);
   do {
      gtk_tree_model_get(model, iter, SIZE_VALUE_COLUMN, &iterSize, -1);
   } while (iterSize != size && gtk_tree_model_iter_next(model, iter));
   /*
    * Custom sizes are disabled in full screen, and multimon may not
    * be available, so default to full screen in those cases.
    */
   if (iterSize != size && size != Prefs::FULL_SCREEN) {
      GetIterForDesktopSize(Prefs::FULL_SCREEN, iter);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::SetDesktopSize --
 *
 *      Select the menu item for the passed-in size.
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
DesktopSelectDlg::SetDesktopSize(Prefs::DesktopSize size) // IN
{
   GtkTreeIter iter;
   GetIterForDesktopSize(size, &iter);
   gtk_combo_box_set_active_iter(mWindowSize, &iter);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetDesktopSize --
 *
 *      Gets the selected desktop size.
 *
 * Results:
 *      The desktop size.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Prefs::DesktopSize
DesktopSelectDlg::GetDesktopSize()
{
   GtkTreeIter iter;
   if (!mWindowSize || !gtk_combo_box_get_active_iter(mWindowSize, &iter)) {
      return Prefs::FULL_SCREEN;
   }

   Prefs::DesktopSize size;
   gtk_tree_model_get(gtk_combo_box_get_model(mWindowSize), &iter,
                      SIZE_VALUE_COLUMN, &size,
                      -1);
   return size;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::UpdateCustomSize --
 *
 *      Updates the menu item for the custom size based on the current
 *      custom size.
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
DesktopSelectDlg::UpdateCustomSize()
{
   GtkTreeIter iter;
   GdkRectangle size;
   GtkListStore *store =
      GTK_LIST_STORE(gtk_combo_box_get_model(mWindowSize));
   GetIterForDesktopSize(Prefs::CUSTOM_SIZE, &iter);
   Prefs::GetPrefs()->GetDefaultCustomDesktopSize(&size);
   gtk_list_store_set(store, &iter, SIZE_LABEL_COLUMN,
                      Util::Format(_("%d x %d"), size.width, size.height).c_str(),
                      -1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnConnect --
 *
 *      Callback for Connect popup click. Emits the connect signal.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      action signal emitted with ACTION_CONNECT
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
      that->action(ACTION_CONNECT);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::ConfirmAction --
 *
 *      Pops up a dialog to confirm the given action. If the user confirms,
 *      emits the action
 *
 * Results:
 *      None
 *
 * Side effects:
 *      connect(act) emitted if confirmed.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::ConfirmAction(Action act) // IN
{
   Util::string question;
   Util::string button;
   switch (act) {
   case ACTION_RESET:
      question = _("Are you sure you want to reset %s?\n\n"
                   "Any unsaved data may be lost.");
      button = _("_Reset");
      break;
   case ACTION_KILL_SESSION:
      question = _("Are you sure you want to end your current session "
                   "with %s?\n\nAny unsaved data may be lost.");
      button = _("_Log Off");
      break;
   case ACTION_ROLLBACK:
      question = _("Are you sure you want to rollback %s?\n\n"
                   "Any changes made to the checked-out desktop on another "
                   "machine since your last backup will be discarded.");
      button = _("_Rollback");
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }

   GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(mDesktopList));
   GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(top),
      GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION,
      GTK_BUTTONS_NONE,
      question.c_str(),
      GetDesktop()->GetName().c_str());
   gtk_window_set_title(GTK_WINDOW(dialog),
                        gtk_window_get_title(GTK_WINDOW(top)));
   gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                          button.c_str(), GTK_RESPONSE_ACCEPT,
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                          NULL);
   if (GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(dialog))) {
      action(act);
   }
   gtk_widget_destroy(dialog);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnKillSession --
 *
 *      User selected the "Logout" menu item; confirm and try to kill our
 *      session.
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

   that->ConfirmAction(ACTION_KILL_SESSION);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnResetDesktop --
 *
 *      User selected "Reset" menu item; confirm that they wish to
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

   that->ConfirmAction(ACTION_RESET);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnRollback --
 *
 *      User selected the "Rollback" menu item; confirm and try to roll back
 *      the desktop.
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
DesktopSelectDlg::OnRollback(GtkMenuItem *item, // IN/UNUSED
                             gpointer data)     // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   that->ConfirmAction(ACTION_ROLLBACK);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::ShowPopup --
 *
 *      Display a context menu for a desktop with "advanced" commands
 *      such as resetting a session, and VMDI options, etc.
 *      Remembers the popup in mPopup.
 *
 *      If customPosition is true, uses DesktopSelectDlg::PopupPositionFunc to
 *      position the menu at the list button corresponding to the currently-
 *      selected desktop. This requires that mButtonPath is set.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A popup is displayed.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::ShowPopup(GdkEventButton *evt, // IN/OPT
                            bool customPosition) // IN/OPT
{
   ASSERT(!customPosition || mButtonPath);

   Desktop *desktop = GetDesktop();
   if (!desktop) {
      return;
   }

   DestroyPopup();

   mPopup = GTK_MENU(gtk_menu_new());
   gtk_widget_show(GTK_WIDGET(mPopup));
   gtk_menu_attach_to_widget(mPopup, GTK_WIDGET(mDesktopList), OnPopupDetach);
   g_signal_connect_after(mPopup, "deactivate",
                          G_CALLBACK(&DesktopSelectDlg::OnPopupDeactivate),
                          this);

   GtkWidget *item;

   bool busy = desktop->GetConnectionState() != Desktop::STATE_DISCONNECTED;

   item = gtk_menu_item_new_with_mnemonic(_("Co_nnect"));
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);
   if (desktop->CanConnect() && !busy) {
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(&DesktopSelectDlg::OnConnect), this);
   } else {
      gtk_widget_set_sensitive(item, false);
   }

   item = gtk_separator_menu_item_new();
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);

   item = gtk_menu_item_new_with_mnemonic(_("_Protocols"));
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);

   GtkMenu *submenu = GTK_MENU(gtk_menu_new());
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), GTK_WIDGET(submenu));

   /*
    * When a new radio menu item is created, it prepends it to the group.
    * This ruins our ability to index into the protocol vector we already
    * have.  Thus, we reverse iterate and prepend to the submenu to fix
    * the issue.
    */
   GSList *group = NULL;
   std::vector<Util::string> protocols = desktop->GetProtocols();
   for (std::vector<Util::string>::reverse_iterator iter = protocols.rbegin();
        iter != protocols.rend(); iter++) {
      item = gtk_radio_menu_item_new_with_mnemonic(
         group, Protocols::GetMnemonic(*iter).c_str());
      gtk_widget_show(item);
      gtk_menu_shell_prepend(GTK_MENU_SHELL(submenu), item);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      bool active = *iter == desktop->GetProtocol();

      // XXX: Need checks to make sure these are actually installed
      switch (Protocols::GetProtocolFromName(*iter)) {
      case Protocols::RDP:
      case Protocols::PCOIP:
         gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
         g_signal_connect(G_OBJECT(item), "toggled",
                          G_CALLBACK(OnProtocolSelected), desktop);
         break;
      default:
         gtk_widget_set_sensitive(GTK_WIDGET(item), false);
         break;
      }
   }

   item = gtk_separator_menu_item_new();
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);

   item = gtk_menu_item_new_with_mnemonic(_("_Log Off"));
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);
   if (!desktop->GetSessionID().empty() && !busy) {
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(&DesktopSelectDlg::OnKillSession),
                       this);
   } else {
      gtk_widget_set_sensitive(item, false);
   }

   item = gtk_menu_item_new_with_mnemonic(_("_Reset"));
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);
   if (desktop->CanReset() && !busy) {
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(&DesktopSelectDlg::OnResetDesktop), this);
   } else {
      gtk_widget_set_sensitive(item, false);
   }

   item = gtk_separator_menu_item_new();
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);

   item = gtk_menu_item_new_with_mnemonic(_("Roll_back"));
   gtk_widget_show(item);
   gtk_menu_shell_append(GTK_MENU_SHELL(mPopup), item);
   // XXX: Should also check policies here.
   if (desktop->GetOfflineState() == BrokerXml::OFFLINE_CHECKED_OUT &&
       !busy) {
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(&DesktopSelectDlg::OnRollback), this);
   } else {
      gtk_widget_set_sensitive(item, false);
   }

   gtk_menu_popup(mPopup, NULL, NULL,
                  customPosition ? &DesktopSelectDlg::PopupPositionFunc : NULL,
                  customPosition ? this : NULL,
                  evt ? evt->button : 0,
                  evt ? evt->time : gtk_get_current_event_time());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::KillPopup --
 *
 *      If any popup is visible, pop it down.
 *      This does not destroy the popup. Kill the
 *      hover image associated with it too.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::KillPopup()
{
   if (PopupVisible()) {
      gtk_menu_popdown(mPopup);
   }
   KillHover();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::DestroyPopup --
 *
 *      Ensure any popups have been destroyed and mPopup is NULL.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::DestroyPopup()
{
   if (mPopup) {
      gtk_widget_destroy(GTK_WIDGET(mPopup));
      // Hopefully the destroy triggered our detach callback.
      ASSERT(!mPopup);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::GetPathForButton --
 *
 *      Given coordinates (x,y) relative to the bin_window of mDesktopList,
 *      returns the path of a row in the list if the coordinates are within
 *      the area of the button corresponding to that row.
 *      The returned GtkTreePath must be freed by the caller.
 *
 * Results:
 *      Path corresponding to the list button's row, or NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkTreePath *
DesktopSelectDlg::GetPathForButton(int x, // IN
                                   int y) // IN
{
   GtkTreePath *path;
   GtkTreeViewColumn *col;

   // cell_x, cell_y will be set with the in-cell coordinates of the pointer.
   int cell_x, cell_y;
   if (!gtk_tree_view_get_path_at_pos(mDesktopList,
                                      x, y,
                                      &path, &col,
                                      &cell_x, &cell_y) ||
       col != mButtonColumn) {
      return NULL;
   }

   // Total area of cell in question.
   GdkRectangle back;
   gtk_tree_view_get_background_area(mDesktopList, path, col, &back);

   // Padding around the button image.
   int xpad = (back.width - BUTTON_SIZE) / 2;
   int ypad = (back.height - BUTTON_SIZE) / 2;

   /*
    * For simplicity of comparison, pretend the button is at the upper-left
    * corner of the cell.
    */
   cell_x -= xpad;
   cell_y -= ypad;

   /*
    * The button's tiny and we've used integer division that might have
    * resulted in an off-by-one discrepancy. So just make the bounds inclusive.
    */
   if (cell_x >= 0 && cell_x <= BUTTON_SIZE &&
       cell_y >= 0 && cell_y <= BUTTON_SIZE) {
      return path;
   }
   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::CheckHover --
 *
 *      Check if coordinates (x,y) relative to the bin_window of mDesktopList
 *      are hovering over a button. If so, change the pixbuf for the button.
 *      If not, un-hover the hovered button (if any).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::CheckHover(int x, // IN
                             int y) // IN
{
   GtkTreePath *path = GetPathForButton(x, y);

   if (path) {
      // Only hover if there is no menu popped up.
      if (!PopupVisible()) {
         GtkTreeIter iter;
         gtk_tree_model_get_iter(GTK_TREE_MODEL(mStore), &iter, path);
         Desktop *desktop = GetDesktopAt(&iter);
         if (desktop->IsCVP()) {
            return;
         }
         gtk_list_store_set(mStore, &iter,
                            BUTTON_COLUMN, mButtonHover,
                            -1);
         mButtonPath = path;
      } else {
         gtk_tree_path_free(path);
      }
   } else {
      KillHover();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::KillHover --
 *
 *      If any button is displaying the "hover" or "open" pixbuf, set it to
 *      the "normal" pixbuf.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::KillHover()
{
   if (mButtonPath) {
      GtkTreeIter iter;
      gtk_tree_model_get_iter(GTK_TREE_MODEL(mStore), &iter, mButtonPath);
      Desktop *desktop = GetDesktopAt(&iter);
      if (desktop->IsCVP()) {
         return;
      }
      gtk_list_store_set(mStore, &iter,
                         BUTTON_COLUMN, mButtonNormal,
                         -1);
      gtk_tree_path_free(mButtonPath);
      mButtonPath = NULL;
   }
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

   GtkTreeModel *model;
   GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(
          gtk_tree_view_get_selection(that->mDesktopList),
          &model,
          &iter)) {
      Desktop *desktop = that->GetDesktopAt(&iter);
      if (desktop->IsCVP()) {
         return true;
      }
      gtk_list_store_set(that->mStore, &iter,
                         BUTTON_COLUMN, that->mButtonOpen,
                         -1);
      that->mButtonPath = gtk_tree_model_get_path(model, &iter);
      that->ShowPopup(NULL, true);
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnButtonPress --
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
DesktopSelectDlg::OnButtonPress(GtkWidget *widget,   // IN
                                GdkEventButton *evt, // IN
                                gpointer data)       // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   if (that->mInButtonPress) {
      return false; // We re-entered.
   }

   if (evt->type != GDK_BUTTON_PRESS) {
      return false; // Let the normal handler run.
   }

   /*
    * If the user clicks on a row that is not fully visible, the widget's
    * event handler will move the list so the row is selected and fully
    * visible. Run our handling code before this so we register the need
    * for a button-click popup properly. (Otherwise, we'll
    * check for a button click after the button has moved out from under
    * the pointer.) However, wait for the widget's handler to run before
    * actually showing the popup, so the proper desktop is selected before
    * ShowPopup runs. Though the list may have moved, we can be pretty sure
    * that the row the user clicked on is now selected and fully visible.
    */
   bool killedPopup = false;
   if (evt->button == 1) {
      // If a menu's already open, this click should close it.
      if (that->PopupVisible()) {
         that->KillPopup();
         killedPopup = true;
      } else {
         // Make sure we're using the right window coordinate system.
         ASSERT(evt->window ==
                gtk_tree_view_get_bin_window(that->mDesktopList));
         GtkTreePath *path = that->GetPathForButton((int)evt->x,
                                                    (int)evt->y);
         if (path) {
            GtkTreeIter iter;
            gtk_tree_model_get_iter(GTK_TREE_MODEL(that->mStore), &iter, path);
            Desktop *desktop = that->GetDesktopAt(&iter);
            if (!desktop->IsCVP()) {
               gtk_list_store_set(that->mStore, &iter,
                                  BUTTON_COLUMN, that->mButtonOpen,
                                  -1);
               that->mButtonPath = path;
            }
         }
      }
   }

   // See block comment above.
   that->mInButtonPress = true;
   gboolean handled = gtk_widget_event(widget, (GdkEvent *)evt);
   that->mInButtonPress = false;

   if (!handled) {
      return false;
   }

   // No pop-ups for CVP desktops
   Desktop *desktop = that->GetDesktop();
   if (desktop->IsCVP()) {
      return true;
   }

   // The selection is fully updated by now.
   if (evt->button == 1) {
      if (that->mButtonPath) {
         that->ShowPopup(evt, true);
      }
      return killedPopup || that->PopupVisible();
   } else if (evt->button == 3) {
      that->ShowPopup(evt, false);
      return true;
   }

   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::PopupPositionFunc --
 *
 *      Position function for popup menu. Returns coordinates at the bottom-
 *      center of the list button corresponding to that->mButtonPath.
 *
 * Results:
 *      Coordinates for menu popup; pushIn = true.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::PopupPositionFunc(GtkMenu *menu,    // IN/UNUSED
                                    int *x,           // OUT
                                    int *y,           // OUT
                                    gboolean *pushIn, // OUT
                                    gpointer data)    // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);
   ASSERT(that->mButtonPath);

   // Cell coordinates relative to bin window.
   GdkRectangle cell;
   gtk_tree_view_get_cell_area(that->mDesktopList, that->mButtonPath,
                               that->mButtonColumn, &cell);

   // Bin window root coordinates.
   int binX, binY;
   gdk_window_get_origin(
      gtk_tree_view_get_bin_window(that->mDesktopList), &binX, &binY);

   // Place the menu at the bottom-center of the button image.
   if (x) {
      *x = binX + cell.x + cell.width / 2;
   }
   if (y) {
      *y = binY + cell.y + cell.height / 2 + BUTTON_SIZE / 2;
   }
   if (pushIn) {
      *pushIn = true;
   }
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
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   that->KillHover();
   int x, y;
   gdk_window_get_pointer(gtk_tree_view_get_bin_window(that->mDesktopList),
                          &x, &y, NULL);
   that->CheckHover(x, y);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnPopupDetach --
 *
 *      Handler for the popup detaching from mDesktopList. This means the
 *      popup is being destroyed, so we clear our pointer to it.
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
DesktopSelectDlg::OnPopupDetach(GtkWidget *widget, // IN
                                GtkMenu *popup)    // IN
{
   gpointer data = g_object_get_data(G_OBJECT(widget), DIALOG_DATA_KEY);
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   ASSERT(that->mPopup == popup);
   that->mPopup = NULL;
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
 * cdk::DesktopDelectDlg::OnPointerMove --
 *
 *      Callback for pointer movement. Checks if the pointer is hovering over
 *      a button (and updates its pixbuf accordingly).
 *
 * Results:
 *      false--the event was not handled.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopSelectDlg::OnPointerMove(GtkWidget *widget,     // IN/UNUSED
                                GdkEventMotion *event, // IN
                                gpointer data)         // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   // Make sure we're using the right window coordinate system.
   ASSERT(event->window == gtk_tree_view_get_bin_window(that->mDesktopList));
   that->CheckHover((int)event->x, (int)event->y);

   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopDelectDlg::OnPointerLeave --
 *
 *      Callback for pointer leaving the ListView. Kills any hover if a
 *      menu is not open.
 *
 * Results:
 *      false--the event was not handled.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
DesktopSelectDlg::OnPointerLeave(GtkWidget *widget,       // IN/UNUSED
                                 GdkEventCrossing *event, // IN
                                 gpointer data)           // IN
{
   DesktopSelectDlg *that = reinterpret_cast<DesktopSelectDlg*>(data);
   ASSERT(that);

   /*
    * We get this event when a popup appears, so don't undo the "open" image
    * of the popup's button.
    */
   if (!that->PopupVisible()) {
      that->KillHover();
   }

   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::OnProtocolSelected --
 *
 *      Callback for the user selecting a protocol from the list in the
 *      context menu.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
DesktopSelectDlg::OnProtocolSelected(GtkButton *button, // IN
                                     gpointer userData) // IN
{
   if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(button))) {
      return;
   }

   Desktop *desktop = reinterpret_cast<Desktop *>(userData);
   ASSERT(desktop);

   std::vector<Util::string> protoVector = desktop->GetProtocols();
   GSList *group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(button));
   int index = g_slist_index(group, button);

   ASSERT(index < (int)protoVector.size());
   desktop->SetProtocol(protoVector[index]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::DesktopSelectDlg::SetIsOffline --
 *
 *      Sets offline state and updates the forward button.
 *
 * Results:
 *      true if the offline state has changed and the list needs to be updated.
 *      false otherwise.
 *
 * Side effects:
 *      UpdateButton called.
 *
 *-----------------------------------------------------------------------------
 */

bool
DesktopSelectDlg::SetIsOffline(bool isOffline) // IN
{
   if (mIsOffline != isOffline) {
      mIsOffline = isOffline;
      return true;
   }
   return false;
}


} // namespace cdk
