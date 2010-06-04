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
 * transitionDlg.hh --
 *
 *    Shows animation while a desktop connection is established.
 */

#ifndef TRANSITION_DLG_HH
#define TRANSITION_DLG_HH


#include <boost/signal.hpp>


#include <gdk-pixbuf/gdk-pixbuf.h>


#include "dlg.hh"


namespace cdk {


class TransitionDlg
   : public Dlg
{
public:
   enum TransitionType {
      TRANSITION_PROGRESS,
      TRANSITION_ERROR
   };

   TransitionDlg(TransitionType type, const Util::string &message,
                 bool useMarkup = false);

   ~TransitionDlg();

   void SetMessage(const Util::string &message);
   void SetAnimation(GdkPixbufAnimation *animation);
   void SetAnimation(std::vector<GdkPixbuf *> pixbufs, float rate);
   void SetImage(GdkPixbuf *pixbuf);
   void SetStock(const Util::string &stockId);

   TransitionType GetTransitionType() const { return mTransitionType; }

   static std::vector<GdkPixbuf *> LoadAnimation(int data_length,
                                                 const guint8 *data,
                                                 bool copy_pixels,
                                                 unsigned int frames);

   virtual bool GetHelpVisible() { return false; }

private:
   static void OnImageRealized(GtkWidget *widget, gpointer userData);
   static void OnImageUnrealized(GtkWidget *widget, gpointer userData);

   void StartAnimating();
   void StopAnimating();
   static gboolean Animate(gpointer data);

   GtkWidget *mImage;
   std::vector<GdkPixbuf *> mPixbufs;
   unsigned int mFrame;
   float mRate;
   unsigned int mTimeout;
   TransitionType mTransitionType;
   GtkLabel *mLabel;
};


} // namespace cdk


#endif // TRANSITION_DLG_HH
