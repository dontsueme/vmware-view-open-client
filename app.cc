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
 * app.cc --
 *
 *    Application singleton object. It handles initialization of global
 *    libraries and resources.
 *
 */


#include <boost/bind.hpp>


#include "app.hh"
#include "disclaimerDlg.hh"
#include "icons/spinner_anim.h"
#define SPINNER_ANIM_N_FRAMES 20
#define SPINNER_ANIM_FPS_RATE 10
#include "view_16x.h"
#include "view_32x.h"
#include "view_48x.h"
#include "view_client_banner.h"
#include "loginDlg.hh"
#include "passwordDlg.hh"
#include "prefs.hh"
#include "scCertDlg.hh"
#include "scInsertPromptDlg.hh"
#include "scPinDlg.hh"
#include "securIDDlg.hh"
#include "transitionDlg.hh"
#include "tunnel.hh"

#include "basicHttp.h"

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <X11/Xlib.h>

extern "C" {
#include "vm_basic_types.h"
#include "vm_version.h"
#include "log.h"
#include "msg.h"
#include "poll.h"
#include "preference.h"
#include "productState.h"
#include "sig.h"
#include "ssl.h"
#include "unicode.h"
#include "vthread.h"
}

enum {
   RESPONSE_CTRL_ALT_DEL = 1,
   RESPONSE_DISCONNECT,
   RESPONSE_RESET
};


/*
 * Define and use these alternate PRODUCT_VIEW_* names until vm_version.h
 * uses the View naming scheme.
 */
#define PRODUCT_VIEW_SHORT_NAME "View"
#define PRODUCT_VIEW_NAME MAKE_NAME("View Manager")
#define PRODUCT_VIEW_CLIENT_NAME_FOR_LICENSE PRODUCT_VIEW_CLIENT_NAME

#define VIEW_DEFAULT_MMR_PATH "/usr/lib/mmr/"

#define BUFFER_LEN 256


namespace cdk {


/*
 * Initialise static data.
 */

App *App::sApp = NULL;

char *App::sOptBroker = NULL;
char *App::sOptUser = NULL;
char *App::sOptPassword = NULL;
char *App::sOptDomain = NULL;
char *App::sOptDesktop = NULL;
gboolean App::sOptNonInteractive = false;
gboolean App::sOptFullscreen = false;
char *App::sOptBackground = NULL;
char *App::sOptFile = NULL;
char **App::sOptRedirect = NULL;
gboolean App::sOptVersion = false;
char **App::sOptUsb = NULL;
char *App::sOptMMRPath = NULL;
char *App::sOptRDesktop = NULL;


GOptionEntry App::sOptEntries[] =
{
   { "serverURL", 's', 0, G_OPTION_ARG_STRING, &sOptBroker,
     N_("Specify connection broker."), N_("<broker URL>") },
   { "userName", 'u', 0, G_OPTION_ARG_STRING, &sOptUser,
     N_("Specify user name for password authentication."), N_("<user name>") },
   { "password", 'p', 0, G_OPTION_ARG_STRING, &sOptPassword,
     N_("Specify password for password authentication."), N_("<password>") },
   { "domainName", 'd', 0, G_OPTION_ARG_STRING, &sOptDomain,
     N_("Specify domain for password authentication."), N_("<domain name>") },
   { "desktopName", 'n', 0, G_OPTION_ARG_STRING, &sOptDesktop,
     N_("Specify desktop by name."), N_("<desktop name>") },
   { "nonInteractive", 'q', 0, G_OPTION_ARG_NONE, &sOptNonInteractive,
     N_("Connect automatically if enough values are given on the command "
        "line."), NULL },
   { "fullscreen", '\0', 0, G_OPTION_ARG_NONE, &sOptFullscreen,
     N_("Enable fullscreen mode."), NULL },
   { "background", 'b', 0 , G_OPTION_ARG_STRING, &sOptBackground,
     N_("Image file to use as background in fullscreen mode."), N_("<image>") },
   { "redirect", 'r', 0 , G_OPTION_ARG_STRING_ARRAY, &sOptRedirect,
     N_("Forward device redirection to rdesktop"), N_("<device info>") },
   { "version", '\0', 0, G_OPTION_ARG_NONE, &sOptVersion,
     N_("Display version information and exit."), NULL },
   { "usb", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &sOptUsb,
     N_("Options for USB forwarding."), N_("<usb options>") },
   { "mmrPath", 'm', 0, G_OPTION_ARG_STRING, &sOptMMRPath,
     N_("Directory location containing Wyse MMR libraries."), N_("<mmr directory>") },
   { "rdesktopOptions", '\0', 0, G_OPTION_ARG_STRING, &sOptRDesktop,
     N_("Command line options to forward to rdesktop."),
     N_("<rdesktop options>") },
   { NULL }
};


GOptionEntry App::sOptFileEntries[] =
{
   { "file", 'f', 0 , G_OPTION_ARG_STRING, &sOptFile,
     N_("File containing additional command line arguments."),
     N_("<file path>") },
   { NULL }
};


/*
 *-------------------------------------------------------------------
 *
 * cdk::App::App --
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

App::App(int argc,    // IN:
         char **argv) // IN:
   : mWindow(GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL))),
     mToplevelBox(GTK_VBOX(gtk_vbox_new(false, 0))),
     mContentBox(NULL),
     mFullscreenAlign(NULL),
     mBackgroundImage(NULL),
     mDlg(NULL),
     mUseAllMonitors(false)
#ifdef VIEW_ENABLE_WINDOW_MODE
   , mFullScreen(true)
#endif // VIEW_ENABLE_WINDOW_MODE
{
#ifdef USE_GLIB_THREADS
   if (!g_thread_supported()) {
      g_thread_init(NULL);
   }
#endif
   VThread_Init(VTHREAD_UI_ID, VMWARE_VIEW);

   /*
    * XXX: Should use PRODUCT_VERSION_STRING for the third arg, but
    * that doesn't know about the vdi version.
    */
   ProductState_Set(PRODUCT_VDM_CLIENT, PRODUCT_VIEW_CLIENT_NAME,
                    VIEW_CLIENT_VERSION_NUMBER " " BUILD_NUMBER,
                    BUILD_NUMBER_NUMERIC, 0,
                    PRODUCT_VIEW_CLIENT_NAME_FOR_LICENSE,
                    PRODUCT_VERSION_STRING_FOR_LICENSE);

   Poll_InitGtk();
   Preference_Init();
   Sig_Init();

   Log_Init(NULL, VMWARE_VIEW ".log.filename", VMWARE_VIEW);
   IntegrateGLibLogging();
   printf(_("Using log file %s\n"), Log_GetFileName());

   Log("Command line: ");
   for (int i = 0; i < argc; i++) {
      if (i > 1 && (Str_Strcmp(argv[i - 1], "-p") == 0 ||
                     Str_Strcmp(argv[i - 1], "--password") == 0)) {
         Log("[password omitted] ");
      } else if (strstr(argv[i], "--password=") == argv[i]) {
         Log("--password=[password omitted] ");
      } else {
         Log("%s ", argv[i]);
      }
   }
   Log("\n");

   // If we are directly linking, the last 3 args are ignored.
   SSL_InitEx(NULL, NULL, NULL, true, false, false);

   BasicHttp_Init(Poll_Callback, Poll_CallbackRemove);

   sApp = this;

   GOptionContext *context =
      g_option_context_new(_("- connect to VMware View desktops"));
   g_option_context_add_main_entries(context, sOptFileEntries, NULL);

#if GTK_CHECK_VERSION(2, 6, 0)
   g_option_context_add_group(context, gtk_get_option_group(true));
#endif

   /*
    * Only the --file argument will be known to the context when it first
    * parses argv, so we should ignore other arguments (and leave them be)
    * until after the file argument has been fully dealt with.
    */
   g_option_context_set_ignore_unknown_options(context, true);

   g_option_context_set_help_enabled(context, false);

   // First, we only want to parse out the --file option.
   GError *fileError = NULL;
   if (!g_option_context_parse(context, &argc, &argv, &fileError)) {
      Util::UserWarning(_("Error parsing command line: %s\n"),
                        fileError->message);
   }
   /*
    * Hold on to the error--we might get the same message the next time we
    * parse, and we only want to show it once.
    */

   g_option_context_add_main_entries(context, sOptEntries, NULL);

   // If --file was specified and it exists, it will be opened and parsed.
   if (sOptFile) {
      ParseFileArgs();
   }

   /*
    * Now, parse the rest of the options out of argv.  By doing this parsing
    * here, it will allows the commandline options to override the config
    * file options.
    */
   g_option_context_set_ignore_unknown_options(context, false);
   g_option_context_set_help_enabled(context, true);
   GError *error = NULL;
   // Show the error message only if it's not the same as the one shown above.
   if (!g_option_context_parse(context, &argc, &argv, &error) &&
       (!fileError || Str_Strcmp(fileError->message, error->message) != 0)) {
      Util::UserWarning(_("Error parsing command line: %s\n"), error->message);
   }
   g_clear_error(&fileError);
   g_clear_error(&error);

   if (sOptVersion) {
      /*
       * XXX; This should PRODUCT_VERSION_STRING once vdi has its own
       * vm_version.h.
       */
      printf(_(
"%s\n\n"
"VMware is a registered trademark or trademark (the \"Marks\") of VMware, Inc.\n"
"in the United States and/or other jurisdictions and is not licensed to you\n"
"under the terms of the LGPL version 2.1. If you distribute VMware View Open\n"
"Client unmodified in either binary or source form or the accompanying\n"
"documentation unmodified, you may not remove, change, alter or otherwise\n"
"modify the Marks in any manner. If you make minor modifications to VMware\n"
"View Open Client or the accompanying documentation, you may, but are not\n"
"required to, continue to distribute the unaltered Marks with your binary or\n"
"source distributions. If you make major functional changes to VMware View\n"
"Open Client or the accompanying documentation, you may not distribute the\n"
"Marks with your binary or source distribution and you must remove all\n"
"references to the Marks contained in your distribution. All other use or\n"
"distribution of the Marks requires the prior written consent of VMware.\n"
"All other marks and names mentioned herein may be trademarks of their\n"
"respective companies.\n\n"
"Copyright © 1998-2009 VMware, Inc. All rights reserved.\n"
"This product is protected by U.S. and international copyright and\n"
"intellectual property laws.\n"
"VMware software products are protected by one or more patents listed at\n%s\n\n"),
             PRODUCT_VIEW_CLIENT_NAME " " VIEW_CLIENT_VERSION_NUMBER " "
                BUILD_NUMBER,
             // TRANSLATORS: Ignore this; we will localize with appropriate URL.
             _("http://www.vmware.com/go/patents"));
      exit(0);
   }

   if (sOptPassword && Str_Strcmp(sOptPassword, "-") == 0) {
      sOptPassword = getpass(_("Password: "));
   }

   if (sOptNonInteractive) {
      Log("Using non-interactive mode.\n");
   }

   if (!sOptMMRPath) {
      sOptMMRPath = VIEW_DEFAULT_MMR_PATH;
   }

   gtk_widget_show(GTK_WIDGET(mToplevelBox));
   gtk_container_add(GTK_CONTAINER(mWindow), GTK_WIDGET(mToplevelBox));
   g_signal_connect(GTK_WIDGET(mToplevelBox), "size-allocate",
                    G_CALLBACK(&App::OnSizeAllocate), this);

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

   // Quit when window closes.
   g_signal_connect(G_OBJECT(mWindow), "destroy",
                    G_CALLBACK(&gtk_main_quit), NULL);
   g_object_add_weak_pointer(G_OBJECT(mWindow), (gpointer *)&mWindow);

   RequestBroker();

   // Set the window's _NET_WM_USER_TIME from an X server roundtrip.
   Util::OverrideWindowUserTime(mWindow);
   gtk_window_present(mWindow);

   /*
    * This removes the padding around the C-A-D dialog so that the
    * banner goes to the edge of the window.
    */
   gtk_rc_parse_string("style \"ctrl-alt-del-dlg\" {\n"
                       "GtkDialog::content_area_border = 0\n"
                       "GtkDialog::action_area_border = 10\n"
                       "}\n"
                       "widget \"CtrlAltDelDlg\" style \"ctrl-alt-del-dlg\"");
}


/*
 *-------------------------------------------------------------------
 *
 * cdk::App::~App --
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

App::~App()
{
   mDesktopUIExitCnx.disconnect();
   delete mDlg;
   if (mWindow) {
      gtk_widget_destroy(GTK_WIDGET(mWindow));
   }
   Log_Exit();
   Sig_Exit();
}


/*
 *-------------------------------------------------------------------
 *
 * cdk::App::ParseFileArgs --
 *
 *      Parses additional options from a file.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-------------------------------------------------------------------
 */

void
App::ParseFileArgs()
{
   GOptionContext *context =
      g_option_context_new(_("- connect to VMware View desktops"));
   g_option_context_add_main_entries(context, sOptEntries, NULL);

   gchar *contents = NULL;
   gsize length = 0;

   GError *error = NULL;
   gint argcp = 0;
   gchar **argvp = NULL;

   if (!g_file_get_contents(sOptFile, &contents, &length, &error) ||
       !g_shell_parse_argv(Util::Format(VMWARE_VIEW " %s", contents).c_str(),
            &argcp, &argvp, &error) ||
       !g_option_context_parse(context, &argcp, &argvp, &error)) {
      Util::UserWarning(_("Error parsing %s: %s\n"), sOptFile,
                        error->message ? error->message : _("Unknown error"));
   }

   g_strfreev(argvp);
   g_free(contents);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::IntegrateGLibLogging --
 *
 *      Replace the default GLib printerr and log handlers with our own
 *      functions so that these will be logged and/or suppressed like our
 *      internal messages.
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
App::IntegrateGLibLogging(void)
{
   g_set_printerr_handler((GPrintFunc)Warning);
   g_log_set_default_handler((GLogFunc)App::OnGLibLog, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnGLibLog --
 *
 *      Our replacement for GLib's default log handler.
 *
 *      Ripped from bora/apps/lib/lui/utils.cc.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Application will be aborted if a fatal error is passed.
 *
 *-----------------------------------------------------------------------------
 */

void
App::OnGLibLog(const gchar *domain,  // IN
               GLogLevelFlags level, // IN
               const gchar *message) // IN
{
   // Both Panic and Warning implicitly log.
   if (level & (G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR)) {
      Panic("%s: %s\n", domain, message);
   } else {
      Warning("%s: %s\n", domain, message);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::InitWindow --
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
App::InitWindow()
{
   mContentBox = GTK_VBOX(gtk_vbox_new(false, VM_SPACING));
   gtk_widget_show(GTK_WIDGET(mContentBox));
   g_object_add_weak_pointer(G_OBJECT(mContentBox) , (gpointer *)&mContentBox);

   // If a background image was specified, go into fullscreen mode.
   if (sOptFullscreen || sOptBackground) {
      /*
       * http://www.vmware.com/files/pdf/VMware_Logo_Usage_and_Trademark_Guidelines_Q307.pdf
       *
       * VMware Blue is Pantone 645 C or 645 U (R 116, G 152, B 191 = #7498bf).
       */
      GdkColor blue;
      gdk_color_parse("#7498bf", &blue);
      gtk_widget_modify_bg(GTK_WIDGET(mWindow), GTK_STATE_NORMAL, &blue);

      g_signal_connect(GTK_WIDGET(mWindow), "realize",
         G_CALLBACK(&App::FullscreenWindow), NULL);

      GtkFixed *fixed = GTK_FIXED(gtk_fixed_new());
      gtk_widget_show(GTK_WIDGET(fixed));
      gtk_box_pack_start(GTK_BOX(mToplevelBox), GTK_WIDGET(fixed), true, true,
                         0);

      if (sOptBackground) {
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
                    G_CALLBACK(&App::OnKeyPress), this);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SetContent --
 *
 *      Removes previous mDlg, if necessary, and puts the dialog's
 *      content in its place.
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
App::SetContent(Dlg *dlg) // IN
{
   ASSERT(dlg != mDlg);
   if (mDlg) {
      if (dynamic_cast<RDesktop *>(mDlg)) {
         mDesktopUIExitCnx.disconnect();
      }
      delete mDlg;
   }
   mDlg = dlg;
   GtkWidget *content = mDlg->GetContent();
   gtk_widget_show(content);

   if (dynamic_cast<RDesktop *>(mDlg)) {
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
   if (!sOptFullscreen && !sOptBackground) {
      gtk_window_set_resizable(GTK_WINDOW(mWindow), mDlg->IsResizable());
      gtk_container_check_resize(GTK_CONTAINER(mWindow));
   }
   if (dynamic_cast<RDesktop *>(mDlg)) {
      g_signal_handlers_disconnect_by_func(
         mWindow, (gpointer)&App::OnKeyPress, this);
      // XXX: This call may fail.  Should monitor the
      //      window_state_event signal, and either restart rdesktop
      //      if we exit fullscreen, or don't start it until we enter
      //      fullscreen.
#ifdef VIEW_ENABLE_WINDOW_MODE
      if (mFullScreen)
#endif // VIEW_ENABLE_WINDOW_MODE
      {
         FullscreenWindow(mWindow, mUseAllMonitors ? &mMonitorBounds : NULL);
      }
   }
   mDlg->cancel.connect(boost::bind(&App::OnCancel, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SetBusy --
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
App::SetBusy(const Util::string &message) // IN
{
   Log("Busy: %s\n", message.c_str());
   mDlg->SetSensitive(false);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SetReady --
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
App::SetReady()
{
   mDlg->SetSensitive(true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::UpdateDesktops --
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
App::UpdateDesktops()
{
   // Only do this if we still have the DesktopSelectDlg around.
   DesktopSelectDlg *dlg = dynamic_cast<DesktopSelectDlg *>(mDlg);
   if (dlg) {
      dlg->UpdateList(mDesktops);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestBroker --
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
App::RequestBroker()
{
   static bool firstTimeThrough = true;
   Reset();
   Util::string defaultBroker(Prefs::GetPrefs()->GetDefaultBroker());
   BrokerDlg *brokerDlg = new BrokerDlg(sOptBroker ? sOptBroker :
                                        defaultBroker);
   SetContent(brokerDlg);
   brokerDlg->connect.connect(boost::bind(&App::DoInitialize, this));

   // Hit the Connect button if broker was supplied and we're non-interactive.
   if ((sOptBroker && sOptNonInteractive) ||
       (!sOptBroker && firstTimeThrough && !defaultBroker.empty())) {
      DoInitialize();
   }
   firstTimeThrough = false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestScInsertPrompt --
 *
 *      Request that the user insert a smart card.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Insert card dialog is shown
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestScInsertPrompt(Cryptoki *cryptoki) // IN
{
   ScInsertPromptDlg *dlg = new ScInsertPromptDlg(cryptoki);
   SetContent(dlg);
   dlg->next.connect(boost::bind(&App::DoSubmitScInsertPrompt, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestScPin --
 *
 *      Request a Smart Card PIN from the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      PIN dialog is shown.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestScPin(const Util::string &tokenName, // IN
                  const X509 *x509)              // IN
{
   ScPinDlg *dlg = new ScPinDlg();
   SetContent(dlg);
   dlg->SetTokenName(tokenName);
   dlg->SetCertificate(x509);
   dlg->login.connect(boost::bind(&App::DoSubmitScPin, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestCertificate --
 *
 *      Request a certificate from the user.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Certificate dialog is shown.
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestCertificate(std::list<X509 *> &certs)
{
   ScCertDlg *dlg = new ScCertDlg();
   SetContent(dlg);
   dlg->SetCertificates(certs);
   dlg->select.connect(boost::bind(&App::DoSubmitCertificate, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestDisclaimer --
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
App::RequestDisclaimer(const Util::string &disclaimer) // IN
{
   DisclaimerDlg *dlg = new DisclaimerDlg();
   SetContent(dlg);
   dlg->SetText(disclaimer);
   dlg->accepted.connect(boost::bind(&App::AcceptDisclaimer, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPasscode --
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
App::RequestPasscode(const Util::string &username) // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   dlg->SetState(SecurIDDlg::STATE_PASSCODE, username);
   dlg->authenticate.connect(boost::bind(&App::DoSubmitPasscode, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestNextTokencode --
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
App::RequestNextTokencode(const Util::string &username) // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   dlg->SetState(SecurIDDlg::STATE_NEXT_TOKEN, username);
   dlg->authenticate.connect(boost::bind(&App::DoSubmitNextTokencode, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPinChange --
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
App::RequestPinChange(const Util::string &pin,     // IN
                      const Util::string &message, // IN
                      bool userSelectable)         // IN
{
   SecurIDDlg *dlg = new SecurIDDlg();
   SetContent(dlg);
   dlg->SetState(SecurIDDlg::STATE_SET_PIN, pin, message, userSelectable);
   dlg->authenticate.connect(boost::bind(&App::DoSubmitPins, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPassword --
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
App::RequestPassword(const Util::string &username,             // IN
                     bool readOnly,                            // IN
                     const std::vector<Util::string> &domains, // IN
                     const Util::string &suggestedDomain)      // IN
{
   LoginDlg *dlg = new LoginDlg();
   SetContent(dlg);

   /*
    * Turn off non-interactive mode if the suggested username differs from
    * the one passed on the command line. We want to use the username
    * returned by the server, but should let the user change it before
    * attempting to authenticate.
    */
   if (sOptUser && Str_Strcasecmp(username.c_str(), sOptUser) != 0) {
      sOptNonInteractive = false;
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
   Util::string domainPref = Prefs::GetPrefs()->GetDefaultDomain();
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

   if (!domainFound && sOptDomain &&
       Str_Strcasecmp(suggestedDomain.c_str(), sOptDomain) == 0) {
      Util::UserWarning(_("Command-line option domain \"%s\" is not in the "
                          "list returned by the server.\n"), sOptDomain);
   }
   if (domain.empty() && domains.size() > 0) {
      domain = domains[0];
   }

   dlg->SetFields(username, readOnly, sOptPassword ? sOptPassword : "",
                  domains, domain);
   dlg->login.connect(boost::bind(&App::DoSubmitPassword, this));
   if (sOptNonInteractive && !username.empty() &&
       ((sOptDomain && domainFound) || domains.size() == 1) && sOptPassword) {
      DoSubmitPassword();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestPasswordChange --
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
App::RequestPasswordChange(const Util::string &username, // IN
                           const Util::string &domain)   // IN
{
   PasswordDlg *dlg = new PasswordDlg();
   SetContent(dlg);

   // Domain is locked, so just create a vector with it as the only value.
   std::vector<Util::string> domains;
   domains.push_back(domain);

   dlg->SetFields(username, true, "", domains, domain);
   dlg->login.connect(boost::bind(&App::DoChangePassword, this));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestDesktop --
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
App::RequestDesktop()
{
   Util::string initialDesktop = "";
   /*
    * Iterate through desktops. If the passed-in desktop name is found,
    * pass it as initially-selected. Otherwise use a desktop with the
    * "alwaysConnect" user preference.
    */
   for (std::vector<Desktop *>::iterator i = mDesktops.begin();
        i != mDesktops.end(); i++) {
      Util::string name = (*i)->GetName();
      if (sOptDesktop && name == sOptDesktop) {
         initialDesktop = sOptDesktop;
         break;
      } else if ((*i)->GetAutoConnect()) {
         initialDesktop = name;
      }
   }
   if (sOptDesktop && initialDesktop != sOptDesktop) {
      Util::UserWarning(_("Command-line option desktop \"%s\" is not in the "
                          "list returned by the server.\n"),
                        sOptDesktop);
   }

   int monitors = gdk_screen_get_n_monitors(gtk_window_get_screen(mWindow));
   Log("Number of monitors on this screen is %d.\n", monitors);

   bool supported =
      gdk_net_wm_supports(gdk_atom_intern("_NET_WM_FULLSCREEN_MONITORS",
                                          FALSE));
   Log("Current window manager %s _NET_WM_FULLSCREEN_MONITORS message.\n",
       supported ? "supports" : "does not support");

   DesktopSelectDlg *dlg = new DesktopSelectDlg(mDesktops, initialDesktop,
                                                monitors > 1 && supported
#ifdef VIEW_ENABLE_WINDOW_MODE
                                                , !sOptFullscreen && !sOptBackground
#endif // VIEW_ENABLE_WINDOW_MODE
                                                );
   SetContent(dlg);
   dlg->action.connect(boost::bind(&App::DoDesktopAction, this, _1));

   // Hit Connect button when non-interactive
   if (sOptNonInteractive &&
       (!initialDesktop.empty() || mDesktops.size() == 1)) {
      dlg->action(DesktopSelectDlg::ACTION_CONNECT);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::RequestTransition --
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
App::RequestTransition(const Util::string &message)
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
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::Quit --
 *
 *      Handle succesful logout command.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Destroys main window, which exits the app.
 *
 *-----------------------------------------------------------------------------
 */

void
App::Quit()
{
   gtk_widget_destroy(GTK_WIDGET(mWindow));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoInitialize --
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
App::DoInitialize()
{
   ASSERT(mDlg);

   BrokerDlg *brokerDlg = dynamic_cast<BrokerDlg *>(mDlg);
   ASSERT(brokerDlg);
   if (brokerDlg->GetBroker().empty()) {
      return;
   }
   Prefs *prefs = Prefs::GetPrefs();
   Initialize(brokerDlg->GetBroker(), brokerDlg->GetPort(),
              brokerDlg->GetSecure(),
              sOptUser ? sOptUser : prefs->GetDefaultUser(),
              // We'll use the domain pref later if need be.
              sOptDomain ? sOptDomain : "");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitScInsertPrompt --
 *
 *      Callback for the insert card dialog's next button.  Tell the
 *      broker to continue.
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
App::DoSubmitScInsertPrompt()
{
   SubmitScInsertPrompt(true);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitScPin --
 *
 *      Callback for the PIN dialog's login signal.  Supplies a PIN to
 *      the broker.
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
App::DoSubmitScPin()
{
   ScPinDlg *dlg = dynamic_cast<ScPinDlg *>(mDlg);
   ASSERT(dlg);
   SubmitScPin(dlg->GetPin());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitCertificate --
 *
 *      Callback for the certificate dialog's select signal.  Provides
 *      a certificate to the broker.
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
App::DoSubmitCertificate()
{
   ScCertDlg *dlg = dynamic_cast<ScCertDlg *>(mDlg);
   ASSERT(dlg);
   SubmitCertificate(dlg->GetCertificate());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitPasscode --
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
App::DoSubmitPasscode()
{
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);

   Util::string user = dlg->GetUsername();
   Prefs::GetPrefs()->SetDefaultUser(user);

   SubmitPasscode(user, dlg->GetPasscode());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitNextTokencode --
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
App::DoSubmitNextTokencode()
{
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);
   SubmitNextTokencode(dlg->GetPasscode());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitPins --
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
App::DoSubmitPins()
{
   SecurIDDlg *dlg = dynamic_cast<SecurIDDlg *>(mDlg);
   ASSERT(dlg);
   std::pair<Util::string, Util::string> pins = dlg->GetPins();
   if (pins.first != pins.second) {
      App::ShowDialog(GTK_MESSAGE_ERROR, _("The PINs do not match."));
   } else {
      SubmitPins(pins.first, pins.second);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoSubmitPassword --
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
App::DoSubmitPassword()
{
   LoginDlg *dlg = dynamic_cast<LoginDlg *>(mDlg);
   ASSERT(dlg);

   Util::string user = dlg->GetUsername();
   Util::string domain = dlg->GetDomain();

   Prefs *prefs = Prefs::GetPrefs();
   prefs->SetDefaultUser(user);
   prefs->SetDefaultDomain(domain);

   SubmitPassword(user, dlg->GetPassword(), domain);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoChangePassword --
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
cdk::App::DoChangePassword()
{
   PasswordDlg *dlg = dynamic_cast<PasswordDlg *>(mDlg);
   ASSERT(dlg);

   std::pair<Util::string, Util::string> pwords = dlg->GetNewPassword();
   if (pwords.first != pwords.second) {
      App::ShowDialog(GTK_MESSAGE_ERROR, _("The passwords do not match."));
   } else {
      ChangePassword(dlg->GetPassword(), pwords.first, pwords.second);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::DoDesktopAction --
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
App::DoDesktopAction(DesktopSelectDlg::Action action) // IN
{
   DesktopSelectDlg *dlg = dynamic_cast<DesktopSelectDlg *>(mDlg);
   ASSERT(dlg);
   Desktop *desktop = dlg->GetDesktop();
   ASSERT(desktop);

   switch (action) {
   case DesktopSelectDlg::ACTION_CONNECT:
#ifdef VIEW_ENABLE_WINDOW_MODE
      mFullScreen = dlg->GetDesktopSize(&mDesktopSize, &mUseAllMonitors);
#else
      mUseAllMonitors = dlg->GetUseAllMonitors();
#endif // VIEW_ENABLE_WINDOW_MODE
      ConnectDesktop(desktop);
      break;
   case DesktopSelectDlg::ACTION_RESET:
      ResetDesktop(desktop);
      break;
   case DesktopSelectDlg::ACTION_KILL_SESSION:
      KillSession(desktop);
      break;
   case DesktopSelectDlg::ACTION_ROLLBACK:
      RollbackDesktop(desktop);
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::TunnelDisconnected --
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
App::TunnelDisconnected(Util::string disconnectReason) // IN
{
   /*
    * rdesktop will probably exit shortly, and we want the user to see
    * our dialog before we exit
    */
   mDesktopUIExitCnx.disconnect();

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
 * cdk::App::OnSizeAllocate --
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
App::OnSizeAllocate(GtkWidget *widget,         // IN/UNUSED
                    GtkAllocation *allocation, // IN
                    gpointer userData)         // IN
{
   App *that = reinterpret_cast<App *>(userData);
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
   if (that->mBackgroundImage) {
      that->ResizeBackground(allocation);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::CreateBanner --
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
App::CreateBanner()
{
   GdkPixbuf *pb = gdk_pixbuf_new_from_inline(-1, view_client_banner, false,
                                              NULL);
   ASSERT(pb);

   GtkWidget *img = gtk_image_new_from_pixbuf(pb);
   gtk_misc_set_alignment(GTK_MISC(img), 0.0, 0.5);
   // Sets the minimum width, to avoid clipping banner logo text
   gtk_widget_set_size_request(GTK_WIDGET(img), 480, -1);
   g_signal_connect(img, "size-allocate",
                    G_CALLBACK(&App::OnBannerSizeAllocate), NULL);
   g_object_unref(pb);
   return img;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnBannerSizeAllocate --
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
App::OnBannerSizeAllocate(GtkWidget *image,          // IN
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
 * cdk::App::ResizeBackground --
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
App::ResizeBackground(GtkAllocation *allocation) // IN
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
   pixbuf = gdk_pixbuf_new_from_file_at_size(sOptBackground, -1,
                                             allocation->height, &error);
   if (error) {
      Util::UserWarning(_("Unable to load background image '%s': %s\n"),
                        sOptBackground,
                        error->message ? error->message : _("Unknown error"));
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
 * cdk::App::RequestLaunchDesktop --
 *
 *      Starts an rdesktop session and embeds it into the main window, and
 *      causes the main window to enter fullscreen.
 *
 * Results:
 *      false
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
App::RequestLaunchDesktop(Desktop *desktop) // IN
{
   ASSERT(desktop);

   SetReady();
   Log("Desktop connect successful.  Starting rdesktop...\n");
   if (sOptNonInteractive) {
      Log("Disabling non-interactive mode.\n");
      sOptNonInteractive = false;
   }

   RequestTransition(_("Connecting to the desktop..."));

   Dlg *dlg = desktop->GetUIDlg();
   mDlg->cancel.connect(boost::bind(&App::OnDesktopUICancel, this, dlg));

   /*
    * Once rdesktop connects, set it as the content dlg.
    */
   RDesktop *rdesktop = dynamic_cast<RDesktop*>(dlg);
   if (rdesktop) {
      rdesktop->onConnect.connect(boost::bind(&App::SetContent, this, dlg));
      rdesktop->onCtrlAltDel.connect(boost::bind(&App::OnCtrlAltDel, this));
      gtk_box_pack_start(GTK_BOX(mToplevelBox), dlg->GetContent(), false, false,
                         0);
      gtk_widget_realize(dlg->GetContent());
   }

   /*
    * Handle rdesktop exit by restarting rdesktop, quitting, or
    * showing a warning dialog.
    */
   ProcHelper *procHelper = dynamic_cast<ProcHelper*>(dlg);
   if (procHelper) {
      mDesktopUIExitCnx = procHelper->onExit.connect(
         boost::bind(&App::OnDesktopUIExit, this, dlg, _1));
   }

   PushDesktopEnvironment();

   // Collect all the -r options.
   std::vector<Util::string> devRedirects = GetSmartCardRedirects();
   for (gchar **redir = sOptRedirect; redir && *redir; redir++) {
      devRedirects.push_back(*redir);
   }

   // Collect all the --usb options
   std::vector<Util::string> usbRedirects;
   for (gchar **usbRedir = sOptUsb; usbRedir && *usbRedir; usbRedir++) {
      usbRedirects.push_back(*usbRedir);
   }

   GdkRectangle geometry;
#ifdef VIEW_ENABLE_WINDOW_MODE
   if (mUseAllMonitors) {
      ASSERT(mFullScreen);
   }
   if (!mFullScreen) {
      geometry = mDesktopSize;
   } else
#endif // VIEW_ENABLE_WINDOW_MODE
   {
      GetFullscreenGeometry(mUseAllMonitors, &geometry,
                            mUseAllMonitors ? &mMonitorBounds : NULL);
   }

   Log("Connecting to desktop with total geometry %dx%d.\n",
       geometry.width, geometry.height);
   desktop->StartUI(&geometry, devRedirects, usbRedirects);

   PopDesktopEnvironment();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::GetFullscreenGeometry --
 *
 *      If allMonitors is true,
 *      computes the rectangle that is the union of all monitors.
 *      Otherwise computes the rectangle of the current monitor.
 *      If allMonitors is true and bounds is non-NULL,
 *      determines the appropriate monitor indices for
 *      sending the _NET_WM_FULLSCREEN_MONITORS message.
 *
 * Results:
 *      geometry: GdkRectangle representing union of all monitors.
 *      bounds: MonitorBounds containing arguments to
 *      _NET_WM_FULLSCREEN_MONITORS message.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
App::GetFullscreenGeometry(bool allMonitors,       // IN
                           GdkRectangle *geometry, // OUT
                           MonitorBounds *bounds)  // OUT/OPT
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
 * cdk::App::FullscreenWindow --
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
App::FullscreenWindow(GtkWindow *win,        // IN
                      MonitorBounds *bounds) // IN/OPT
{
   GdkScreen *screen = gtk_window_get_screen(win);
   ASSERT(screen);

   if (gdk_net_wm_supports(gdk_atom_intern("_NET_WM_STATE_FULLSCREEN",
                                           FALSE))) {
      Log("Attempting to fullscreen window using _NET_WM_STATE_FULLSCREEN"
          " hint.\n");
      // The window manager supports fullscreening the window on its own.
      gtk_window_fullscreen(win);
      if (bounds &&
          gdk_net_wm_supports(gdk_atom_intern("_NET_WM_FULLSCREEN_MONITORS",
                                              FALSE))) {
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
 * cdk::App::ShowDialog --
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
App::ShowDialog(GtkMessageType type,       // IN
                const Util::string format, // IN
                ...)
{
   ASSERT(sApp);
   /*
    * It would be nice if there was a va_list variant of
    * gtk_message_dialog_new().
    */
   va_list args;
   va_start(args, format);
   Util::string label = Util::FormatV(format.c_str(), args);
   va_end(args);

   if (sOptNonInteractive) {
      Log("ShowDialog: %s; Turning off non-interactive mode.\n", label.c_str());
      sOptNonInteractive = false;
   }

   /*
    * If we're trying to connect, or have already connected, show the
    * error using the transition page.
    */
   if (type == GTK_MESSAGE_ERROR &&
       (dynamic_cast<TransitionDlg *>(sApp->mDlg) ||
        dynamic_cast<RDesktop *>(sApp->mDlg))) {
      /*
       * We may get a tunnel error/message while the Desktop::Connect RPC is
       * still in flight, which puts us here. If so, and the user clicks
       * Retry before the RPC completes, Broker::ReconnectDesktop will fail
       * the assertion (state != CONNECTING). So cancel all requests before
       * allowing the user to retry.
       */
      sApp->CancelRequests();
      TransitionDlg *dlg = new TransitionDlg(TransitionDlg::TRANSITION_ERROR,
                                             label);
      dlg->SetStock(GTK_STOCK_DIALOG_ERROR);
      sApp->SetContent(dlg);
      dlg->retry.connect(boost::bind(&App::ReconnectDesktop, sApp));
   } else {
      GtkWidget *dialog = gtk_message_dialog_new(
         sApp->mWindow, GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK,
         "%s", label.c_str());
      gtk_widget_show(dialog);
      gtk_window_set_title(GTK_WINDOW(dialog),
                           gtk_window_get_title(sApp->mWindow));
      g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy),
                       NULL);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnCancel --
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
App::OnCancel()
{
   if (sOptNonInteractive) {
      Log("User cancelled; turning off non-interactive mode.\n");
      sOptNonInteractive = false;
   }
   Log("User cancelled.\n");
   if (mDlg->IsSensitive()) {
      TransitionDlg *dlg = dynamic_cast<TransitionDlg *>(mDlg);
      if (dynamic_cast<BrokerDlg *>(mDlg)) {
         Quit();
      } else if (dynamic_cast<ScInsertPromptDlg *>(mDlg)) {
         SubmitScInsertPrompt(false);
      } else if (dynamic_cast<ScPinDlg *>(mDlg)) {
         SubmitScPin(NULL);
      } else if (dynamic_cast<ScCertDlg *>(mDlg)) {
         SubmitCertificate(NULL);
      } else if (dlg) {
         if (dlg->GetTransitionType() == TransitionDlg::TRANSITION_PROGRESS) {
            CancelRequests();
         }
         LoadDesktops();
      } else {
         RequestBroker();
      }
   } else {
      CancelRequests();
      if (dynamic_cast<ScPinDlg *>(mDlg) ||
          dynamic_cast<ScCertDlg *>(mDlg)) {
         RequestBroker();
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::PushDesktopEnvironment --
 *
 *      Updates the DISPLAY environment variable according to the
 *      GdkScreen mWindow is on and the LD_LIBRARY_PATH and GST_PLUGIN_PATH
 *      variables to include the MMR path (if MMR is enabled).
 *
 * Results:
 *      None
 *
 * Side effects:
 *      DISPLAY is updated and LD_LIBRARY_PATH may be updated.
 *
 *-----------------------------------------------------------------------------
 */

void
App::PushDesktopEnvironment()
{
   char *dpy = gdk_screen_make_display_name(gtk_window_get_screen(mWindow));
   setenv("DISPLAY", dpy, true);
   g_free(dpy);

   if (GetDesktop()->GetIsMMREnabled() && strlen(sOptMMRPath) != 0) {
      const char *ldpath = getenv("LD_LIBRARY_PATH");
      mOrigLDPath = ldpath ? ldpath : "";

      Util::string env = Util::Format("%s%s%s",
                                      mOrigLDPath.c_str(),
                                      mOrigLDPath.empty() ? "" : ":",
                                      sOptMMRPath);
      setenv("LD_LIBRARY_PATH", env.c_str(), true);

      const char *gstpath = getenv("GST_PLUGIN_PATH");
      mOrigGSTPath = gstpath ? gstpath : "";

      char *newPath = g_build_filename(sOptMMRPath, "gstreamer", NULL);
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
 * cdk::App::PopDesktopEnvironment --
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
App::PopDesktopEnvironment()
{
   setenv("LD_LIBRARY_PATH", mOrigLDPath.c_str(), true);
   mOrigLDPath.clear();

   setenv("GST_PLUGIN_PATH", mOrigGSTPath.c_str(), true);
   mOrigGSTPath.clear();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnKeyPress --
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
App::OnKeyPress(GtkWidget *widget, // IN/UNUSED
                GdkEventKey *evt,  // IN
                gpointer userData) // IN
{
   App *that = reinterpret_cast<App*>(userData);
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
      that->mDlg->Cancel();
      return true;
   } else if (GDK_F5 == evt->keyval && !modKeyPressed &&
              dynamic_cast<DesktopSelectDlg *>(that->mDlg)) {
      that->GetDesktops(true);
      return true;
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnCtrlAltDel --
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
App::OnCtrlAltDel()
{
   Desktop *desktop = GetDesktop();
   ASSERT(desktop);

   GtkWidget *d = gtk_message_dialog_new(
      mWindow, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
      _("You are connected to %s.\n\n"
        "If this desktop is unresponsive, click Disconnect."),
      desktop->GetName().c_str());
   gtk_window_set_title(GTK_WINDOW(d), gtk_window_get_title(mWindow));
   gtk_container_set_border_width(GTK_CONTAINER(d), 0);
   gtk_widget_set_name(d, "CtrlAltDelDlg");

   GtkWidget *img = CreateBanner();
   gtk_widget_show(img);
   gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), img, false, false, 0);
   gtk_box_reorder_child(GTK_BOX(GTK_DIALOG(d)->vbox), img, 0);

   gtk_dialog_add_buttons(GTK_DIALOG(d),
                          _("Send C_trl-Alt-Del"), RESPONSE_CTRL_ALT_DEL,
                          _("_Disconnect"), RESPONSE_DISCONNECT,
                          desktop->CanReset() || desktop->CanResetSession()
                             ? _("_Reset") : NULL,
                          RESPONSE_RESET,
                          NULL);
   gtk_dialog_add_action_widget(
      GTK_DIALOG(d), GTK_WIDGET(Util::CreateButton(GTK_STOCK_CANCEL)),
      GTK_RESPONSE_CANCEL);

   // Widget must be shown to do grabs on it.
   gtk_widget_show(d);

   /*
    * Grab the keyboard and mouse; our rdesktop window currently has
    * the keyboard grab, which we need here to have keyboard
    * focus/navigation.
    */
   GdkGrabStatus kbdStatus = gdk_keyboard_grab(d->window, false,
                                               GDK_CURRENT_TIME);
   GdkGrabStatus mouseStatus =
      gdk_pointer_grab(d->window, true,
                       (GdkEventMask)(GDK_POINTER_MOTION_MASK |
                                      GDK_POINTER_MOTION_HINT_MASK |
                                      GDK_BUTTON_MOTION_MASK |
                                      GDK_BUTTON1_MOTION_MASK |
                                      GDK_BUTTON2_MOTION_MASK |
                                      GDK_BUTTON3_MOTION_MASK |
                                      GDK_BUTTON_PRESS_MASK |
                                      GDK_BUTTON_RELEASE_MASK),
                       NULL, NULL, GDK_CURRENT_TIME);

   int response = gtk_dialog_run(GTK_DIALOG(d));
   gtk_widget_destroy(d);

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
      Quit();
      return true;
   case RESPONSE_RESET:
      ResetDesktop(GetDesktop(), true);
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
 * cdk::App::OnDesktopUIExit --
 *
 *      Handle rdesktop exiting.  If rdesktop has exited too many
 *      times recently, give up and exit.
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
App::OnDesktopUIExit(Dlg *dlg,   // IN
                     int status) // IN
{
   RDesktop *rdesktop = dynamic_cast<RDesktop*>(dlg);

   if (status && rdesktop && rdesktop->GetHasConnected() &&
       !mRDesktopMonitor.ShouldThrottle()) {
      ReconnectDesktop();
   } else if (!status) {
      Quit();
   } else {
      // The ShowDialog() below will delete rdesktop if it is mDlg.
      if (dlg != mDlg) {
         delete dlg;
      }
      mRDesktopMonitor.Reset();
      ShowDialog(GTK_MESSAGE_ERROR,
                 _("The desktop has unexpectedly disconnected."));
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::OnDesktopUICancel --
 *
 *      Extra handler for the "Connecting to desktop..." transition's
 *      cancel handler, to free the rdesktop associated with it.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Deletes associated rdesktop.
 *
 *-----------------------------------------------------------------------------
 */

void
App::OnDesktopUICancel(Dlg *dlg) // IN
{
   mDesktopUIExitCnx.disconnect();
   delete dlg;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::GetRDesktopOptions --
 *
 *      Returns a vector containing the command line arguments supplied by
 *      the user to pass to rdesktop.
 *
 * Results:
 *      A vector containing rdesktop options.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

const std::vector<Util::string>
App::GetRDesktopOptions()
{
   std::vector<Util::string> ret;

   if (!sOptRDesktop) {
      return ret;
   }

   char **args = NULL;
   GError *error = NULL;

   if (g_shell_parse_argv(sOptRDesktop, NULL, &args, &error)) {
      for (char **arg = args; *arg; arg++) {
         ret.push_back(*arg);
      }
      g_strfreev(args);
   } else {
      Log("Error retrieving rdesktop options: %s", error->message);
      g_error_free(error);
   }

   return ret;
}


} // namespace cdk
