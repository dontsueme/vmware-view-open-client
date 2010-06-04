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
 * windowSizeDlg.cc --
 *
 *      Implementation of cdk::WindowSizeDlg.
 */

#include "prefs.hh"
#include "windowSizeDlg.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::WindowSizeDlg::WindowSizeDlg --
 *
 *      Constructor - create and initialize our widgets.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

WindowSizeDlg::WindowSizeDlg(GtkWindow *parent) // IN
   : mDialog(GTK_DIALOG(gtk_dialog_new_with_buttons(gtk_window_get_title(parent),
                                                    parent, GTK_DIALOG_NO_SEPARATOR,
                                                    NULL))),
     mSizeLabel(GTK_LABEL(gtk_label_new(""))),
     mSlider(GTK_HSCALE(gtk_hscale_new_with_range(0, 1, 1)))
{
   g_signal_connect_swapped(mDialog, "screen-changed",
                            G_CALLBACK(UpdateWindowSizes), this);
   g_signal_connect_swapped(mDialog, "realize",
                            G_CALLBACK(UpdateWindowSizes), this);

   GtkButton *ok = Util::CreateButton(GTK_STOCK_OK, _("_Select"));
   GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
   gtk_dialog_add_action_widget(mDialog, GTK_WIDGET(ok), GTK_RESPONSE_OK);
   gtk_dialog_set_default_response(mDialog, GTK_RESPONSE_OK);
   gtk_dialog_add_action_widget(mDialog,
                                GTK_WIDGET(Util::CreateButton(GTK_STOCK_CANCEL)),
                                GTK_RESPONSE_CANCEL);

   GtkWidget *vbox = gtk_vbox_new(false, VM_SPACING);
   gtk_widget_show(vbox);
   gtk_box_pack_start(GTK_BOX(mDialog->vbox), vbox, true, true, 0);
   gtk_container_set_border_width(GTK_CONTAINER(vbox), VM_SPACING);

   GtkWidget *l = gtk_label_new(_("Select a window size:"));
   gtk_widget_show(l);
   gtk_box_pack_start(GTK_BOX(vbox), l, false, false, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);

   gtk_widget_show(GTK_WIDGET(mSlider));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(mSlider), true, true, 0);
   g_signal_connect_swapped(G_OBJECT(mSlider), "value-changed",
                            G_CALLBACK(OnSliderChanged), this);
   gtk_scale_set_draw_value(GTK_SCALE(mSlider), false);
   gtk_range_set_increments(GTK_RANGE(mSlider), 1.0, 1.0);
   gtk_widget_set_size_request(GTK_WIDGET(mSlider), 300, -1);

   GtkWidget *hbox = gtk_hbox_new(true, 0);
   gtk_widget_show(hbox);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, true, true, 0);

   l = gtk_label_new(_("Small"));
   gtk_widget_show(l);
   gtk_box_pack_start(GTK_BOX(hbox), l, false, true, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);

   gtk_widget_show(GTK_WIDGET(mSizeLabel));
   gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(mSizeLabel), true, true, 0);

   l = gtk_label_new(_("Large"));
   gtk_widget_show(l);
   gtk_box_pack_start(GTK_BOX(hbox), l, false, true, 0);
   gtk_misc_set_alignment(GTK_MISC(l), 1.0, 0.5);

   Prefs::GetPrefs()->GetDefaultCustomDesktopSize(&mSizes[0]);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::WindowSizeDlg::~WindowSizeDlg --
 *
 *      Destructor - destroy our window.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

WindowSizeDlg::~WindowSizeDlg()
{
   gtk_widget_destroy(GTK_WIDGET(mDialog));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::WindowSizeDlg::Run --
 *
 *      Run the dialog, returning the selected size if the user chose
 *      it.
 *
 * Results:
 *      true if the user selected a size, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
WindowSizeDlg::Run(GdkRectangle *size) // OUT
{
   ASSERT(size);
   if (gtk_dialog_run(mDialog) != GTK_RESPONSE_OK) {
      return false;
   }
   *size = mSizes[(int)gtk_range_get_value(GTK_RANGE(mSlider))];
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::WindowSizeDlg::UpdateWindowSizes --
 *
 *      Populates our array of resolutions with ones that will fit on
 *      our screen.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Updates the range and value of the slider.
 *
 *-----------------------------------------------------------------------------
 */

void
WindowSizeDlg::UpdateWindowSizes(gpointer data) // IN
{
   WindowSizeDlg *that = reinterpret_cast<WindowSizeDlg *>(data);
   ASSERT(that);

   GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(that->mDialog));
   ASSERT(screen);

   GdkRectangle screenGeom;
   gdk_screen_get_monitor_geometry(
      screen,
      gdk_screen_get_monitor_at_window(screen, GTK_WIDGET(that->mDialog)->window),
      &screenGeom);

   /*
    * This handles both the initial selection, and keeping the current
    * selection if we move screens.
    */
   GdkRectangle geom =
      that->mSizes[(int)gtk_range_get_value(GTK_RANGE(that->mSlider))];

   int i = 0;
   int active = 0;

#define APPEND_SIZE(w, h)                                               \
   ASSERT(i < N_DESKTOP_SIZES);                                         \
   if (screenGeom.width > w && screenGeom.height > h) {                 \
      that->mSizes[i].width = w;                                        \
      that->mSizes[i].height = h;                                       \
      if (w == geom.width && h == geom.height) {                        \
         active = i;                                                    \
      }                                                                 \
      ++i;                                                              \
   }

   APPEND_SIZE(640, 480);
   APPEND_SIZE(800, 600);
   APPEND_SIZE(1024, 768);
   APPEND_SIZE(1280, 854);
   APPEND_SIZE(1280, 1024);
   APPEND_SIZE(1440, 900);
   APPEND_SIZE(1600, 1200);
   APPEND_SIZE(1680, 1050);
   APPEND_SIZE(1920, 1200);
   APPEND_SIZE(2560, 1600);

#undef APPEND_SIZE

#if GTK_CHECK_VERSION(2, 16, 0)
   gtk_scale_clear_marks(GTK_SCALE(that->mSlider));
#endif
   gtk_range_set_range(GTK_RANGE(that->mSlider), 0, i - 1);
   gtk_range_set_value(GTK_RANGE(that->mSlider), active);
   OnSliderChanged(that);
#if GTK_CHECK_VERSION(2, 16, 0)
   for (int j = 0; j < i; j++) {
      gtk_scale_add_mark(GTK_SCALE(that->mSlider), j, GTK_POS_BOTTOM, NULL);
   }
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::WindowSizeDlg::OnSliderChanged --
 *
 *      Updates the label when the slider changes value.
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
WindowSizeDlg::OnSliderChanged(gpointer data) // IN
{
   WindowSizeDlg *that = reinterpret_cast<WindowSizeDlg *>(data);
   ASSERT(that);

   int idx = (int)gtk_range_get_value(GTK_RANGE(that->mSlider));
   gtk_label_set_text(that->mSizeLabel,
                      Util::Format(_("%d x %d"),
                                   that->mSizes[idx].width,
                                   that->mSizes[idx].height).c_str());
}


} // namespace cdk
