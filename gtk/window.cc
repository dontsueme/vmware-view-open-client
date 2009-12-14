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

extern "C" {
#include "vm_basic_types.h"
#include "base64.h"
} // extern "C"

#include "brokerDlg.hh"
#include "certViewer.hh"
#include "cryptoki.hh"
#include "desktopDlg.hh"
#include "disclaimerDlg.hh"
#include "helpSupportDlg.hh"
#include "icons/spinner_anim.h"
#include "icons/view_16x.h"
#include "icons/view_32x.h"
#include "icons/view_48x.h"
#include "icons/view_client_banner.h"
#include "loginDlg.hh"
#include "passwordDlg.hh"
#include "prefs.hh"
#include "protocols.hh"
#include "rdesktop.hh"
#include "rmks.hh"
#include "scCertDetailsDlg.hh"
#include "scCertDlg.hh"
#include "scInsertPromptDlg.hh"
#include "scPinDlg.hh"
#include "securIDDlg.hh"
#include "transitionDlg.hh"
#include "tunnel.hh"
#include "window.hh"


#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>


enum {
   RESPONSE_CTRL_ALT_DEL = 1,
   RESPONSE_DISCONNECT,
   RESPONSE_RESET
};


#define BANNER_HEIGHT 62
#define BANNER_MIN_WIDTH 480
#define BUFFER_LEN 256
#define SPINNER_ANIM_FPS_RATE 10
#define SPINNER_ANIM_N_FRAMES 20
#define TOKEN_EVENT_TIMEOUT_MS 500

#define VMWARE_HOME_DIR "~/.vmware"
#define COOKIE_FILE_NAME VMWARE_HOME_DIR"/view-cookies"
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
     mCertAuthInfo(NULL),
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
   mDesktopUIExitCnx.disconnect();
   delete mDesktopHelper;
   delete mCryptoki;
   delete mDlg;
   delete mBroker;
   if (mWindow) {
      gtk_widget_destroy(GTK_WIDGET(mWindow));
   }
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
      mCryptoki->FreeCert(mAuthCert);
      mAuthCert = NULL;

      mCryptoki->CloseAllSessions();
   } else {
      /*
       * If we don't have a Cryptoki object, we better not have a
       * cert.
      */
      ASSERT(!mAuthCert);
   }
   delete mBroker;
   mBroker = NULL;
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

      mFullscreenAlign = GTK_ALIGNMENT(gtk_alignment_new(0.5,0.5,0,0));
      gtk_widget_show(GTK_WIDGET(mFullscreenAlign));
      gtk_fixed_put(fixed, GTK_WIDGET(mFullscreenAlign), 0, 0);
      g_object_add_weak_pointer(G_OBJECT(mFullscreenAlign),
                                (gpointer *)&mFullscreenAlign);
      OnSizeAllocate(NULL, &GTK_WIDGET(mWindow)->allocation, this);

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
   // Set the window's _NET_WM_USER_TIME from an X server roundtrip.
   Util::OverrideWindowUserTime(mWindow);
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
      if (dynamic_cast<DesktopDlg *>(mDlg)) {
         mDesktopUIExitCnx.disconnect();
         if (mDesktopHelper) {
            mDesktopHelper->Kill();
            g_idle_add(OnIdleDeleteProcHelper, mDesktopHelper);
            mDesktopHelper = NULL;
         }
         if (!GetFullscreen() && Prefs::GetPrefs()->GetBackground().empty()) {
            /*
             * This causes GtkWindow's centering logic to be reset, so
             * the window isn't in the bottom left corner after coming
             * out of full screen mode.
             */
            gtk_widget_hide(GTK_WIDGET(mWindow));
            hidden = true;
         }
      }
      delete mDlg;
   }
   mDlg = dlg;
   GtkWidget *content = mDlg->GetContent();
   gtk_widget_show(content);

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

   UpdateForwardButton(mDlg->GetForwardEnabled(), mDlg->GetForwardVisible());
   mDlg->updateForwardButton.connect(boost::bind(&Window::UpdateForwardButton,
                                                 this, _1, _2));

   if (mCancelButton) {
      Util::string stockId = dynamic_cast<BrokerDlg *>(mDlg) ?
                                 GTK_STOCK_QUIT : GTK_STOCK_CANCEL;
      Util::SetButtonIcon(mCancelButton, stockId);
   }

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
   }
   firstTimeThrough = false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::GetCertAuthInfo --
 *
 *      Get a user-selected certificate and private key for use in
 *      authenticating to the broker.
 *
 * Results:
 *      An X509 cert, or NULL, and *privKey points to a private key, or NULL.
 *
 * Side effects:
 *      mCryptoki may be created and initialized.
 *
 *-----------------------------------------------------------------------------
 */

Broker::CertAuthInfo
Window::GetCertAuthInfo(SSL *ssl) // IN
{
   ASSERT(!mCertAuthInfo);

   if (!mCryptoki) {
      mCryptoki = new Cryptoki();
      mCryptoki->requestPin.connect(boost::bind(&Window::OnScPinRequested,
                                                this, _1, _2));
      mCryptoki->LoadModules(LIBDIR"/vmware/view/pkcs11");
   }

   std::list<X509 *> certs;
   Broker::CertAuthInfo info;
   mCertAuthInfo = &info;

requestCerts:
   // Reset the certs.
   info.cert = NULL;
   info.key = NULL;
   if (info.pin) {
      ZERO_STRING(info.pin);
      g_free(info.pin);
      info.pin = NULL;
   }
   info.reader.clear();
   Cryptoki::FreeCertificates(certs);

   if (mCryptoki->GetHasSlots() && !mCryptoki->GetHasTokens()) {
      SetReady();
      ScInsertPromptDlg *dlg = new ScInsertPromptDlg(mCryptoki);
      SetContent(dlg);

      mCanceledScDlg = false;
      gtk_main();
      SetBusy(_("Logging in..."));

      if (mCanceledScDlg) {
         goto returnInfo;
      }
   }

   certs = mCryptoki->GetCertificates(SSL_get_client_CA_list(ssl));
   switch (certs.size()) {
   case 0:
      goto returnInfo;
   case 1:
      info.cert = certs.front();
      break;
   default: {
      ScCertDlg *dlg = new ScCertDlg();
      SetReady();
      SetContent(dlg);
      Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Co_nnect"));
      dlg->SetCertificates(certs);

      mCanceledScDlg = false;
      StartWatchingForTokenEvents(ACTION_QUIT_MAIN_LOOP);
      gtk_main();
      SetBusy(_("Logging in..."));
      if (HadTokenEvent()) {
         goto requestCerts;
      }
      StopWatchingForTokenEvents();
      if (!mCanceledScDlg) {
         info.cert = const_cast<X509 *>(dlg->GetCertificate());
      }
      break;
   }
   }

   if (info.cert) {
      StartWatchingForTokenEvents(ACTION_QUIT_MAIN_LOOP);
      info.key = mCryptoki->GetPrivateKey(info.cert);
      if (HadTokenEvent()) {
         goto requestCerts;
      }
      StopWatchingForTokenEvents();
      certs.remove(info.cert);
      Cryptoki::FreeCertificates(certs);
      info.reader = mCryptoki->GetSlotName(info.cert);
   }

returnInfo:
   mCertAuthInfo = NULL;
   mAuthCert = mCryptoki->DupCert(info.cert);
   return info;
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
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK);
   dlg->SetText(disclaimer);
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
Window::RequestPasscode(const Util::string &username) // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("_Authenticate"));
   dlg->SetState(SecurIDDlg::STATE_PASSCODE, username);
   dlg->authenticate.connect(boost::bind(&Window::DoSubmitPasscode, this));
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
   dlg->SetState(SecurIDDlg::STATE_SET_PIN, pin, message, userSelectable);
   dlg->authenticate.connect(boost::bind(&Window::DoSubmitPins, this));
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
      if (Str_Strcasecmp(i->c_str(), suggestedDomain.c_str()) == 0) {
         // Use value in the list so the case matches.
         domain = *i;
         domainFound = true;
         break;
      } else if (Str_Strcasecmp(i->c_str(), domainPref.c_str()) == 0) {
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
      AddForwardIdleHandler();
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
         break;
      } else if ((*i)->GetAutoConnect()) {
         initialDesktop = name;
      }
   }

   int monitors = gdk_screen_get_n_monitors(gtk_window_get_screen(mWindow));
   Log("Number of monitors on this screen is %d.\n", monitors);

   GdkAtom atom = gdk_atom_intern("_NET_WM_FULLSCREEN_MONITORS", FALSE);
   bool supported = atom != GDK_NONE && gdk_net_wm_supports(atom);
   Log("Current window manager %s _NET_WM_FULLSCREEN_MONITORS message.\n",
       supported ? "supports" : "does not support");

   DesktopSelectDlg *dlg = new DesktopSelectDlg(mBroker->mDesktops,
                                                initialDesktop,
                                                monitors > 1 && supported,
                                                !GetFullscreen());
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Co_nnect"));
   dlg->action.connect(boost::bind(&Window::DoDesktopAction, this, _1));

   if (Prefs::GetPrefs()->GetNonInteractive() &&
       (!initialDesktop.empty() ||
        (mBroker->mDesktops.size() == 1 && dlg->GetDesktop()->CanConnect()))) {
      AddForwardIdleHandler();
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
Window::RequestTransition(const Util::string &message)
{
   Log("Transitioning: %s\n", message.c_str());
   TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_PROGRESS,
                                          message);

   std::vector<GdkPixbuf *> pixbufs = TransitionDlg::LoadAnimation(
      -1, spinner_anim, false, SPINNER_ANIM_N_FRAMES);
   dlg->SetAnimation(pixbufs, SPINNER_ANIM_FPS_RATE);
   for (std::vector<GdkPixbuf *>::iterator i = pixbufs.begin();
        i != pixbufs.end(); i++) {
      g_object_unref(*i);
   }

   SetContent(dlg);

   gtk_widget_hide(GTK_WIDGET(mForwardButton));
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
   Util::string tmpName = COOKIE_FILE_NAME;
   if (Base64_EasyEncode((const uint8 *)brokerUrl.c_str(), brokerUrl.size(), &encUrl)) {
      tmpName += ".";
      tmpName += encUrl;
      free(encUrl);
   } else {
      Log("Failed to b64-encode url: %s; using default cookie file.\n", brokerUrl.c_str());
   }
   char *cookieFile = Util_ExpandString(tmpName.c_str());
   if (cookieFile && *cookieFile) {
      bool cookieFileOk = false;
      if (0 == chmod(cookieFile, COOKIE_FILE_MODE)) {
         cookieFileOk = true;
      } else if (errno == ENOENT) {
         int fd = open(cookieFile, O_CREAT, COOKIE_FILE_MODE);
         if (fd != -1) {
            cookieFileOk = true;
            close(fd);
         } else  {
            Warning(_("Cookie file '%s' could not be created: %s\n"),
                    cookieFile, strerror(errno));
         }
      } else {
         Warning(_("Could not change status of cookie file '%s': %s\n"),
                 cookieFile, strerror(errno));
      }
      if (cookieFileOk) {
         mBroker->SetCookieFile(cookieFile);
      }
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

   if (mBroker) {
       // this method can be called repeatedly, for example if a broker
       // connection could not be made, so we need to clean up any existing
       // broker before reinitializing.
       delete mBroker;
       mBroker = NULL;
   }

   mBroker = new Broker();
   mBroker->SetDelegate(this);

   SetCookieFile(Util::GetHostLabel(brokerDlg->GetBroker(),
                                    brokerDlg->GetPort(),
                                    brokerDlg->GetSecure()));

   std::vector<Util::string> protocols;
   if (RDesktop::GetIsProtocolAvailable()) {
        protocols.push_back(Protocols::GetName(Protocols::RDP));
   }
   if (RMks::GetIsProtocolAvailable()) {
        protocols.push_back(Protocols::GetName(Protocols::PCOIP));
   }
   mBroker->SetSupportedProtocols(protocols);
   mBroker->Initialize(brokerDlg->GetBroker(), brokerDlg->GetPort(),
                       brokerDlg->GetSecure(),
                       prefs->GetDefaultUser(),
                       // We'll use the domain pref later if need be.
                       "");
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
Window::DelayedDoInitialize(void *that) // IN:
{
   ((cdk::Window *)that)->DoInitialize();
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

   if (dynamic_cast<BrokerDlg *>(mDlg)) {
      DoInitialize();
   } else if (dynamic_cast<DisclaimerDlg *>(mDlg)) {
      ASSERT(mBroker);
      mBroker->AcceptDisclaimer();
   } else if (dynamic_cast<ScInsertPromptDlg *>(mDlg) ||
              dynamic_cast<ScCertDlg *>(mDlg) ||
              dynamic_cast<ScPinDlg *>(mDlg)) {
      gtk_main_quit();
   } else if (securIdDlg) {
      securIdDlg->authenticate();
   } else if (dynamic_cast<LoginDlg *>(mDlg)) {
      if (dynamic_cast<PasswordDlg *>(mDlg)) {
         DoChangePassword();
      } else {
         DoSubmitPassword();
      }
   } else if (dynamic_cast<DesktopSelectDlg *>(mDlg)) {
      DoDesktopAction(DesktopSelectDlg::ACTION_CONNECT);
   } else if (dynamic_cast<TransitionDlg *>(mDlg)) {
      ASSERT(mBroker);
      mBroker->ReconnectDesktop();
   } else {
      NOT_IMPLEMENTED();
   }
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
      ShowDialog(GTK_MESSAGE_ERROR, _("The PINs do not match."));
   } else {
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
   prefs->SetDefaultUser(user);
   prefs->SetDefaultDomain(domain);

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
      ShowDialog(GTK_MESSAGE_ERROR, _("The passwords do not match."));
   } else {
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
   ASSERT(desktop);

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

   Util::string message = _("The secure connection to the View Server has"
                            " unexpectedly disconnected.");
   if (!disconnectReason.empty()) {
      message += Util::Format(_("\n\nReason: %s."), _(disconnectReason.c_str()));
   }

   ShowDialog(GTK_MESSAGE_ERROR, "%s", message.c_str());
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
 *      Resize the GtkAlignment to fill available space, and possibly
 *      the background image as well.
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
   if (that->mFullscreenAlign) {
      /*
       * This really does need to be a _set_size_request(), and not
       * _size_allocate(), otherwise there is some resize flickering
       * at startup (and quitting, if that happens).
       */
      gtk_widget_set_size_request(GTK_WIDGET(that->mFullscreenAlign),
                                  allocation->width, allocation->height);
   }
   if (dynamic_cast<DesktopDlg *>(that->mDlg)) {
      gtk_widget_size_allocate(that->mDlg->GetContent(), allocation);
   }
   if (that->mBackgroundImage) {
      that->ResizeBackground(allocation);
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
      pb = gdk_pixbuf_new_from_inline(-1, view_client_banner, false, NULL);
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
      mDesktopHelper = new RDesktop();
      dlg = new DesktopDlg(mDesktopHelper, Prefs::GetPrefs()->GetAllowWMBindings());
      break;
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
   DesktopDlg *deskDlg = dynamic_cast<DesktopDlg *>(dlg);
   if (deskDlg) {
      deskDlg->onConnect.connect(boost::bind(&Window::SetContent, this, dlg));
      deskDlg->onCtrlAltDel.connect(boost::bind(&Window::OnCtrlAltDel, this));
   }

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

   deskDlg->SetInitialDesktopSize(geometry.width, geometry.height);

   RDesktop *rdesktop = dynamic_cast<RDesktop *>(mDesktopHelper);
   RMks *rmks = dynamic_cast<RMks *>(mDesktopHelper);

   ASSERT(!rmks || !rdesktop);

   PushDesktopEnvironment();

   if (desktop->GetIsUSBEnabled()) {
      desktop->StartUsb();
   }

   const BrokerXml::DesktopConnection &conn = desktop->GetConnection();
   Log("Connecting to desktop %s: %s://%s@%s:%d\n",
       conn.id.c_str(), conn.protocol.c_str(), conn.username.c_str(),
       conn.address.c_str(), conn.port);

   if (rdesktop) {
      rdesktop->Start(conn, deskDlg->GetWindowId(), &geometry,
                      GetSmartCardRedirects());
      deskDlg->SetInhibitCtrlEnter(true);
   } else if (rmks) {
      rmks->Start(conn, deskDlg->GetWindowId(), &geometry);
      deskDlg->SetInhibitCtrlEnter(false);
   } else {
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

      gdk_screen_get_monitor_geometry(screen, 0, geometry);
      int minX = geometry->x;
      int maxX = geometry->x + geometry->width;
      int minY = geometry->y;
      int maxY = geometry->y + geometry->height;
      for (int i = 1; i < numMonitors; ++i) {
         GdkRectangle nextMonitor;
         gdk_screen_get_monitor_geometry(screen, i, &nextMonitor);
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
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::ShowDialog --
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
Window::ShowDialog(GtkMessageType type,       // IN
                   const Util::string format, // IN
                   ...)
{
   /*
    * It would be nice if there was a va_list variant of
    * gtk_message_dialog_new().
    */
   va_list args;
   va_start(args, format);
   Util::string label = Util::FormatV(format.c_str(), args);
   va_end(args);

   if (Prefs::GetPrefs()->GetNonInteractive()) {
      Log("ShowDialog: %s; Turning off non-interactive mode.\n", label.c_str());
      Prefs::GetPrefs()->SetNonInteractive(false);
   }

   /*
    * If we're trying to connect, or have already connected, show the
    * error using the transition page.
    */
   if (type == GTK_MESSAGE_ERROR &&
       (dynamic_cast<TransitionDlg *>(mDlg) ||
        dynamic_cast<DesktopDlg *>(mDlg))) {
      /*
       * We may get a tunnel error/message while the Desktop::Connect RPC is
       * still in flight, which puts us here. If so, and the user clicks
       * Retry before the RPC completes, Broker::ReconnectDesktop will fail
       * the assertion (state != CONNECTING). So cancel all requests before
       * allowing the user to retry.
       */
      mBroker->CancelRequests();
      TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_ERROR,
                                             label);
      dlg->SetStock(GTK_STOCK_DIALOG_ERROR);
      SetContent(dlg);
      Util::SetButtonIcon(mForwardButton, GTK_STOCK_REDO, _("_Retry"));
   } else {
      /*
       * XXX: Ideally, we'd set our parent window here, except that
       * when coming out of full screen, if our parent is the window,
       * the dialog may be positioned strangely.
       */
      GtkWidget *dialog = gtk_message_dialog_new(
         NULL, GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK,
         "%s", label.c_str());
      gtk_widget_show(dialog);
      gtk_window_set_title(GTK_WINDOW(dialog),
                           gtk_window_get_title(mWindow));
      g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy),
                       NULL);
   }
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
         mCanceledScDlg = true;
         gtk_main_quit();
      } else if (dlg) {
         if (dlg->GetTransitionType() == TransitionDlg::TRANSITION_PROGRESS) {
            mBroker->CancelRequests();
         }
         mBroker->LoadDesktops();
      } else {
         RequestBroker();
      }
   } else {
      int reqs = mBroker->CancelRequests();

      ASSERT(reqs > 0);
      if (reqs == 0 ||
          dynamic_cast<ScPinDlg *>(mDlg) ||
          dynamic_cast<ScCertDlg *>(mDlg)) {
         if (reqs == 0) {
            Log("Tried to cancel requests, but none were pending; "
                "requesting a new broker.\n");
         }
         RequestBroker();
      }
   }

   ProcHelper *proc = dynamic_cast<ProcHelper *>(mDlg);
   if (proc) {
      mDesktopUIExitCnx.disconnect();
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
   char *dpy = gdk_screen_make_display_name(gtk_window_get_screen(mWindow));
   setenv("DISPLAY", dpy, true);
   g_free(dpy);

   Util::string mmrPath = Prefs::GetPrefs()->GetMMRPath();
   if (mBroker->GetDesktop()->GetIsMMREnabled() && !mmrPath.empty()) {
      const char *ldpath = getenv("LD_LIBRARY_PATH");
      mOrigLDPath = ldpath ? ldpath : "";

      Util::string env = Util::Format("%s%s%s",
                                      mOrigLDPath.c_str(),
                                      mOrigLDPath.empty() ? "" : ":",
                                      mmrPath.c_str());
      setenv("LD_LIBRARY_PATH", env.c_str(), true);

      const char *gstpath = getenv("GST_PLUGIN_PATH");
      mOrigGSTPath = gstpath ? gstpath : "";

      char *newPath = g_build_filename(mmrPath.c_str(), "gstreamer", NULL);
      env = Util::Format("%s%s%s",
                         newPath,
                         mOrigGSTPath.empty() ? "" : ":",
                         mOrigGSTPath.c_str());
      g_free(newPath);
      setenv("GST_PLUGIN_PATH", env.c_str(), true);
   }
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
   setenv("LD_LIBRARY_PATH", mOrigLDPath.c_str(), true);
   mOrigLDPath.clear();

   setenv("GST_PLUGIN_PATH", mOrigGSTPath.c_str(), true);
   mOrigGSTPath.clear();
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

   if (GDK_Escape == evt->keyval && !modKeyPressed) {
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

   gtk_dialog_add_buttons(GTK_DIALOG(mCadDlg),
                          _("Send C_trl-Alt-Del"), RESPONSE_CTRL_ALT_DEL,
                          _("_Disconnect"), RESPONSE_DISCONNECT,
                          desktop->CanReset() || desktop->CanResetSession()
                             ? _("_Reset") : NULL,
                          RESPONSE_RESET,
                          NULL);
   gtk_dialog_add_action_widget(
      GTK_DIALOG(mCadDlg), GTK_WIDGET(Util::CreateButton(GTK_STOCK_CANCEL)),
      GTK_RESPONSE_CANCEL);

   // Widget must be shown to do grabs on it.
   gtk_widget_show(mCadDlg);

   /*
    * Grab the keyboard and mouse; our rdesktop window currently has
    * the keyboard grab, which we need here to have keyboard
    * focus/navigation.
    */
   GdkGrabStatus kbdStatus = gdk_keyboard_grab(mCadDlg->window, false,
                                               GDK_CURRENT_TIME);
   GdkGrabStatus mouseStatus =
      gdk_pointer_grab(mCadDlg->window, true,
                       (GdkEventMask)(GDK_POINTER_MOTION_MASK |
                                      GDK_POINTER_MOTION_HINT_MASK |
                                      GDK_BUTTON_MOTION_MASK |
                                      GDK_BUTTON1_MOTION_MASK |
                                      GDK_BUTTON2_MOTION_MASK |
                                      GDK_BUTTON3_MOTION_MASK |
                                      GDK_BUTTON_PRESS_MASK |
                                      GDK_BUTTON_RELEASE_MASK),
                       NULL, NULL, GDK_CURRENT_TIME);

   int response = gtk_dialog_run(GTK_DIALOG(mCadDlg));
   gtk_widget_destroy(mCadDlg);
   mCadDlg = NULL;

   if (mouseStatus == GDK_GRAB_SUCCESS) {
      gdk_pointer_ungrab(GDK_CURRENT_TIME);
   }
   if (kbdStatus == GDK_GRAB_SUCCESS) {
      gdk_keyboard_ungrab(GDK_CURRENT_TIME);
   }

   switch (response) {
   case RESPONSE_CTRL_ALT_DEL:
      return false;
   case RESPONSE_DISCONNECT:
      Close();
      return true;
   case RESPONSE_RESET:
      mBroker->ResetDesktop(mBroker->GetDesktop(), true);
      return true;
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
   DesktopDlg *deskDlg = dynamic_cast<DesktopDlg *>(mDlg);
   if (status && deskDlg && deskDlg->GetHasConnected() &&
       !mRDesktopMonitor.ShouldThrottle()) {
      mBroker->ReconnectDesktop();
   } else if (!status) {
      Close();
   } else {
      // The ShowDialog() below will delete rdesktop if it is mDlg.
      if (dlg != mDlg) {
         delete dlg;
      }
      mRDesktopMonitor.Reset();
      ShowDialog(GTK_MESSAGE_ERROR,
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

   HelpSupportDlg::ShowDlg(GTK_WINDOW(that->mWindow));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Window::OnScPinRequested --
 *
 *      Cryptoki callback for when a PIN is requested.  Ask Window for
 *      the PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

const char *
Window::OnScPinRequested(const Util::string &label, // IN
                         const X509 *x509)          // IN
{
   ASSERT(mCertAuthInfo);
   // If the old PIN was incorrect...
   if (mCertAuthInfo->pin) {
      ZERO_STRING(mCertAuthInfo->pin);
      g_free(mCertAuthInfo->pin);
      mCertAuthInfo->pin = NULL;
   }

   SetReady();
   ScPinDlg *dlg = new ScPinDlg();
   SetContent(dlg);
   Util::SetButtonIcon(mForwardButton, GTK_STOCK_OK, _("Co_nnect"));
   dlg->SetTokenName(label);
   dlg->SetCertificate(x509);

   mCanceledScDlg = false;

   // We need to block the caller until we have an answer.
   gtk_main();

   mCertAuthInfo->pin = mCanceledScDlg ? NULL : g_strdup(dlg->GetPin());

   // Disable the OK button
   SetBusy(_("Logging in..."));

   return mCertAuthInfo->pin;
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
      for (std::vector<Util::string>::iterator i = names.begin();
           i != names.end(); i++) {
         *i = "scard:" + *i;
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

   // Don't let Reset() remove this source.
   that->mTokenEventTimeout = 0;

   switch (that->mTokenEventAction) {
   case ACTION_QUIT_MAIN_LOOP:
      gtk_main_quit();
      break;
   case ACTION_LOGOUT:
      if (that->mCryptoki->GetIsInserted(that->mAuthCert)) {
         return true;
      }
      that->RequestBroker();
      that->ShowDialog(GTK_MESSAGE_INFO,
                       _("Your smart card or token was removed, so you have "
                         "been logged out of the View Connection Server."));
      break;
   case ACTION_NONE:
   default:
      NOT_IMPLEMENTED();
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


} // namespace cdk
