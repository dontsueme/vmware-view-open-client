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
 * windowSizeDlg.hh --
 *
 *      Dialog to select a window size for the desktop.
 */

#ifndef WINDOW_SIZE_DLG_HH
#define WINDOW_SIZE_DLG_HH


#include <gtk/gtk.h>


namespace cdk {


class WindowSizeDlg
{
public:
   WindowSizeDlg(GtkWindow *parent);
   ~WindowSizeDlg();

   bool Run(GdkRectangle *size);

private:
   enum {
      N_DESKTOP_SIZES = 10
   };

   static void UpdateWindowSizes(gpointer data);
   static void OnSliderChanged(gpointer data);

   GtkDialog *mDialog;
   GtkLabel *mSizeLabel;
   GtkHScale *mSlider;
   GdkRectangle mSizes[N_DESKTOP_SIZES];
};


} // namespace cdk


#endif // WINDOW_SIZE_DLG_HH
