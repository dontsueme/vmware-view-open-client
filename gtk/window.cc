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
 * window.cc --
 *
 *    Implementation of cdk::Window.
 *
 */


#include <boost/bind.hpp>
#include <sys/stat.h>
#include <fcntl.h>
#include <gtk/gtk.h>

extern "C" {
#include "vm_basic_types.h"
#include "base64.h"
} // extern "C"

#include "brokerDlg.hh"
#include "cdkErrors.h"
#include "certViewer.hh"
#include "cryptoki.hh"
#ifndef __MINGW32__
#include "desktopDlg.hh"
#endif
#include "disclaimerDlg.hh"
#include "icons/spinner_anim.h"
#include "icons/view_16x.h"
#include "icons/view_32x.h"
#include "icons/view_48x.h"
#include "icons/view_client_banner.h"
#ifdef HAVE_PCOIP_BANNER
#include "icons/view_client_banner_pcoip.h"
#endif
#include "loginDlg.hh"
#include "passwordDlg.hh"
#include "prefs.hh"
#include "protocols.hh"
#ifdef __MINGW32__
#include "mstsc.hh"
#else
#include "rdesktop.hh"
#include "rmks.hh"
#endif
#include "scCertDetailsDlg.hh"
#include "scCertDlg.hh"
#include "scInsertPromptDlg.hh"
#include "scPinDlg.hh"
#include "securIDDlg.hh"
#include "transitionDlg.hh"
#include "tunnel.hh"
#include "window.hh"


#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#endif


enum {
   RESPONSE_CTRL_ALT_DEL = 1,
   RESPONSE_DISCONNECT,
   RESPONSE_RESET,
   RESPONSE_QUIT
};


#define BANNER_HEIGHT 62
#define BANNER_MIN_WIDTH 480
#define BUFFER_LEN 256
#define SPINNER_ANIM_FPS_RATE 10
#define SPINNER_ANIM_N_FRAMES 20
#define TOKEN_EVENT_TIMEOUT_MS 500

#ifdef _WIN32
#define COOKIE_FILE_NAME "view-cookies"
#else
#define VMWARE_HOME_DIR "~/.vmware"
#define COOKIE_FILE_NAME VMWARE_HOME_DIR"/view-cookies"
#endif
#define COOKIE_FILE_MODE (S_IRUSR | S_IWUSR)


namespace cdk {


/*
 *-------------------------------------------------------------------
 *
 * cdk::Window::Window --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-------------------------------------------------------------------
 */

Window::Window()
   : mBroker(NULL),
     mDlg(NULL),
     mWindow(GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL))),
     mToplevelBox(GTK_VBOX(gtk_vbox_new(false, 0))),
     mContentBox(NULL),
     mFullscreenAlign(NULL),
     mBackgroundImage(NULL),
     mActionArea(NULL),
     mCancelButton(NULL),
     mForwardButton(NULL),
     mHelpButton(NULL),
     mViewCertButton(NULL),
     mCryptoki(NULL),
     mTokenEventTimeout(0),
     mCanceledScDlg(false),
     mAuthCert(NULL),
     mDesktopHelper(NULL),
     mTokenEventAction(ACTION_NONE),
     mCadDlg(NULL)
{
   gtk_widget_show(GTK_WIDGET(mToplevelBox));
   gtk_container_add(GTK_CONTAINER(mWindow), GTK_WIDGET(mToplevelBox));
   g_signal_connect(GTK_WIDGET(mToplevelBox), "size-allocate",
                    G_CALLBACK(&Window::OnSizeAllocate), this);

   GList *li = NULL;
#define ADD_ICON(icon)                                                  \
   {                                                                    \
      GdkPixbuf *pb = gdk_pixbuf_new_from_inline(-1, icon, false, NULL); \
      if (pb) {                                                         \
         li = g_list_prepend(li, pb);                                   \
      }                                                                 \
   }

   ADD_ICON(view_16x);
   ADD_ICON(view_32x);
   ADD_ICON(view_48x);
#undef ADD_ICON

   gtk_window_set_default_icon_list(li);
   g_list_foreach(li, (GFunc)g_object_unref, NULL);
   g_list_free(li);
   li = NULL;

   g_object_add_weak_pointer(G_OBJECT(mWindow), (gpointer *)&mWindow);

#ifndef __MINGW32__
   g_signal_connect(mWindow, "realize", G_CALLBACK(OnRealize), this);
   g_signal_connect(mWindow, "unrealize", G_CALLBACK(OnUnrealize), this);
#endif
}


/*
 *-------------------------------------------------------------------
 *
 * cdk::Window::~Window --
 *
 *      Destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Global libraries and resources are shutdown and released
 *
 *-------------------------------------------------------------------
 */

Window::~Window()
{
   Reset();

   ASSERT(!mDesktopHelper);
   ASSERT(!mBroker);
   delete mDlg;

   if (mWindow) {
      gtk_widget_destroy(GTK_WIDGET(mWindow));
   }

   delete mCryptoki;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::Reset --
 *
 *      Reset state to allow a new login.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      State is restored as if we had restarted.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::Reset()
{
   mDesktopUIExitCnx.disconnect();
   delete mDesktopHelper;
   mDesktopHelper = NULL;

   if (mCadDlg) {
      gtk_dialog_response(GTK_DIALOG(mCadDlg), GTK_RESPONSE_CANCEL);
   }

   if (mTokenEventTimeout) {
      StopWatchingForTokenEvents();
   }
   if (mCryptoki) {
      /*
       * If we have a certificate list, mAuthCert is just a pointer to
       * the copy stored in the list.  Otherwise, mAuthCert is an
       * extra copy and needs to be freed itself.
       */
      if (mCertificates.size()) {
         mCryptoki->FreeCertificates(mCertificates);
      } else {
         mCryptoki->FreeCert(mAuthCert);
      }
      mAuthCert = NULL;

      mCryptoki->CloseAllSessions();
   } else {
      /*
       * If we don't have a Cryptoki object, we better not have a
       * cert.
      */
      ASSERT(!mAuthCert);
   }
   SetBroker(NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::InitWindow --
 *
 *      Set up the main UI to either be a fullscreen window that the dialogs
 *      are placed over, or a regular window that dialogs go into.
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
Window::InitWindow()
{
   mContentBox = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   gtk_widget_show(GTK_WIDGET(mContentBox));
   g_object_add_weak_pointer(G_OBJECT(mContentBox) , (gpointer *)&mContentBox);

   // If a background image was specified, go into fullscreen mode.
   if (GetFullscreen()) {
      /*
       * http://www.vmware.com/files/pdf/VMware_Logo_Usage_and_Trademark_Guidelines_Q307.pdf
       *
       * VMware Blue is Pantone 645 C or 645 U (R 116, G 152, B 191 = #7498bf).
       */
      GdkColor blue;
      gdk_color_parse("#7498bf", &blue);
      gtk_widget_modify_bg(GTK_WIDGET(mWindow), GTK_STATE_NORMAL, &blue);

      g_signal_connect(GTK_WIDGET(mWindow), "realize",
         G_CALLBACK(&Window::FullscreenWindow), NULL);

      GtkFixed *fixed = GTK_FIXED(gtk_fixed_new());
      gtk_widget_show(GTK_WIDGET(fixed));
      gtk_box_pack_start(GTK_BOX(mToplevelBox), GTK_WIDGET(fixed), true, true,
                         0);

      if (!Prefs::GetPrefs()->GetBackground().empty()) {
         mBackgroundImage = GTK_IMAGE(gtk_image_new());
         gtk_widget_show(GTK_WIDGET(mBackgroundImage));
         gtk_fixed_put(fixed, GTK_WIDGET(mBackgroundImage), 0, 0);
         g_object_add_weak_pointer(G_OBJECT(mBackgroundImage),
                                   (gpointer *)&mBackgroundImage);
      }

      CreateAlignment(fixed);

      /*
       * Use a GtkEventBox to get the default background color.
       */
      GtkEventBox *eventBox = GTK_EVENT_BOX(gtk_event_box_new());
      gtk_widget_show(GTK_WIDGET(eventBox));
      gtk_container_add(GTK_CONTAINER(mFullscreenAlign), GTK_WIDGET(eventBox));

      GtkFrame *frame = GTK_FRAME(gtk_frame_new(NULL));
      gtk_widget_show(GTK_WIDGET(frame));
      gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
      gtk_container_add(GTK_CONTAINER(eventBox), GTK_WIDGET(frame));

      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(mContentBox));
   } else {
      gtk_window_unfullscreen(mWindow);
      gtk_window_set_position(mWindow, GTK_WIN_POS_CENTER);
      gtk_window_set_gravity(mWindow, GDK_GRAVITY_CENTER);
      gtk_box_pack_start(GTK_BOX(mToplevelBox), GTK_WIDGET(mContentBox),
                         true, true, 0);
   }

   GtkWidget *img = CreateBanner();
   gtk_widget_show(img);
   gtk_box_pack_start(GTK_BOX(mContentBox), img, false, false, 0);

   gtk_window_set_title(mWindow, _(PRODUCT_VIEW_CLIENT_NAME));
   g_signal_connect(mWindow, "key-press-event",
                    G_CALLBACK(&Window::OnKeyPress), this);

   GtkWidget *align = gtk_alignment_new(0, 0, 1, 0);
   gtk_widget_show(align);
   gtk_box_pack_end(GTK_BOX(mContentBox), align, true, true, 0);
   gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0,
                             VM_SPACING, VM_SPACING, VM_SPACING);

   ASSERT(mActionArea == NULL);
   mActionArea = GTK_HBUTTON_BOX(gtk_hbutton_box_new());
   gtk_widget_show(GTK_WIDGET(mActionArea));
   gtk_container_add(GTK_CONTAINER(align), GTK_WIDGET(mActionArea));
   gtk_box_set_spacing(GTK_BOX(mActionArea), VM_SPACING);
   g_object_add_weak_pointer(G_OBJECT(mActionArea), (gpointer *)&mActionArea);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(mActionArea), GTK_BUTTONBOX_END);

   ASSERT(mForwardButton == NULL);
   mForwardButton = Util::CreateButton(GTK_STOCK_OK);
   gtk_widget_show(GTK_WIDGET(mForwardButton));
   gtk_container_add(GTK_CONTAINER(mActionArea), GTK_WIDGET(mForwardButton));
   GTK_WIDGET_SET_FLAGS(mForwardButton, GTK_CAN_DEFAULT);
   gtk_window_set_default(mWindow, GTK_WIDGET(mForwardButton));
   g_object_add_weak_pointer(G_OBJECT(mForwardButton),
                             (gpointer *)&mForwardButton);
   g_signal_connect(mForwardButton, "clicked", G_CALLBACK(OnForward), this);

   ASSERT(mCancelButton == NULL);
   mCancelButton = Util::CreateButton(GTK_STOCK_CANCEL);
   gtk_widget_show(GTK_WIDGET(mCancelButton));
   gtk_container_add(GTK_CONTAINER(mActionArea), GTK_WIDGET(mCancelButton));
   g_object_add_weak_pointer(G_OBJECT(mCancelButton),
                             (gpointer *)&mCancelButton);
   g_signal_connect(mCancelButton, "clicked", G_CALLBACK(OnCancel), this);

   ASSERT(mHelpButton == NULL);
   mHelpButton = Util::CreateButton(GTK_STOCK_HELP);
   gtk_widget_show(GTK_WIDGET(mHelpButton));
   gtk_container_add(GTK_CONTAINER(mActionArea), GTK_WIDGET(mHelpButton));
   gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(mActionArea),
                                      GTK_WIDGET(mHelpButton), true);
   g_object_add_weak_pointer(G_OBJECT(mHelpButton),
                             (gpointer *)&mHelpButton);
   g_signal_connect(mHelpButton, "clicked", G_CALLBACK(OnHelp), this);


   ASSERT(mViewCertButton == NULL);
   mViewCertButton = GTK_BUTTON(gtk_button_new_with_mnemonic(
                                   _("_View Certificate")));
   gtk_container_add(GTK_CONTAINER(mActionArea), GTK_WIDGET(mViewCertButton));
   gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(mActionArea),
                                      GTK_WIDGET(mViewCertButton), true);
   g_object_add_weak_pointer(G_OBJECT(mViewCertButton),
                             (gpointer *)&mViewCertButton);
   g_signal_connect(mViewCertButton, "clicked", G_CALLBACK(OnViewCert), this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::CreateAlignment --
 *
 *      Create alignment for the fullscreen and set widgets to align in the
 *      center.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      mFullscreenAlign is initialed and added to fixed.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::CreateAlignment(GtkFixed *fixed) // IN
{
   ASSERT(fixed);
   ASSERT(!mFullscreenAlign);

   mFullscreenAlign = GTK_ALIGNMENT(gtk_alignment_new(0.5, 0.5, 0, 0));
   gtk_widget_show(GTK_WIDGET(mFullscreenAlign));
   gtk_fixed_put(fixed, GTK_WIDGET(mFullscreenAlign), 0, 0);
   g_object_add_weak_pointer(G_OBJECT(mFullscreenAlign),
                             (gpointer *)&mFullscreenAlign);
   DoSizeAllocate(&GTK_WIDGET(mWindow)->allocation);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::Show --
 *
 *      Show the window.
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
Window::Show()
{
#ifdef GDK_WINDOWING_X11
   // Set the window's _NET_WM_USER_TIME from an X server roundtrip.
   Util::OverrideWindowUserTime(mWindow);
#endif
   gtk_window_present(mWindow);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::SetContent --
 *
 *      Removes previous mDlg, if necessary, and puts the dialog's
 *      content in its place.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Window buttons will be reset to initial state.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::SetContent(Dlg *dlg) // IN
{
   bool hidden = false;
   ASSERT(dlg != mDlg);
   if (mDlg) {
      /*
       * If the new dialog is not a BrokerDlg, then that
       * indicates success and we need to save the users choices.
       */
      if (!dynamic_cast<BrokerDlg *>(dlg)) {
         mDlg->SavePrefs();
      }
#ifndef __MINGW32__
      if (dynamic_cast<DesktopDlg *>(mDlg)) {
         mDesktopUIExitCnx.disconnect();
         if (mDesktopHelper) {
            /*
             * Since this codepath can be called from a ProcHelper's onErr
             * handler, we need to actually delete the object in an idle
             * handler, but we kill it before hand.
             */
            mDesktopHelper->Kill();
            g_idle_add(OnIdleDeleteProcHelper, mDesktopHelper);
            mDesktopHelper = NULL;
         }
         Prefs::DesktopSize desktopSize =
            Prefs::GetPrefs()->GetDefaultDesktopSize();
         if (!GetFullscreen() && (desktopSize == Prefs::ALL_SCREENS ||
                                  desktopSize == Prefs::FULL_SCREEN)) {
            /*
             * This causes GtkWindow's centering logic to be reset, so
             * the window isn't in the bottom left corner after coming
             * out of full screen mode.
             */
            gtk_widget_hide(GTK_WIDGET(mWindow));
            hidden = true;
         }
      }
#endif
      delete mDlg;
   }
   mDlg = dlg;
   GtkWidget *content = mDlg->GetContent();
   gtk_widget_show(content);

#ifndef __MINGW32__
   if (dynamic_cast<DesktopDlg *>(mDlg)) {
      if (mContentBox) {
         GList *children =
            gtk_container_get_children(GTK_CONTAINER(mToplevelBox));
         for (GList *li = children; li; li = li->next) {
            if (GTK_WIDGET(li->data) != content) {
               gtk_widget_destroy(GTK_WIDGET(li->data));
            }
         }
         g_list_free(children);
         ASSERT(!mContentBox);
      }
      // The widget was added before rdesktop was launched.
      ASSERT(gtk_widget_get_parent(content) == GTK_WIDGET(mToplevelBox));
   } else {
      if (!mContentBox) {
         InitWindow();
      }
      gtk_box_pack_start(GTK_BOX(mContentBox), content, true, true, 0);
   }
#else
   if (!mContentBox) {
      InitWindow();
   }
   gtk_box_pack_start(GTK_BOX(mContentBox), content, true, true, 0);
#endif
   /*
    * From bora/apps/lib/lui/window.cc:
    *
    * Some window managers (Metacity in particular) refuse to go
    * fullscreen if the window is not resizable (i.e. if the window has
    * the max size hint set), which is reasonable. So we need to make the
    * window resizable first.  This happens in a few different places
    * throughout these transitions.
    *
    * In GTK+ 2.2 and 2.4, gtk_window_set_resizable() sets the internal
    * state to resizable, and then queues a resize. That ends up calling
    * the check_resize method of the window, which updates the window
    * manager hints according to the internal state. The bug is that this
    * update happens asynchronously.
    *
    * We want the update to happen now, so we workaround the issue by
    * synchronously calling the check_resize method of the window
    * ourselves.
    */
#ifndef __MINGW32__
   Prefs::DesktopSize size = Prefs::GetPrefs()->GetDefaultDesktopSize();
   DesktopDlg *desktopDlg = dynamic_cast<DesktopDlg *>(mDlg);

   if (!GetFullscreen()) {
      gtk_window_set_resizable(GTK_WINDOW(mWindow),
                               desktopDlg ? size == Prefs::ALL_SCREENS ||
                                            size == Prefs::FULL_SCREEN ||
                                            desktopDlg->GetResizable()
                                          : false);
      gtk_container_check_resize(GTK_CONTAINER(mWindow));
   }

   if (desktopDlg) {
      g_signal_handlers_disconnect_by_func(mWindow, (gpointer)OnKeyPress, this);
      if (size == Prefs::ALL_SCREENS || size == Prefs::FULL_SCREEN) {
         // XXX: This call may fail.  Should monitor the
         //      window_state_event signal, and either restart rdesktop
         //      if we exit fullscreen, or don't start it until we enter
         //      fullscreen.
         FullscreenWindow(mWindow, size == Prefs::ALL_SCREENS ? &mMonitorBounds
                                                              : NULL);
      }
   }
#endif
   UpdateForwardButton(mDlg->GetForwardEnabled(), mDlg->GetForwardVisible());
   mDlg->updateForwardButton.connect(boost::bind(&Window::UpdateForwardButton,
                                                 this, _1, _2));

   if (mCancelButton) {
      Util::string stockId = dynamic_cast<BrokerDlg *>(mDlg) ?
                                 GTK_STOCK_QUIT : GTK_STOCK_CANCEL;
      Util::SetButtonIcon(mCancelButton, stockId);
      UpdateCancelButton(mDlg->GetCancelable());
   }

   UpdateHelpButton(true, mDlg->GetHelpVisible());

   CertViewer *vCert = dynamic_cast<CertViewer *>(mDlg);
   if (vCert) {
      UpdateViewCertButton(vCert->GetCertificate() != NULL, true);
      vCert->enableViewCert.connect(
               boost::bind(&Window::UpdateViewCertButton, this, _1, true));
   } else {
      UpdateViewCertButton(false, false);
   }
   if (hidden) {
      Show();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::SetBusy --
 *
 *      Called when we are awaiting a response from the broker.
 *
 *      TBD: display the message.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Disables UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::SetBusy(const Util::string &message) // IN
{
   Log("Busy: %s\n", message.c_str());
   mDlg->SetSensitive(false);

   if (dynamic_cast<BrokerDlg *>(mDlg)) {
      Util::SetButtonIcon(mCancelButton, GTK_STOCK_CANCEL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::SetReady --
 *
 *      Called when we are awaiting input from the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Enables UI.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::SetReady()
{
   mDlg->SetSensitive(true);

   if (dynamic_cast<BrokerDlg *>(mDlg)) {
      Util::SetButtonIcon(mCancelButton, GTK_STOCK_QUIT);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::SetLogoutOnCertRemoval --
 *
 *     Called after a successful cert auth, if the server option is
 *     enabled.  We should log out if the smart card used to
 *     authenticate is removed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      We start polling for smart card events.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::SetLogoutOnCertRemoval(bool enabled) // IN
{
   if (enabled) {
      ASSERT(mAuthCert);
      StartWatchingForTokenEvents(ACTION_LOGOUT);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::UpdateDesktops --
 *
 *      If mDlg is a DesktopSelectDlg, updates its list.
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
Window::UpdateDesktops()
{
   // Only do this if we still have the DesktopSelectDlg around.
   DesktopSelectDlg *dlg = dynamic_cast<DesktopSelectDlg *>(mDlg);
   if (dlg) {
      dlg->UpdateList(mBroker->mDesktops);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestBroker --
 *
 *      Set up the broker connection dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Resets the broker state.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestBroker()
{
   static bool firstTimeThrough = true;
   Reset();
   BrokerDlg *brokerDlg = new BrokerDlg(Prefs::GetPrefs()->GetDefaultBroker());
   SetContent(brokerDlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Co_nnect"));

   // Hit the Connect button if broker was supplied and we're non-interactive.
   if (brokerDlg->IsValid() &&
       (Prefs::GetPrefs()->GetNonInteractive() ||
        (firstTimeThrough && Prefs::GetPrefs()->GetAutoConnect()))) {
      /*
       * Delay init to the main loop so that it happens outside the constructor.
       * DoInitialize is a virtual method.
       */
      g_idle_add(DelayedDoInitialize, this);
   } else {
      SetReady();
   }
   firstTimeThrough = false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestCertificate --
 *
 *      Get a user-selected certificate and private key for use in
 *      authenticating to the broker.
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
Window::RequestCertificate(std::list<Util::string> &trustedIssuers) // IN
{
   mTrustedIssuers = trustedIssuers;

   RequestCertificate();
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestInsertSmartCard --
 *
 *      Prompt the user to insert a smart card.
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
Window::RequestInsertSmartCard()
{
   ScInsertPromptDlg *dlg = new ScInsertPromptDlg(mCryptoki);
   SetContent(dlg);
   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestCertificate --
 *
 *      Attempt to request a certificate from the user.
 *
 *      If no slots are available, skip certificate auth.
 *
 *      If no tokens are available, prompt the user to insert a token.
 *
 *      If one cert is available, try to use that cert.
 *
 *      If multiple certs are available, prompt the user to select
 *      one.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Initializes mCryptoki.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestCertificate()
{
   if (!mCryptoki) {
      mCryptoki = new Cryptoki();
      mCryptoki->LoadModules(LIBDIR"/vmware/view/pkcs11");
   }

   if (!mCryptoki->GetHasSlots()) {
      SetBusy(_("Logging in..."));
      mBroker->SubmitCertificate();
   } else if (!mCryptoki->GetHasTokens()) {
      RequestInsertSmartCard();
   } else {
      Cryptoki::FreeCertificates(mCertificates);
      mAuthCert = NULL;

      mCertificates = mCryptoki->GetCertificates(mTrustedIssuers);

      switch (mCertificates.size()) {
      case 0:
         SetBusy(_("Logging in..."));
         mBroker->SubmitCertificate();
         break;
      case 1:
         mAuthCert = mCertificates.front();
         DoAttemptSubmitCertificate();
         break;
      default: {
         ScCertDlg *dlg = new ScCertDlg();
         SetContent(dlg);
         Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Co_nnect"));
         dlg->SetCertificates(mCertificates);
         SetReady();
         StartWatchingForTokenEvents(ACTION_REQUEST_CERTIFICATE);
         break;
      }
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestPrivateKey --
 *
 *      Prompt the user to enter their PIN.
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
Window::RequestPrivateKey()
{
   ScPinDlg *dlg = new ScPinDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Co_nnect"));
   dlg->SetTokenName(mCryptoki->GetTokenName(mAuthCert));
   dlg->SetCertificate(mAuthCert);
   SetReady();
   StartWatchingForTokenEvents(ACTION_REQUEST_CERTIFICATE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestDisclaimer --
 *
 *      Sets up the given DisclaimerDlg to accept/cancel the disclaimer.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Disclaimer page is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestDisclaimer(const Util::string &disclaimer) // IN
{
   DisclaimerDlg *dlg = new DisclaimerDlg();
   SetContent(dlg);
   dlg->SetText(disclaimer);
   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestPasscode --
 *
 *      Prompt the user for their username and passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Passcode page is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestPasscode(const Util::string &username, // IN
                        bool userSelectable)          // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("_Authenticate"));
   dlg->SetState(SecurIDDlg::STATE_PASSCODE, username, userSelectable);
   dlg->authenticate.connect(boost::bind(&Window::DoSubmitPasscode, this));
   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestNextTokencode --
 *
 *      Prompt the user for their next tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tokencode page is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestNextTokencode(const Util::string &username) // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("_Authenticate"));
   dlg->SetState(SecurIDDlg::STATE_NEXT_TOKEN, username);
   dlg->authenticate.connect(boost::bind(&Window::DoSubmitNextTokencode, this));
   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestPinChange --
 *
 *      Prompt the user for a new PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Pin change dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestPinChange(const Util::string &pin,     // IN
                         const Util::string &message, // IN
                         bool userSelectable)         // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("_Authenticate"));
   dlg->SetState(SecurIDDlg::STATE_SET_PIN, pin, userSelectable, message);
   dlg->authenticate.connect(boost::bind(&Window::DoSubmitPins, this));
   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestPassword --
 *
 *      Prompt the user for their password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestPassword(const Util::string &username,             // IN
                     bool readOnly,                            // IN
                     const std::vector<Util::string> &domains, // IN
                     const Util::string &suggestedDomain)      // IN
{
   Prefs *prefs = Prefs::GetPrefs();

   LoginDlg *dlg = new LoginDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("_Login"));

   /*
    * Turn off non-interactive mode if the suggested username differs from
    * the one passed on the command line. We want to use the username
    * returned by the server, but should let the user change it before
    * attempting to authenticate.
    */
   if (prefs->GetNonInteractive() && username != prefs->GetDefaultUser()) {
      prefs->SetNonInteractive(false);
   }

   /*
    * Try to find the suggested domain in the list returned by the server.
    * If it's found, use it. If it's not and it was passed via the command
    * line, show a warning. Use the pref if it's in the list. If all else
    * fails, use the first domain in the list. Only go non-interactive if
    * the domain  was given on the command line and it was found, or if
    * there's only one domain in the list.
    */
   Util::string domain = "";
   bool domainFound = false;
   Util::string domainPref = prefs->GetDefaultDomain();
   for (std::vector<Util::string>::const_iterator i = domains.begin();
        i != domains.end(); i++) {
      if (Util::Utf8Casecmp(i->c_str(), suggestedDomain.c_str()) == 0) {
         // Use value in the list so the case matches.
         domain = *i;
         domainFound = true;
         break;
      } else if (Util::Utf8Casecmp(i->c_str(), domainPref.c_str()) == 0) {
         domain = *i;
      }
   }

   if (domain.empty() && domains.size() > 0) {
      domain = domains[0];
   }

   const char *password = prefs->GetPassword();
   dlg->SetFields(username, readOnly, password ? password : "",
                  domains, domain);
   prefs->ClearPassword();
   if (prefs->GetNonInteractive() && dlg->IsValid()) {
      ForwardHandler();
   } else {
      SetReady();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestPasswordChange --
 *
 *      Prompt the user for a new password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password change dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestPasswordChange(const Util::string &username, // IN
                           const Util::string &domain)   // IN
{
   PasswordDlg *dlg = new PasswordDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Ch_ange"));

   // Domain is locked, so just create a vector with it as the only value.
   std::vector<Util::string> domains;
   domains.push_back(domain);

   dlg->SetFields(username, true, "", domains, domain);
   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestDesktop --
 *
 *      Prompt the user for a desktop with which to connect.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop selection dialog is visible.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestDesktop()
{
   Util::string defaultDesktop = Prefs::GetPrefs()->GetDefaultDesktop();
   Util::string initialDesktop = "";
   Util::string defaultProto = Prefs::GetPrefs()->GetDefaultProtocol();
   /*
    * Iterate through desktops. If the passed-in desktop name is found,
    * pass it as initially-selected. Otherwise use a desktop with the
    * "alwaysConnect" user preference.
    */
   for (std::vector<Desktop *>::iterator i = mBroker->mDesktops.begin();
        i != mBroker->mDesktops.end(); i++) {
      Util::string name = (*i)->GetName();
      if (name == defaultDesktop) {
         initialDesktop = defaultDesktop;
      } else if (initialDesktop.empty() && (*i)->GetAutoConnect()) {
         initialDesktop = name;
      }
      if (!defaultProto.empty()) {
         (*i)->SetProtocol(defaultProto);
      }
   }

   int monitors = gdk_screen_get_n_monitors(gtk_window_get_screen(mWindow));
   Log("Number of monitors on this screen is %d.\n", monitors);

#ifdef GDK_WINDOWING_X11
   GdkAtom atom = gdk_atom_intern("_NET_WM_FULLSCREEN_MONITORS", FALSE);
   bool supported = atom != GDK_NONE && gdk_net_wm_supports(atom);
#else
   bool supported = false;
#endif

   Log("Current window manager %s _NET_WM_FULLSCREEN_MONITORS message.\n",
       supported ? "supports" : "does not support");

   DesktopSelectDlg *dlg = CreateDesktopSelectDlg(mBroker->mDesktops,
                                                  initialDesktop,
                                                  monitors > 1 && supported,
                                                  !GetFullscreen());
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("C_onnect"));
   dlg->action.connect(boost::bind(&Window::DoDesktopAction, this, _1));

   if (Prefs::GetPrefs()->GetNonInteractive() &&
       (!initialDesktop.empty() ||
        (mBroker->mDesktops.size() == 1 && dlg->GetDesktop()->CanConnect()))) {
      ForwardHandler();
   } else {
      SetReady();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestTransition --
 *
 *      Show the transition dialog; a message with a spinner.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Transition dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::RequestTransition(const Util::string &message, // IN
                          bool useMarkup)              // IN/OPT
{
   Log("Transitioning: %s\n", message.c_str());
   TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_PROGRESS,
                                          message, useMarkup);

   std::vector<GdkPixbuf *> pixbufs = TransitionDlg::LoadAnimation(
      -1, spinner_anim, false, SPINNER_ANIM_N_FRAMES);
   dlg->SetAnimation(pixbufs, SPINNER_ANIM_FPS_RATE);
   for (std::vector<GdkPixbuf *>::iterator i = pixbufs.begin();
        i != pixbufs.end(); i++) {
      g_object_unref(*i);
   }

   SetContent(dlg);

   gtk_widget_hide(GTK_WIDGET(mForwardButton));

   SetReady();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::SetCookieFile --
 *
 *      This sets the cookie file for our broker object based on the
 *      passed-in URL.  We base64-encode that url, and append that to
 *      our base cookie file.  This lets us use a separate cookie file
 *      per-broker, to allow multiple instances to work.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Cookie file is created and set on mBroker.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::SetCookieFile(const Util::string &brokerUrl) // IN
{
   char *encUrl = NULL;
   Util::string tmpName;

#ifdef _WIN32
   tmpName = getenv("USERPROFILE");
   if (tmpName.empty()) {
      /* XXX - if USERPROFILE is not defined, is leaving the cookie in
       * the user's home directory acceptable or do we need a containing
       * directory in which to store it?
       */
      tmpName = ".";
   }
   tmpName += "\\";
   tmpName += COOKIE_FILE_NAME;
#else
   tmpName = COOKIE_FILE_NAME;
#endif

   if (Base64_EasyEncode((const uint8 *)brokerUrl.c_str(), brokerUrl.size(), &encUrl)) {
      tmpName += ".";
      tmpName += encUrl;
      free(encUrl);
   } else {
      Log("Failed to b64-encode url: %s; using default cookie file.\n", brokerUrl.c_str());
   }
   char *cookieFile = Util_ExpandString(tmpName.c_str());
   if (cookieFile && *cookieFile &&
       Util::EnsureFilePermissions(cookieFile, COOKIE_FILE_MODE)) {
      mBroker->SetCookieFile(cookieFile);
   }
   free(cookieFile);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoInitialize --
 *
 *      Handle a Connect button click in the broker entry control.  Invoke the
 *      async broker Initialize.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::Initialize().
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoInitialize()
{
   ASSERT(mDlg);

   BrokerDlg *brokerDlg = dynamic_cast<BrokerDlg *>(mDlg);
   ASSERT(brokerDlg);
   if (brokerDlg->GetBroker().empty()) {
      return;
   }
   Prefs *prefs = Prefs::GetPrefs();

   SetBroker(new Broker());

   SetCookieFile(Util::GetHostLabel(brokerDlg->GetBroker(),
                                    brokerDlg->GetPort(),
                                    brokerDlg->GetSecure()));

   InitializeProtocols();

   SetBusy(_("Initializing connection..."));
   mBroker->Initialize(brokerDlg->GetBroker(), brokerDlg->GetPort(),
                       brokerDlg->GetSecure(),
                       prefs->GetDefaultUser(),
                       // We'll use the domain pref later if need be.
                       "");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::InitializeProtocols --
 *
 *      Determine available remoting protocols and initialize accordingly.
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
Window::InitializeProtocols()
{
   std::vector<Util::string> protocols;
#ifdef __MINGW32__
   if (Mstsc::GetIsProtocolAvailable()) {
        protocols.push_back(Protocols::GetName(Protocols::RDP));
   }
#else
   if (RDesktop::GetIsProtocolAvailable()) {
        protocols.push_back(Protocols::GetName(Protocols::RDP));
   }
   if (RMks::GetIsProtocolAvailable()) {
        protocols.push_back(Protocols::GetName(Protocols::PCOIP));
   }
#endif
   mBroker->SetSupportedProtocols(protocols);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DelayedDoInitialize --
 *
 *      Marshaller to allow DoInitialize to be called from the main loop.
 *
 * Results:
 *      Always FALSE to indicate callback is not periodic.
 *
 * Side effects:
 *      See Window::DoInitialize().
 *
 *-----------------------------------------------------------------------------
 */

gboolean
Window::DelayedDoInitialize(void *data) // IN:
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);
   that->DoInitialize();
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::ForwardHandler --
 *
 *      Handler for a forward button click.  Determines the type of mDlg
 *      and calls the appropriate function.
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
Window::ForwardHandler()
{
   SecurIDDlg *securIdDlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ScCertDlg *certDlg = dynamic_cast<ScCertDlg *>(mDlg);
   ScPinDlg *pinDlg = dynamic_cast<ScPinDlg *>(mDlg);

   if (dynamic_cast<BrokerDlg *>(mDlg)) {
      DoInitialize();
   } else if (dynamic_cast<DisclaimerDlg *>(mDlg)) {
      ASSERT(mBroker);
      SetBusy(_("Accepting disclaimer..."));
      mBroker->AcceptDisclaimer();
   } else if (dynamic_cast<ScInsertPromptDlg *>(mDlg)) {
      RequestCertificate();
   } else if (certDlg) {
      StopWatchingForTokenEvents();
      /*
       * We own the reference to this cert, so it's OK to discard
       * const.
       */
      mAuthCert = (X509 *)certDlg->GetCertificate();
      DoAttemptSubmitCertificate();
   } else if (pinDlg) {
      StopWatchingForTokenEvents();
      DoAttemptSubmitCertificate(pinDlg->GetPin());
   } else if (securIdDlg) {
      securIdDlg->authenticate();
   } else if (dynamic_cast<PasswordDlg *>(mDlg)) {
      DoChangePassword();
   } else if (dynamic_cast<LoginDlg *>(mDlg)) {
      DoSubmitPassword();
   } else if (dynamic_cast<DesktopSelectDlg *>(mDlg)) {
      DoDesktopAction(DesktopSelectDlg::ACTION_CONNECT);
   } else if (dynamic_cast<TransitionDlg *>(mDlg)) {
      ASSERT(mBroker);
      SetBusy(_("Reconnecting to desktop..."));
      mBroker->ReconnectDesktop();
   } else {
      NOT_IMPLEMENTED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoAttemptSubmitCertificate --
 *
 *      Try to submit a certificate for authentication.
 *
 *      If it needs authentication, prompt the user.
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
Window::DoAttemptSubmitCertificate(const char *pin) // IN/OPT
{
   GError *err = NULL;
   if (mCryptoki->Login(mAuthCert, pin, &err)) {
      SetBusy(_("Logging in..."));
      mBroker->SubmitCertificate(X509_dup(mAuthCert),
                                 mCryptoki->GetPrivateKey(mAuthCert),
                                 pin,
                                 mCryptoki->GetSlotName(mAuthCert));
      return;
   }

   ASSERT(err->domain == CDK_CRYPTOKI_ERROR);
   if (err->code == Cryptoki::ERR_DEVICE_REMOVED) {
      RequestInsertSmartCard();
   } else {
      /*
       * If the pin for the cert given is locked, there isn't much use to
       * showing the user the pin dialog again, so just move on.
       */
      if (pin && err->code == Cryptoki::ERR_PIN_LOCKED) {
         SetBusy(_("Logging in..."));
         mBroker->SubmitCertificate();
      } else {
         RequestPrivateKey();
      }
      /*
       * We want to give a final pin warning, but otherwise we
       * don't want to show anything if the user hasn't tried to
       * log in yet.
       */
      if (!pin && err->code == Cryptoki::ERR_PIN_FINAL_TRY) {
         BaseApp::ShowWarning(_("PIN Warning"), "%s", err->message);
      } else if (pin) {
         BaseApp::ShowError(CDK_ERR_PIN_ERROR, _("PIN Error"), "%s", err->message);
      }
   }
   g_error_free(err);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoSubmitPasscode --
 *
 *      Attempt to authenticate using a username and passcode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitPasscode().
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoSubmitPasscode()
{
   ASSERT(mBroker);
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);

   Util::string user = dlg->GetUsername();
   Prefs::GetPrefs()->SetDefaultUser(user);

   SetBusy(_("Submitting passcode..."));
   mBroker->SubmitPasscode(user, dlg->GetPasscode());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoSubmitNextTokencode --
 *
 *      Continues authentication using a tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitNextTokencode().
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoSubmitNextTokencode()
{
   ASSERT(mBroker);
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);
   SetBusy(_("Submitting tokencode..."));
   mBroker->SubmitNextTokencode(dlg->GetPasscode());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoSubmitPins --
 *
 *      Continue authentication by submitting new PINs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitPins().
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoSubmitPins()
{
   ASSERT(mBroker);
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);
   // The char * in this pair are zeroed by GTK when the text entry deconstructs.
   std::pair<const char *, const char *> pins = dlg->GetPins();
   if (strcmp(pins.first, pins.second)) {
      BaseApp::ShowError(CDK_ERR_PIN_MISMATCH,
                         _("PIN Mismatch"),
                         _("The PINs you entered do not match. "
                           "Please try again."));
   } else {
      SetBusy(_("Changing PIN..."));
      mBroker->SubmitPins(pins.first, pins.second);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoSubmitPassword --
 *
 *      Authenticate using a username and password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::SubmitPassword()
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoSubmitPassword()
{
   ASSERT(mBroker);
   LoginDlg *dlg = dynamic_cast<LoginDlg *>(mDlg);
   ASSERT(dlg);

   Util::string user = dlg->GetUsername();
   Util::string domain = dlg->GetDomain();

   Prefs *prefs = Prefs::GetPrefs();
   /*
    * Don't save the user name if it's read only, since the user name would have
    * been given to us by the View Connection Server.
    */
   if (!dlg->GetIsUserReadOnly()) {
      prefs->SetDefaultUser(user);
   }
   prefs->SetDefaultDomain(domain);

   SetBusy(_("Logging in..."));
   mBroker->SubmitPassword(user, dlg->GetPassword(), domain);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoChangePassword --
 *
 *      Continue authentication by choosing a new password.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      See Broker::ChangePassword()
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoChangePassword()
{
   ASSERT(mBroker);
   PasswordDlg *dlg = dynamic_cast<PasswordDlg *>(mDlg);
   ASSERT(dlg);
   // The char * in this pair are zeroed by GTK when the text entry deconstructs.
   std::pair<const char *, const char *> pwords = dlg->GetNewPassword();
   if (Str_Strcmp(pwords.first, pwords.second)) {
      BaseApp::ShowError(CDK_ERR_PASSWORD_MISMATCH,
                         _("Password Mismatch"),
                         _("The passwords you entered do not match. Please "
                           "try again."));
   } else {
      SetBusy(_("Changing password..."));
      mBroker->ChangePassword(dlg->GetPassword(), pwords.first, pwords.second);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoDesktopAction --
 *
 *      Initiates the indicated action on the selected desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May send RPC requests; see Broker functions.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::DoDesktopAction(DesktopSelectDlg::Action action) // IN
{
   ASSERT(mBroker);
   DesktopSelectDlg *dlg = dynamic_cast<DesktopSelectDlg *>(mDlg);
   ASSERT(dlg);
   Desktop *desktop = dlg->GetDesktop();
   if (!desktop) {
      Log("Window::DoDesktopAction: Selected desktop is NULL "
          "(see bz 528113).  Action attempted = %d.\n", action);
      ASSERT_BUG(528113, desktop);
   }

   switch (action) {
   case DesktopSelectDlg::ACTION_CONNECT:
      DoDesktopConnect(desktop);
      break;
   case DesktopSelectDlg::ACTION_RESET:
      mBroker->ResetDesktop(desktop);
      break;
   case DesktopSelectDlg::ACTION_KILL_SESSION:
      mBroker->KillSession(desktop);
      break;
   case DesktopSelectDlg::ACTION_ROLLBACK:
      mBroker->RollbackDesktop(desktop);
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::TunnelDisconnected --
 *
 *      Tunnel onDisconnect signal handler.  Shows an error dialog to the user.
 *      Clicking 'Ok' in the dialog destroys mWindow, which quits the client.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      mWindow is insensitive.
 *      We will not exit if rdesktop exits.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::TunnelDisconnected(Util::string disconnectReason) // IN
{
   /*
    * rdesktop will probably exit shortly, and we want the user to see
    * our dialog before we exit
    */
   mDesktopUIExitCnx.disconnect();
   delete mDesktopHelper;
   mDesktopHelper = NULL;

   ASSERT(mBroker);
   Util::string message;
   if (mBroker->GetSecure()) {
      message = _("The secure connection to the View Server has"
                  " unexpectedly disconnected.");
   } else {
      message = _("The connection to the View Server has"
                  " unexpectedly disconnected.");
   }

   if (!disconnectReason.empty()) {
      message += Util::Format(_("\n\nReason: %s."), _(disconnectReason.c_str()));
   }

   BaseApp::ShowError(CDK_ERR_TUNNEL_DISCONNECTED,
                      _("Tunnel Disconnected"), "%s", message.c_str());
   /*
    * If the tunnel really exited, it's probably not going to let us
    * get a new one until we log in again.  If we're at the desktop
    * selection page, that means we should restart.
    */
   if (!dynamic_cast<TransitionDlg *>(mDlg)) {
      RequestBroker();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnSizeAllocate --
 *
 *      Callback for window size change.
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
Window::OnSizeAllocate(GtkWidget *widget,         // IN/UNUSED
                    GtkAllocation *allocation, // IN
                    gpointer userData)         // IN
{
   Window *that = reinterpret_cast<Window *>(userData);
   ASSERT(that);
   that->DoSizeAllocate(allocation);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::DoSizeAllocate --
 *
 *      Helper method.  Resize the GtkAlignment to fill available space, and
 *      possibly the background image as well.
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
Window::DoSizeAllocate(GtkAllocation *allocation) // IN
{
   ASSERT(allocation);

   if (mFullscreenAlign) {
      /*
       * This really does need to be a _set_size_request(), and not
       * _size_allocate(), otherwise there is some resize flickering
       * at startup (and quitting, if that happens).
       */
      gtk_widget_set_size_request(GTK_WIDGET(mFullscreenAlign),
                                  allocation->width, allocation->height);
   }
#ifndef __MINGW32__
   if (dynamic_cast<DesktopDlg *>(mDlg)) {
      gtk_widget_size_allocate(mDlg->GetContent(), allocation);
   }
#endif
   if (mBackgroundImage) {
      ResizeBackground(allocation);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::CreateBanner --
 *
 *      Create a GtkImage containing the logo banner.
 *
 * Results:
 *      A GtkImage.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkWidget *
Window::CreateBanner()
{
   GdkPixbuf *pb = NULL;
   Util::string logo = Prefs::GetPrefs()->GetCustomLogo();
   if (!logo.empty()) {
      GError *error = NULL;
      pb = gdk_pixbuf_new_from_file(logo.c_str(), &error);
      if (error) {
         Util::UserWarning(_("Unable to load image '%s': %s\n"),
                           logo.c_str(), error->message);
         // Fall back to the default banner if the custom one can't be loaded.
         logo = "";
         g_error_free(error);
      }
   }
   // If no custom logo specified (or loading failed), use the default.
   if (logo.empty()) {
      const guint8 *banner = view_client_banner;
#ifdef HAVE_PCOIP_BANNER
      if (RMks::GetIsProtocolAvailable()) {
         banner = view_client_banner_pcoip;
      }
#endif
      pb = gdk_pixbuf_new_from_inline(-1, banner, false, NULL);
   }
   ASSERT(pb);

   // Scale the banner to BANNER_HEIGHT.
   int width = gdk_pixbuf_get_width(pb);
   int height = gdk_pixbuf_get_height(pb);
   if (height > BANNER_HEIGHT) {
      int newWidth = BANNER_HEIGHT * width / height;
      GdkPixbuf *scaledPb = gdk_pixbuf_scale_simple(pb,
                                                    newWidth,
                                                    BANNER_HEIGHT,
                                                    GDK_INTERP_BILINEAR);
      g_object_unref(pb);
      pb = scaledPb;
   }

   GtkImage *img = GTK_IMAGE(gtk_image_new_from_pixbuf(pb));
   g_object_unref(pb);

   GtkWidget *ret = NULL;
   if (!logo.empty()) {
      gtk_widget_show(GTK_WIDGET(img));
      gtk_misc_set_alignment(GTK_MISC(img), 0.5, 0.5);

      GtkEventBox *box = GTK_EVENT_BOX(gtk_event_box_new());
      GdkColor white;
      gdk_color_parse("white", &white);
      gtk_widget_modify_bg(GTK_WIDGET(box), GTK_STATE_NORMAL, &white);

      gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(img));

      ret = GTK_WIDGET(box);
   } else {
      gtk_misc_set_alignment(GTK_MISC(img), 0.0, 0.5);
      g_signal_connect(img, "size-allocate",
                     G_CALLBACK(&Window::OnBannerSizeAllocate), NULL);
      ret = GTK_WIDGET(img);
   }
   gtk_widget_set_size_request(ret, BANNER_MIN_WIDTH, -1);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnBannerSizeAllocate --
 *
 *      If the GtkImage is resized larger than its pixbuf, stretch it
 *      out by copying the last column of pixels.
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
Window::OnBannerSizeAllocate(GtkWidget *image,          // IN
                          GtkAllocation *allocation, // IN
                          gpointer userData)         // IN/UNUSED
{
   GdkPixbuf *pb;
   g_object_get(image, "pixbuf", &pb, NULL);
   if (!pb) {
      Log("No pixbuf for image, can't resize it.");
      return;
   }
   int old_width = gdk_pixbuf_get_width(pb);
   if (allocation->width <= old_width) {
      g_object_unref(pb);
      return;
   }
   GdkPixbuf *newPb = gdk_pixbuf_new(gdk_pixbuf_get_colorspace(pb),
                                     gdk_pixbuf_get_has_alpha(pb),
                                     gdk_pixbuf_get_bits_per_sample(pb),
                                     allocation->width,
                                     gdk_pixbuf_get_height(pb));
   gdk_pixbuf_copy_area(pb, 0, 0, gdk_pixbuf_get_width(pb),
                        gdk_pixbuf_get_height(pb), newPb, 0, 0);
   int old_height = gdk_pixbuf_get_height(pb);
   for (int y = old_width; y < allocation->width; y++) {
      gdk_pixbuf_copy_area(pb, old_width - 1, 0, 1, old_height, newPb, y, 0);
   }
   g_object_set(image, "pixbuf", newPb, NULL);
   g_object_unref(pb);
   g_object_unref(newPb);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::ResizeBackground --
 *
 *      Load and scale the background to fill the screen, maintaining
 *      aspect ratio.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop has a nice background image.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::ResizeBackground(GtkAllocation *allocation) // IN
{
   ASSERT(mBackgroundImage);

   if (allocation->width <= 1 || allocation->height <= 1) {
      return;
   }

   GdkPixbuf *pixbuf;
   g_object_get(G_OBJECT(mBackgroundImage), "pixbuf", &pixbuf, NULL);
   if (pixbuf &&
       gdk_pixbuf_get_width(pixbuf) == allocation->width &&
       gdk_pixbuf_get_height(pixbuf) == allocation->height) {
      gdk_pixbuf_unref(pixbuf);
      return;
   }
   if (pixbuf) {
      gdk_pixbuf_unref(pixbuf);
   }
   GError *error = NULL;
   pixbuf = gdk_pixbuf_new_from_file_at_size(
      Prefs::GetPrefs()->GetBackground().c_str(),
      -1, allocation->height, &error);

   if (error) {
      Util::UserWarning(_("Unable to load background image '%s': %s\n"),
                        Prefs::GetPrefs()->GetBackground().c_str(),
                        error->message);
      g_error_free(error);
      return;
   }
   if (gdk_pixbuf_get_width(pixbuf) < allocation->width) {
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
         pixbuf,
         allocation->width,
         allocation->height * allocation->width / gdk_pixbuf_get_width(pixbuf),
         GDK_INTERP_BILINEAR);
      gdk_pixbuf_unref(pixbuf);
      pixbuf = scaled;
   }
   GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(
      pixbuf,
      (gdk_pixbuf_get_width(pixbuf) - allocation->width) / 2,
      (gdk_pixbuf_get_height(pixbuf) - allocation->height) / 2,
      allocation->width,
      allocation->height);
   gdk_pixbuf_unref(pixbuf);
   g_object_set(G_OBJECT(mBackgroundImage), "pixbuf", sub, NULL);
   gdk_pixbuf_unref(sub);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::RequestLaunchDesktop --
 *
 *      Starts an rdesktop session and embeds it into the main window, and
 *      causes the main window to enter fullscreen.
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
Window::RequestLaunchDesktop(Desktop *desktop) // IN
{
   ASSERT(desktop);
   ASSERT(!mDesktopHelper);

   SetReady();
   Log("Desktop connect successful.  Starting desktop using %s...\n",
       desktop->GetProtocol().c_str());
   if (Prefs::GetPrefs()->GetNonInteractive()) {
      Log("Disabling non-interactive mode.\n");
      Prefs::GetPrefs()->SetNonInteractive(false);
   }

   RequestTransition(_("Connecting to the desktop..."));

   Dlg *dlg = NULL;
   switch (Protocols::GetProtocolFromName(desktop->GetProtocol())) {
   case Protocols::RDP:
#ifdef __MINGW32__
      mDesktopHelper = new Mstsc();
#else
      mDesktopHelper = new RDesktop();
      dlg = new DesktopDlg(mDesktopHelper, Prefs::GetPrefs()->GetAllowWMBindings());
#endif
      break;
#ifndef __MINGW32__
   case Protocols::PCOIP:
      {
         /*
          * Since we don't tunnel PCOIP, there may be cases where the
          * desktop is not routeable from the client w/o the tunnel.
          *
          * This lets us display an error to the user suggesting they
          * try a different protocol.
          */
         bool tunneledRdpAvailable = false;
         if (mBroker->GetIsUsingTunnel()) {
            std::vector<Util::string> protos = desktop->GetProtocols();
            for (std::vector<Util::string>::iterator i = protos.begin();
                 i != protos.end(); ++i) {
               if (Protocols::GetProtocolFromName(*i) != Protocols::PCOIP) {
                  tunneledRdpAvailable = true;
                  break;
               }
            }
         }
         mDesktopHelper = new RMks(tunneledRdpAvailable);
         /*
          * Pass true to desktopDlg so it won't grab the keyboard
          * (remotemks grabs the keyboard for itself.)
          */
         dlg = new DesktopDlg(mDesktopHelper, true);
         dynamic_cast<DesktopDlg *>(dlg)->SetResizable(true);
         break;
      }
#endif
   default:
      NOT_REACHED();
      break;
   }

   if (dlg) {
      gtk_box_pack_start(GTK_BOX(mToplevelBox), dlg->GetContent(), false, false,
                         0);
      gtk_widget_realize(dlg->GetContent());
   }

   /*
    * Once the desktop connects, set it as the content dlg.
    */
#ifndef __MINGW32__
   DesktopDlg *deskDlg = dynamic_cast<DesktopDlg *>(dlg);
   if (deskDlg) {
      deskDlg->onConnect.connect(boost::bind(&Window::SetContent, this, dlg));
      deskDlg->onCtrlAltDel.connect(boost::bind(&Window::OnCtrlAltDel, this));
   }
#endif

   /*
    * Handle desktop exit by restarting it, quitting, or showing a
    * warning dialog.
    */
   mDesktopUIExitCnx = mDesktopHelper->onExit.connect(
      boost::bind(&Window::OnDesktopUIExit, this, dlg, _1));

   Util::Rect geometry;
   Prefs::DesktopSize desktopSize = Prefs::GetPrefs()->GetDefaultDesktopSize();
   if (GetFullscreen()) {
      switch (desktopSize) {
      case Prefs::ALL_SCREENS:
      case Prefs::FULL_SCREEN:
         break;
      default:
         NOT_REACHED();
         desktopSize = Prefs::FULL_SCREEN;
         break;
      }
   }
   GetFullscreenGeometry(
      desktopSize == Prefs::ALL_SCREENS, &geometry,
      desktopSize == Prefs::ALL_SCREENS ? &mMonitorBounds : NULL);

   /*
    * "Large" and "Small" aren't actually defined by the spec, so
    * we're free to choose whatever size we feel is appropriate.  A
    * quarter of the screen (by area) is decently small, and 3/4ths of
    * the dimensions is half-way between small and full screen, so
    * these seem decent enough.
    */
   switch (desktopSize) {
   case Prefs::LARGE_WINDOW:
      geometry.width = (int)(geometry.width * .75);
      geometry.height = (int)(geometry.height * .75);
      break;
   case Prefs::SMALL_WINDOW:
      geometry.width = (int)(geometry.width * 0.5);
      geometry.height = (int)(geometry.height * 0.5);
      break;
   case Prefs::CUSTOM_SIZE:
      Prefs::GetPrefs()->GetDefaultCustomDesktopSize(&geometry);
      break;
   default:
      break;
   }

   geometry.width = MAX(geometry.width, 640);
   geometry.height = MAX(geometry.height, 480);
   Log("Connecting to desktop with total geometry %dx%d.\n",
       geometry.width, geometry.height);

#ifdef __MINGW32__
   Mstsc *mstsc = dynamic_cast<Mstsc *>(mDesktopHelper);
   ASSERT(mstsc);
#else
   deskDlg->SetInitialDesktopSize(geometry.width, geometry.height);
   RMks *rmks = dynamic_cast<RMks *>(mDesktopHelper);
   RDesktop *rdesktop = dynamic_cast<RDesktop *>(mDesktopHelper);
   ASSERT(!rmks || !rdesktop);
#endif

   PushDesktopEnvironment();

   if (desktop->GetIsUSBEnabled()) {
      desktop->StartUsb();
   }

   const BrokerXml::DesktopConnection &conn = desktop->GetConnection();
   Log("Connecting to desktop %s: %s://%s@%s:%d\n",
       conn.id.c_str(), conn.protocol.c_str(), conn.username.c_str(),
       conn.address.c_str(), conn.port);

#ifdef __MINGW32__
   if (mstsc) {
      mstsc->Start(conn, &geometry,
                   gtk_widget_get_screen(GTK_WIDGET(mWindow)));
      /*
       * XXX - would prefer to only hide the window if we know the
       * mstsc has successfully launched; otherwise, we would cancel
       * the current dialog and revert back to the desktop select
       * dialog.
       */
      Hide();
   } else {
#else
   if (rdesktop) {
      rdesktop->Start(conn, deskDlg->GetWindowId(), &geometry,
                      mBroker->GetDesktop()->GetIsMMREnabled(),
                      GetSmartCardRedirects(),
                      gtk_widget_get_screen(GTK_WIDGET(mWindow)));
      deskDlg->SetInhibitCtrlEnter(true);
      deskDlg->SetSendCADXMessage(false);
   } else if (rmks) {
      rmks->Start(conn, deskDlg->GetWindowId(), &geometry,
                  gtk_widget_get_screen(GTK_WIDGET(mWindow)));
      deskDlg->SetInhibitCtrlEnter(false);
      deskDlg->SetSendCADXMessage(true);
   } else {
#endif
      NOT_REACHED();
   }

   PopDesktopEnvironment();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::GetFullscreenGeometry --
 *
 *      If allMonitors is true,
 *      computes the rectangle that is the union of all monitors.
 *      Otherwise computes the rectangle of the current monitor.
 *      If allMonitors is true and bounds is non-NULL,
 *      determines the appropriate monitor indices for
 *      sending the _NET_WM_FULLSCREEN_MONITORS message.
 *
 * Results:
 *      geometry: Util::Rect representing union of all monitors.
 *      bounds: MonitorBounds containing arguments to
 *      _NET_WM_FULLSCREEN_MONITORS message.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Window::GetFullscreenGeometry(bool allMonitors,      // IN
                           Util::Rect *geometry,  // OUT
                           MonitorBounds *bounds) // OUT/OPT
{
   GdkScreen *screen = gtk_window_get_screen(mWindow);

   if (allMonitors) {
      int numMonitors = gdk_screen_get_n_monitors(screen);

      if (bounds) {
         bounds->top = 0;
         bounds->bottom = 0;
         bounds->left = 0;
         bounds->right = 0;
      }

      int index = 0;
      bool invalid = true;

      while (invalid && index < numMonitors) {
         gdk_screen_get_monitor_geometry(screen, index++, geometry);
         invalid = (geometry->width <= 0 || geometry->height <= 0);
      }
      if (invalid) {
         NOT_REACHED();
         Warning("No valid screen found.\n");
         return;
      }

      int minX = geometry->x;
      int maxX = geometry->x + geometry->width;
      int minY = geometry->y;
      int maxY = geometry->y + geometry->height;
      for (int i = index; i < numMonitors; ++i) {
         GdkRectangle nextMonitor;
         gdk_screen_get_monitor_geometry(screen, i, &nextMonitor);

         if (nextMonitor.width <= 0 || nextMonitor.height <= 0) {
            continue;
         }

         gdk_rectangle_union(geometry, &nextMonitor, geometry);

         if (bounds) {
            if (nextMonitor.y < minY) {
               bounds->top = i;
               minY = nextMonitor.y;
            }
            if (nextMonitor.y + nextMonitor.height > maxY) {
               bounds->bottom = i;
               maxY = nextMonitor.y + nextMonitor.height;
            }
            if (nextMonitor.x < minX) {
               bounds->left = i;
               minX = nextMonitor.x;
            }
            if (nextMonitor.x + nextMonitor.width > maxX) {
               bounds->right = i;
               maxX = nextMonitor.x + nextMonitor.width;
            }
         }
      }
   } else {
      gdk_screen_get_monitor_geometry(
         screen,
         gdk_screen_get_monitor_at_window(screen, GTK_WIDGET(mWindow)->window),
         geometry);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::FullscreenWindow --
 *
 *      Checks if the window manager supports fullscreen, then either calls
 *      gtk_window_fullscreen() or manually sets the size and position of
 *      mWindow. If bounds is non-NULL, sends the _NET_WM_FULLSCREEN_MONITORS
 *      message to stretch the window over multiple monitors.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The window is fullscreened.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::FullscreenWindow(GtkWindow *win,        // IN
                         MonitorBounds *bounds) // IN/OPT
{
#ifdef GDK_WINDOWING_X11
   GdkScreen *screen = gtk_window_get_screen(win);
   ASSERT(screen);

   GdkAtom atom = gdk_atom_intern("_NET_WM_STATE_FULLSCREEN", FALSE);
   if (atom != GDK_NONE && gdk_net_wm_supports(atom)) {
      Log("Attempting to fullscreen window using _NET_WM_STATE_FULLSCREEN"
          " hint.\n");
      // The window manager supports fullscreening the window on its own.
      gtk_window_fullscreen(win);
      atom = gdk_atom_intern("_NET_WM_FULLSCREEN_MONITORS", FALSE);
      if (bounds && atom != GDK_NONE && gdk_net_wm_supports(atom)) {
         Log("Arguments to _NET_WM_FULLSCREEN_MONITORS: top %ld, bottom %ld, "
             "left %ld, right %ld.\n",
             bounds->top, bounds->bottom, bounds->left, bounds->right);

         gdk_error_trap_push();

         Display *display = GDK_WINDOW_XDISPLAY(GTK_WIDGET(win)->window);
         XGrabServer(display);

         XClientMessageEvent xclient;
         memset(&xclient, 0, sizeof xclient);
         xclient.type = ClientMessage;
         xclient.window = GDK_WINDOW_XWINDOW(GTK_WIDGET(win)->window);
         xclient.message_type = XInternAtom(display,
                                            "_NET_WM_FULLSCREEN_MONITORS",
                                            false);
         xclient.format = 32;
         xclient.data.l[0] = bounds->top;
         xclient.data.l[1] = bounds->bottom;
         xclient.data.l[2] = bounds->left;
         xclient.data.l[3] = bounds->right;

         // Source indication = 1 for normal applications.
         xclient.data.l[4] = 1;

         XSendEvent(display,
                    GDK_WINDOW_XWINDOW(gdk_screen_get_root_window(screen)),
                    false,
                    SubstructureRedirectMask | SubstructureNotifyMask,
                    (XEvent *) &xclient);

         XUngrabServer(display);

         gdk_display_sync(gdk_screen_get_display(screen));
         int errCode = gdk_error_trap_pop();
         if (errCode) {
            char buffer[BUFFER_LEN];
            XGetErrorText(display, errCode, buffer, BUFFER_LEN);
            Log("Error sending _NET_WM_FULLSCREEN_MONITORS message: %s\n",
                buffer);
         }
      }
   } else {
      /*
       * The window manager does not support fullscreening the window, so
       * we must set the size and position manually.
       */
      GdkRectangle geometry;
      gdk_screen_get_monitor_geometry(
         screen,
         gdk_screen_get_monitor_at_window(screen, GTK_WIDGET(win)->window),
         &geometry);

      Log("Attempting to manually fullscreen window: %d, %d %d x %d\n",
          geometry.x, geometry.y, geometry.width, geometry.height);

      gtk_window_move(win, geometry.x, geometry.y);
      gtk_window_resize(win, geometry.width, geometry.height);

      if (GTK_WIDGET_VISIBLE(GTK_WIDGET(win))) {
         gtk_widget_hide(GTK_WIDGET(win));
         gtk_window_set_decorated(win, false);
         gtk_widget_show(GTK_WIDGET(win));
      } else {
         gtk_window_set_decorated(win, false);
      }
   }
#else
   // gtk on mingw supports fullscreen windows natively; no need to check.
   gtk_window_fullscreen(win);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::ShowMessageDialog --
 *
 *      Pops up a dialog or shows a transition error message.  The
 *      format argument is a printf format string.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A new dialog, or the error transition page, is displayed.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::ShowMessageDialog(GtkMessageType type,         // IN
                          const Util::string &message, // IN
                          const Util::string &details, // IN
                          va_list args)                // IN
{
   char *format = g_strdup_vprintf(details.c_str(), args);
   char *label = g_markup_printf_escaped("<b>%s</b>\n\n%s", message.c_str(),
                                         format);

   if (Prefs::GetPrefs()->GetNonInteractive()) {
      Log("ShowMessageDialog: %s: %s; Turning off non-interactive mode.\n",
         message.c_str(), format);
      Prefs::GetPrefs()->SetNonInteractive(false);
   }

   g_free(format);

   /*
    * If we're trying to connect, or have already connected, show the
    * error using the transition page.
    */
   if (type == GTK_MESSAGE_ERROR &&
       (dynamic_cast<TransitionDlg *>(mDlg)
#ifndef __MINGW32__
        || dynamic_cast<DesktopDlg *>(mDlg)
#endif
       )) {
      /*
       * We may get a tunnel error/message while the Desktop::Connect RPC is
       * still in flight, which puts us here. If so, and the user clicks
       * Retry before the RPC completes, Broker::ReconnectDesktop will fail
       * the assertion (state != CONNECTING). So cancel all requests before
       * allowing the user to retry.
       */
      mBroker->CancelRequests();
      TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_ERROR,
                                             label, true);
      dlg->SetStock(GTK_STOCK_DIALOG_ERROR);
      SetContent(dlg);
      Util::SetButtonIcon(mForwardButton, GTK_STOCK_REDO, _("_Retry"));
      SetReady();
   } else {
      /*
       * We were seeing issues with dialog placement here when coming out of
       * fullscreen, but that underlying issue seems to have been solved.  In
       * that case, we will set the parent again, but if it leads to a
       * regression, undo this change. See bz 458555 and 486230.
       */
      GtkDialogFlags flags = (GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT |
                                              GTK_DIALOG_MODAL);
      GtkWidget *dialog = gtk_message_dialog_new(mWindow, flags, type,
                                                 GTK_BUTTONS_OK, NULL);
      gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), label);
      gtk_window_set_title(GTK_WINDOW(dialog), gtk_window_get_title(mWindow));
      g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy),
                       NULL);
      gtk_widget_show(dialog);
   }
   g_free(label);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::CancelHandler --
 *
 *      Handler for the various dialogs' cancel button.
 *      Turns off non-interactive mode, allowing users to interact with
 *      dialogs that would otherwise be skipped.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The cancel button does one of three things:
 *
 *      1.  On the broker page, with no RPC in-flight, Quits.
 *
 *      2.  If RPCs are in-flight, cancel them (which re-sensitizes
 *      the page).
 *
 *      3.  Otherwise, goes back to the broker page.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::CancelHandler()
{
   if (!mDlg->GetCancelable()) {
      return;
   }
   if (Prefs::GetPrefs()->GetNonInteractive()) {
      Log("User cancelled; turning off non-interactive mode.\n");
      Prefs::GetPrefs()->SetNonInteractive(false);
   }
   Log("User cancelled.\n");
   if (mDlg->IsSensitive()) {
      TransitionDlg *dlg = dynamic_cast<TransitionDlg *>(mDlg);
      if (dynamic_cast<BrokerDlg *>(mDlg)) {
         Close();
      } else if (dynamic_cast<ScInsertPromptDlg *>(mDlg) ||
                 dynamic_cast<ScCertDlg *>(mDlg) ||
                 dynamic_cast<ScPinDlg *>(mDlg)) {
         SetBusy(_("Logging in..."));
         if (mTokenEventTimeout) {
            StopWatchingForTokenEvents();
         }
         mBroker->SubmitCertificate();
      } else if (dlg) {
         /*
          * If we ask the broker to cancel requests, it will set mDesktop to
          * NULL.  To avoid dereferencing a NULL pointer, disconnect the desktop
          * now.  See bz 531542.
          */
         mBroker->GetDesktop()->Disconnect();
         if (dlg->GetTransitionType() == TransitionDlg::TRANSITION_PROGRESS) {
            /*
             * If the user has canceled the connection, but the DesktopDlg has
             * already kicked off the process, make sure to kill it.
             *
             * Otherwise, we can just cancel all the outstanding Broker
             * requests.
             */
            if (mDesktopHelper) {
               mDesktopUIExitCnx.disconnect();
               delete mDesktopHelper;
               mDesktopHelper = NULL;
            } else {
               mBroker->CancelRequests();
            }
         }
         SetBusy(_("Loading desktops..."));
         mBroker->LoadDesktops();
      } else {
         RequestBroker();
      }
   } else {
      /*
       * If we made it here, the dialog has been desensitized.  In that case,
       * there should be at least one RPC in flight.
       */
      int reqs = mBroker->CancelRequests();

      ASSERT(reqs > 0);
      if (reqs == 0 ||
          dynamic_cast<ScPinDlg *>(mDlg) ||
          dynamic_cast<ScCertDlg *>(mDlg)) {
         if (reqs == 0) {
            Log("Tried to cancel requests, but none were pending; "
                "requesting a new broker.\n");
         }
         // This calls SetReady();
         RequestBroker();
      } else {
         SetReady();
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::PushDesktopEnvironment --
 *
 *      Updates the DISPLAY environment variable according to the
 *      GdkScreen mWindow is on and the LD_LIBRARY_PATH and GST_PLUGIN_PATH
 *      variables to include the MMR path (if MMR is enabled).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      DISPLAY is updated and LD_LIBRARY_PATH may be updated
 *
 *-----------------------------------------------------------------------------
 */

void
Window::PushDesktopEnvironment()
{
#ifndef __MINGW32__
   char *dpy = gdk_screen_make_display_name(gtk_window_get_screen(mWindow));
   g_setenv("DISPLAY", dpy, true);
   g_free(dpy);

   Util::string mmrPath = Prefs::GetPrefs()->GetMMRPath();
   if (mBroker->GetDesktop()->GetIsMMREnabled() && !mmrPath.empty()) {
      const char *ldpath = getenv("LD_LIBRARY_PATH");
      mOrigLDPath = ldpath ? ldpath : "";

      Util::string env = Util::Format("%s%s%s",
                                      mOrigLDPath.c_str(),
                                      mOrigLDPath.empty() ? "" : ":",
                                      mmrPath.c_str());
      g_setenv("LD_LIBRARY_PATH", env.c_str(), true);

      const char *gstpath = getenv("GST_PLUGIN_PATH");
      mOrigGSTPath = gstpath ? gstpath : "";

      char *newPath = g_build_filename(mmrPath.c_str(), "gstreamer", NULL);
      env = Util::Format("%s%s%s",
                         newPath,
                         mOrigGSTPath.empty() ? "" : ":",
                         mOrigGSTPath.c_str());
      g_free(newPath);
      g_setenv("GST_PLUGIN_PATH", env.c_str(), true);
   }
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::PopDesktopEnvironment --
 *
 *      Unsets the DISPLAY environment variable and returns
 *      LD_LIBRARY_PATH variable to it's setting prior to rdesktop
 *      connection.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      DISPLAY is unset and LD_LIBRARY_PATH may be updated
 *
 *-----------------------------------------------------------------------------
 */

void
Window::PopDesktopEnvironment()
{
#ifndef __MINGW32__
   g_setenv("LD_LIBRARY_PATH", mOrigLDPath.c_str(), true);
   mOrigLDPath.clear();

   g_setenv("GST_PLUGIN_PATH", mOrigGSTPath.c_str(), true);
   mOrigGSTPath.clear();
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnKeyPress --
 *
 *      Handle keypress events.
 *
 * Results:
 *      true to stop other handlers from being invoked for the event.
 *      false to propogate the event further.
 *
 * Side effects:
 *      May press cancel button.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
Window::OnKeyPress(GtkWidget *widget, // IN/UNUSED
                   GdkEventKey *evt,  // IN
                   gpointer userData) // IN
{
   Window *that = reinterpret_cast<Window *>(userData);
   ASSERT(that);

   /* If modKeyPressed is true, then it means that one of the Shift,
    * Control, Alt, or Super keys is held down.  The reason to use
    * this over evt->state is that evt->state counts lock modifiers
    * such as Caps Lock and Num Lock, which would prevent catching
    * keystrokes if one of those was enabled.
    */
   bool modKeyPressed = evt->state & GDK_SHIFT_MASK ||
      evt->state & GDK_CONTROL_MASK ||
      evt->state & GDK_MOD1_MASK ||    // Alt
      evt->state & GDK_MOD4_MASK;      // Super (Windows Key)

   if (GDK_Escape == evt->keyval && !modKeyPressed &&
       GTK_WIDGET_SENSITIVE(GTK_WIDGET(that->mCancelButton)) &&
       GTK_WIDGET_VISIBLE(GTK_WIDGET(that->mCancelButton))) {
      ASSERT(that->mDlg);
      gtk_widget_activate(GTK_WIDGET(that->mCancelButton));
      return true;
   } else if (GDK_F5 == evt->keyval && !modKeyPressed &&
              dynamic_cast<DesktopSelectDlg *>(that->mDlg)) {
      that->mBroker->GetDesktops(true);
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnCtrlAltDel --
 *
 *      Ask the user what to do if they hit Ctrl-Alt-Delete.
 *
 * Results:
 *      true: the Ctrl-Alt-Delete key sequence should be inhibited;
 *      the user chose to do something else.
 *      false: Ctrl-Alt-Delete should be sent to the remote desktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Window::OnCtrlAltDel()
{
   Desktop *desktop = mBroker->GetDesktop();
   ASSERT(desktop);
   ASSERT(!mCadDlg);

   mCadDlg = gtk_message_dialog_new(
      mWindow, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
      _("You are connected to %s.\n\n"
        "If this desktop is unresponsive, click Disconnect."),
      desktop->GetName().c_str());
   gtk_window_set_title(GTK_WINDOW(mCadDlg), gtk_window_get_title(mWindow));
   gtk_container_set_border_width(GTK_CONTAINER(mCadDlg), 0);
   gtk_widget_set_name(mCadDlg, "CtrlAltDelDlg");

   GtkWidget *img = CreateBanner();
   gtk_widget_show(img);
   gtk_box_pack_start(GTK_BOX(GTK_DIALOG(mCadDlg)->vbox), img, false, false, 0);
   gtk_box_reorder_child(GTK_BOX(GTK_DIALOG(mCadDlg)->vbox), img, 0);

   /*
    * CanReset() would return false, as we didn't have a session when the
    * desktop was loaded.  Obviously, we now do.
    *
    * Resetting can take time, so don't allow the user to request a reset again
    * while waiting for the first request to process (bz 512077).
    */
   bool showReset = desktop->CanResetSession() &&
                    desktop->GetStatus() != Desktop::STATUS_RESETTING;
   gtk_dialog_add_buttons(GTK_DIALOG(mCadDlg),
                          _("Send C_trl-Alt-Del"), RESPONSE_CTRL_ALT_DEL,
                          _("_Disconnect"), RESPONSE_DISCONNECT,
                          showReset ? _("_Reset") : NULL,
                          RESPONSE_RESET,
#ifdef VMX86_DEVEL
                          Prefs::GetPrefs()->GetKioskMode() ? GTK_STOCK_QUIT : NULL,
                          RESPONSE_QUIT,
#endif
                          NULL);
   gtk_dialog_add_action_widget(
      GTK_DIALOG(mCadDlg), GTK_WIDGET(Util::CreateButton(GTK_STOCK_CANCEL)),
      GTK_RESPONSE_CANCEL);

#ifdef GDK_WINDOWING_X11
   GrabResults grabResults = { false, false, NULL };

   g_signal_connect(mCadDlg, "map-event", G_CALLBACK(OnCADMapped),
                    &grabResults);
   g_signal_connect(mCadDlg, "unrealize", G_CALLBACK(OnCADUnrealized),
                    &grabResults);
#endif

   int response = gtk_dialog_run(GTK_DIALOG(mCadDlg));
   gtk_widget_destroy(mCadDlg);
   mCadDlg = NULL;

   switch (response) {
   case RESPONSE_CTRL_ALT_DEL:
      return false;
   case RESPONSE_DISCONNECT:
      Close();
      return true;
   case RESPONSE_RESET:
      mBroker->ResetDesktop(mBroker->GetDesktop(), true);
      return true;
   case RESPONSE_QUIT:
      Close();
      exit(0);
   case GTK_RESPONSE_DELETE_EVENT:
   case GTK_RESPONSE_CANCEL:
      return true;
   default:
      break;
   }
   g_assert_not_reached();
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnDesktopUIExit --
 *
 *      Handle rdesktop/cvp-ui exiting.  If rdesktop has exited too many times
 *      recently, give up and exit.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May restart RDesktop, display an error, or exit.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::OnDesktopUIExit(Dlg *dlg,   // IN
                        int status) // IN
{
   ASSERT(mDesktopHelper);

   bool exitedWithError = false;
   bool connected = true;

#ifndef __MINGW32__
   exitedWithError = !WIFEXITED(status) ||
      mDesktopHelper->GetIsErrorExitStatus(WEXITSTATUS(status));
   DesktopDlg *deskDlg = dynamic_cast<DesktopDlg *>(mDlg);
   connected = deskDlg != NULL && deskDlg->GetHasConnected();
#endif

   if (exitedWithError && connected && !mRDesktopMonitor.ShouldThrottle()) {
      SetBusy(_("Connecting to desktop..."));
      mBroker->ReconnectDesktop();
   } else if (!exitedWithError) {
      Close();
   } else {
      // The ShowError() below will delete rdesktop if it is mDlg.
      if (dlg != mDlg) {
         delete dlg;
      }
      mRDesktopMonitor.Reset();
      BaseApp::ShowError(CDK_ERR_DESKTOP_DISCONNECTED,
                         _("Disconnected"),
                         _("The desktop has unexpectedly disconnected."));
   }
   delete mDesktopHelper;
   mDesktopHelper = NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnHelp --
 *
 *      Callback for Help button click.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Help dialog shown, possibly created.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::OnHelp(GtkButton *button, // IN
            gpointer data)     // IN
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);

   HelpSupportDlg *helpDlg = that->GetHelpSupportDlg();
   helpDlg->Run();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::CreateHelpSupportDlg --
 *
 *      Returns help and support dialog, set up ready to run.
 *
 * Results:
 *      HelpSupportDlg *
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

HelpSupportDlg *
Window::GetHelpSupportDlg()
{
   static HelpSupportDlg instance;

   InitializeHelpSupportDlg(&instance);
   return &instance;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::InitializeHelpSupportDlg --
 *
 *      Preps the passed in dialog to be run.
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
Window::InitializeHelpSupportDlg(HelpSupportDlg *dlg)
{
   Util::string brokerHostName = mBroker ? mBroker->GetSupportBrokerUrl() : "";

   dlg->SetParent(mWindow);
   dlg->SetHelpContext(mDlg->GetHelpContext());
   dlg->SetSupportFile(Prefs::GetPrefs()->GetSupportFile());
   dlg->SetBrokerHostName(brokerHostName.empty() ? _("Not Connected") :
                                                   brokerHostName);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::GetSmartCardRedirects --
 *
 *      Get rdesktop redirect arguments for the smart cards we know
 *      about.
 *
 * Results:
 *      A list of strings that should be used with the -r option of
 *      rdesktop.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
Window::GetSmartCardRedirects()
{
   std::vector<Util::string> names;
   if (mCryptoki) {
      names = mCryptoki->GetSlotNames();
      for (unsigned int i = 0; i < names.size(); i++) {
         names[i] = Util::Format("scard:%s=%s;Virtual Slot %u", names[i].c_str(), names[i].c_str(), i);
      }
   }
   return names;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::StartWatchingForTokenEvents --
 *
 *      Adds a timeout which monitors for smart card event when the
 *      user is on a smart card related screen.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      TokenEventMonitor() will be called in timeouts.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::StartWatchingForTokenEvents(TokenEventAction action) // IN
{
   ASSERT(mTokenEventTimeout == 0);

   // Ignore any currently pending events.
   while (mCryptoki->GetHadEvent()) { }

   Log("Watching for token events with action %d\n", action);

   mTokenEventAction = action;
   mTokenEventTimeout = g_timeout_add(TOKEN_EVENT_TIMEOUT_MS,
                                      TokenEventMonitor, this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::StopWatchingForTokenEvent --
 *
 *      Removes the token timeout poll.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      TokenEventMonitor() will no longer be called.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::StopWatchingForTokenEvents()
{
   ASSERT(mTokenEventTimeout);
   g_source_remove(mTokenEventTimeout);
   mTokenEventTimeout = 0;
   mTokenEventAction = ACTION_NONE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::TokenEventMonitor --
 *
 *      If any token had an event, quit the main loop so that the
 *      token auth detection process is restarted.
 *
 * Results:
 *      false if the source should be removed.
 *
 * Side effects:
 *      A main loop is quit if false is returned.
 *
 *-----------------------------------------------------------------------------
 */

gboolean
Window::TokenEventMonitor(gpointer data) // IN
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);
   ASSERT(that->mTokenEventTimeout);

   if (!that->mCryptoki->GetHadEvent()) {
      return true;
   }

   switch (that->mTokenEventAction) {
   case ACTION_REQUEST_CERTIFICATE:
      that->mTokenEventTimeout = 0;
      that->RequestCertificate();
      break;
   case ACTION_LOGOUT:
      if (that->mCryptoki->GetIsInserted(that->mAuthCert)) {
         // If our card is still inserted, ignore the event.
         return true;
      }
      that->mTokenEventTimeout = 0;
      that->RequestBroker();
      BaseApp::ShowInfo(_("You have been logged out"),
                        _("Your smart card or token was removed, so you have "
                          "been logged out of the View Connection Server."));
      break;
   case ACTION_NONE:
   default:
      NOT_IMPLEMENTED();
      that->mTokenEventTimeout = 0;
      break;
   }

   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnViewCert --
 *
 *      Callback for view cert button click.  Displays certificate given
 *      by the mDlg.
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
Window::OnViewCert(GtkButton *button, // IN
                   gpointer data)     // IN
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);
   ASSERT(that->mDlg);

   CertViewer *vCert = dynamic_cast<CertViewer *>(that->mDlg);

   if (vCert) {
      // This deletes itself when button is destroyed.
      new ScCertDetailsDlg(
         GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
         vCert->GetCertificate());
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::UpdateButton --
 *
 *      Sets button sensitive and visible according to arguments.
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
Window::UpdateButton(GtkButton *button, // IN
                     bool sensitive,    // IN
                     bool visible,      // IN
                     bool isDefault)    // IN
{
   if (button) {
      gtk_widget_set_sensitive(GTK_WIDGET(button), sensitive);

      if (visible) {
         gtk_widget_show(GTK_WIDGET(button));
         if (isDefault) {
            gtk_window_set_default(mWindow, GTK_WIDGET(button));
         }
      } else {
         gtk_widget_hide(GTK_WIDGET(button));
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::GetFullscreen --
 *
 *      Return whether we are in full screen mode.
 *
 * Results:
 *      true if full screen mode is enabled in the prefs.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Window::GetFullscreen()
   const
{
   return Prefs::GetPrefs()->GetFullScreen() ||
      !Prefs::GetPrefs()->GetBackground().empty();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnWindowManagerChanged --
 *
 *      Callback for when the window manager changed.  Recall the our fullscreen
 *      method to ensure we are properly displayed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifndef __MINGW32__
void
Window::OnWindowManagerChanged(GdkScreen *screen, // IN/UNUSED
                               gpointer data)     // IN
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);

   ASSERT(that->mWindow);
   if (that->GetFullscreen() || dynamic_cast<DesktopDlg *>(that->mDlg)) {
      that->FullscreenWindow(that->mWindow);
   }
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnRealize --
 *
 *      Callback for when the window is realized.  Attach to the GdkScreen
 *      'window-manager-changed' signal.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifndef __MINGW32__
void
Window::OnRealize(GtkWindow *window, // IN
                  gpointer data)     // IN
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);
   ASSERT(window);

   GdkScreen *screen = gtk_window_get_screen(window);
   ASSERT(screen);
   g_signal_connect(screen, "window-manager-changed",
                    G_CALLBACK(OnWindowManagerChanged), that);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnUnrealize --
 *
 *      Callback for when the window is unrealized.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifndef __MINGW32__
void
Window::OnUnrealize(GtkWindow *window, // IN
                    gpointer data)     // IN
{
   Window *that = reinterpret_cast<Window *>(data);
   ASSERT(that);

   GdkScreen *screen = gtk_window_get_screen(window);
   ASSERT(screen);
   g_signal_handlers_disconnect_by_func(screen,
                                        (gpointer)OnWindowManagerChanged, that);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::SetBroker --
 *
 *      Safely set the effective broker.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Current broker is deleted.  Note that a Window instance owns its
 *      broker and as such the caller should not delete it.
 *
 *-----------------------------------------------------------------------------
 */

void
Window::SetBroker(Broker *broker)  // IN
{
   if (mBroker) {
      mBroker->SetDelegate(NULL);
   }
   delete mBroker;
   mBroker = broker;
   if (mBroker) {
      mBroker->SetDelegate(this);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnCADMapped --
 *
 *      Grabs only work if the window has been mapped, so wait for this signal
 *      to grab the pointer (if pointer is outside our window).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef GDK_WINDOWING_X11
void
Window::OnCADMapped(GtkWidget *widget,  // IN
                    GdkEventAny *event, // IN
                    gpointer userData)  // IN
{
   GrabResults *results = reinterpret_cast<GrabResults *>(userData);
   ASSERT(results);

   ::Window root;
   ::Window *children = NULL;
   ::Window parentWin;
   unsigned int num;
   XQueryTree(GDK_WINDOW_XDISPLAY(event->window), GDK_WINDOW_XID(event->window),
              &root, &parentWin, &children, &num);
   if (children) {
      XFree(children);
   }

   /*
    * Look to see if our window decoration X window has a corresponding
    * GdkWindow.  If not, create one.
    */
   GdkWindow *newWin = NULL;
   gpointer xidLookup = gdk_xid_table_lookup(parentWin);
   if (!xidLookup) {
      newWin = gdk_window_foreign_new(parentWin);
      ASSERT(newWin);
   } else if (GDK_IS_WINDOW(xidLookup)) {
      newWin = GDK_WINDOW(xidLookup);
   } else {
      // Our decoration window shouldn't be anything but a GdkWindow
      NOT_REACHED();
      return;
   }

   results->window = newWin;
   gdk_window_set_events(newWin,
                         (GdkEventMask)(GDK_ENTER_NOTIFY_MASK |
                                        GDK_LEAVE_NOTIFY_MASK));
   gdk_window_add_filter(newWin, CADEventFilter, results);

   if (gdk_window_at_pointer(NULL, NULL) != newWin) {
      GrabPointer(results);
   }

   gdk_keyboard_grab(event->window, false, GDK_CURRENT_TIME);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnCADUnrealized --
 *
 *      Make sure we don't have the pointer grabbed anymore, and remove our
 *      event filter.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef GDK_WINDOWING_X11
void
Window::OnCADUnrealized(GtkWidget *widget, // IN/UNUSED
                        gpointer userData) // IN
{
   GrabResults *results = reinterpret_cast<GrabResults *>(userData);
   ASSERT(results);

   UngrabPointer(results);
   gdk_window_remove_filter(results->window, CADEventFilter, results);

   gdk_keyboard_ungrab(GDK_CURRENT_TIME);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::GrabPointer --
 *
 *      Grab the pointer for the given window.
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
Window::GrabPointer(GrabResults *results) // IN
{
   if (!results->mouseGrabbed) {
      results->ignoreNext = true;
      GdkGrabStatus mouseStatus =
         gdk_pointer_grab(results->window, true,
                          (GdkEventMask)(GDK_BUTTON_PRESS_MASK |
                                         GDK_BUTTON_RELEASE_MASK |
                                         GDK_ENTER_NOTIFY_MASK |
                                         GDK_LEAVE_NOTIFY_MASK),
                          NULL, NULL, GDK_CURRENT_TIME);
      results->mouseGrabbed = mouseStatus == GDK_GRAB_SUCCESS;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::UngrabPointer --
 *
 *      Ungrab the pointer.
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
Window::UngrabPointer(GrabResults *results) // IN
{
   if (results->mouseGrabbed) {
      results->ignoreNext = false;
      gdk_pointer_ungrab(GDK_CURRENT_TIME);
      results->mouseGrabbed = false;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::CADEventFilter --
 *
 *      Filter all events for WM decoration window for the CAD dialog.  We're
 *      only interested in EnterNotify, LeaveNotify, ButtonPress, and
 *      ButtonRelease.  The window will grab the pointer on leave to prevent
 *      interation with the socket, and will ungrab on enter, so the user can
 *      interact with us.  We ignore button press and button release events due
 *      to the fact that this window is not a top-level window and doesn't have
 *      any way to interact with it.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Pointer may be grabbed or ungrabbed.
 *
 *-----------------------------------------------------------------------------
 */

#ifdef GDK_WINDOWING_X11
GdkFilterReturn
Window::CADEventFilter(GdkXEvent *xevent, // IN
                       GdkEvent *event,   // IN/UNUSED
                       gpointer userData) // IN
{
   GrabResults *results = reinterpret_cast<GrabResults *>(userData);
   ASSERT(results);

   XEvent *castEvent = (XEvent *)xevent;

   if (castEvent->type == EnterNotify ||
       castEvent->type == LeaveNotify) {

      if (results->ignoreNext) {
         results->ignoreNext = false;
         return GDK_FILTER_REMOVE;
      }

      if (castEvent->type == LeaveNotify) {
         GrabPointer(results);
      } else {
         UngrabPointer(results);
      }
   } else if (castEvent->type == ButtonPress ||
              castEvent->type == ButtonRelease) {
      return GDK_FILTER_REMOVE;
   }

   return GDK_FILTER_CONTINUE;
}
#endif


} // namespace cdk
