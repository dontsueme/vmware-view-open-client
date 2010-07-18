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


#include "app.hh"
#include "kioskWindow.hh"
#include "prefs.hh"


extern "C" {
#include "preference.h"
}


namespace cdk {


/*
 *-----------------------------------------------------------------------------
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
 *-----------------------------------------------------------------------------
 */

App::App()
   : mWindow(NULL)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::~App --
 *
 *      Destructor - free our window.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

App::~App()
{
   delete mWindow;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::Main --
 *
 *      Main application for Gtk.
 *
 * Results:
 *      0 on success, non-zero on error.
 *
 * Side effects:
 *      Runs the application.
 *
 *-----------------------------------------------------------------------------
 */

int
App::Main(int argc,     // IN
          char *argv[]) // IN
{
   Util::string wmname;

   if (!Init(argc, argv)) {
      return 1;
   }

   Preference_Init();

   Log("Using gtk+ version %d.%d.%d\n",
       gtk_major_version, gtk_minor_version, gtk_micro_version);

   /*
    * Set GDK_NATIVE_WINDOWS=1 so that a native XID is used for all
    * GdkWindow's on gtk+ 2.18+.  A native XID is required so that our
    * GtkSocket/GtkPlug widgets can handle mouse and keyboard events.
    *
    * Ideally, we would use gdk_window_ensure_native() from a dlsym to force
    * the native xwindow.  However, that doesn't seem to work.
    */
   g_setenv("GDK_NATIVE_WINDOWS", "1", FALSE);

   /*
    * This needs to go after bindtextdomain so it handles GOption localization
    * properly.
    */
   gtk_init(&argc, &argv);

   wmname = GetWindowManagerName();
   Log("Using %s window manager\n",
       wmname.empty() ? "unknown" : wmname.c_str());

   // And then our args.
   Prefs::GetPrefs()->ParseArgs(&argc, &argv);

   /*
    * This removes the padding around the C-A-D dialog so that the
    * banner goes to the edge of the window.
    */
   gtk_rc_parse_string("style \"ctrl-alt-del-dlg\" {\n"
                       "GtkDialog::content_area_border = 0\n"
                       "GtkDialog::action_area_border = 10\n"
                       "}\n"
                       "widget \"CtrlAltDelDlg\" style \"ctrl-alt-del-dlg\"");

   // Build the UI
   mWindow = CreateAppWindow();

   // Quit when window closes.
   g_signal_connect(G_OBJECT(mWindow->GetWindow()), "destroy",
                    G_CALLBACK(gtk_main_quit), NULL);

   /*
    * So, building the UI needs to be after the constructor in case
    * any subclasses override things such as GetFullscreen().
    */
   mWindow->RequestBroker();

   mWindow->Show();

#ifdef VIEW_POSIX
   Sig_Callback(SIGTERM, SIG_SAFE,
                (SigCallbackFunc)SigTermHandlerHelper, this);
#endif

   gtk_main();

   Fini();

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::CreateAppWindow --
 *
 *      Creates a new application Window.
 *
 * Results:
 *      A new Window.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Window *
App::CreateAppWindow()
{
   return Prefs::GetPrefs()->GetKioskMode() ? new KioskWindow() : new Window();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::ShowErrorDialog --
 *
 *      Show an error dialog.
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
App::ShowErrorDialog(const Util::string &message, // IN
                     const Util::string &details, // IN
                     va_list args)                // IN
{
   ASSERT(mWindow);
   mWindow->ShowMessageDialog(GTK_MESSAGE_ERROR, message, details, args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::ShowInfoDialog --
 *
 *      Show an information dialog.
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
App::ShowInfoDialog(const Util::string &message, // IN
                    const Util::string &details, // IN
                    va_list args)                // IN
{
   ASSERT(mWindow);
   mWindow->ShowMessageDialog(GTK_MESSAGE_INFO, message, details, args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::ShowWarningDialog --
 *
 *      Show a warning dialog.
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
App::ShowWarningDialog(const Util::string &message, // IN
                       const Util::string &details, // IN
                       va_list args)                // IN
{
   ASSERT(mWindow);
   mWindow->ShowMessageDialog(GTK_MESSAGE_WARNING, message, details, args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::GetLocaleDir --
 *
 *      Get the locale dir to use.  Typically this will be in our
 *      prefix, but if running from the tarball, we look relative to
 *      our binary.
 *
 * Results:
 *      The locale dir to use.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
App::GetLocaleDir()
{
   // XXX - this path is very likely incorrect for Windows.
   char *localePath = g_build_filename("..", "share", "locale", NULL);
   Util::string localeDir = Util::GetUsefulPath(LOCALEDIR, localePath);
   if (localeDir.empty()) {
      Util::UserWarning(_("Could not find locale directory; falling back "
                          "to %s\n"), LOCALEDIR);
      localeDir = LOCALEDIR;
   }
   g_free(localePath);
   return localeDir;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SigTermHandlerHelper --
 *
 *      Helper function for SIGTERM handler.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef VIEW_POSIX
/* static */ void
App::SigTermHandlerHelper(int s,            // IN
                          siginfo_t *info,  // IN
                          void *clientData) // IN
{
   App *that = reinterpret_cast<App *>(clientData);
   ASSERT(that);
   that->SigTermHandler(s, info);
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::SigTermHandler --
 *
 *      Handler for SIGTERM.  Close window so that we exit gracefully.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Program is exited.
 *
 *-----------------------------------------------------------------------------
 */

#ifdef VIEW_POSIX
void
App::SigTermHandler(int s,            // IN
                    siginfo_t *info)  // IN/UNUSED
{
   ASSERT(s == SIGTERM);
   ASSERT(mWindow != NULL);
   Warning("Received signal %d. Exiting.\n", s);
   mWindow->Close();
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::TriageError --
 *
 *      Analyze error conditions and respond accordingly.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May exit the process on non-zero error, depending on preferences.
 *
 *-----------------------------------------------------------------------------
 */

void
App::TriageError(CdkError error,
                 const Util::string &message,
                 const Util::string &details,
                 va_list args)
{
   if (error != CDK_ERR_SUCCESS && Prefs::GetPrefs()->GetKioskMode() &&
         Prefs::GetPrefs()->GetOnce()) {
      Util::string errorMsg = Util::Format("Error %d: %s - %s\n",
         error, message.c_str(), Util::FormatV(details.c_str(), args).c_str());
      Util::UserWarning(errorMsg.c_str());
      Log(errorMsg.c_str());
      delete mWindow;
      exit(error);
   }

   ShowErrorDialog(message, details, args);
}


/*
 *----------------------------------------------------------------------------
 *
 * cdk::App::GetWindowManagerName --
 *
 *       Determines the active window manager name.
 *
 * Returns:
 *       A newly allocated string containing the window manager name.
 *
 * Side effects:
 *       None.
 *
 *----------------------------------------------------------------------------
 */

Util::string
App::GetWindowManagerName()
{
#ifndef GDK_WINDOWING_X11
   return "";
#else
   GdkScreen *screen;
   GdkWindow *window;
   GdkNativeWindow native;
   GdkAtom atom_wmcheck;
   GdkAtom atom_wmname;
   int data_format;
   int data_length;
   uint8 *data;
   GdkDisplay *display;

   /*
    * Retreive atoms for the properties we need to check.
    */
   atom_wmcheck = gdk_atom_intern("_NET_SUPPORTING_WM_CHECK", FALSE);
   if (atom_wmcheck == GDK_NONE) {
      return "";
   }
   atom_wmname = gdk_atom_intern("_NET_WM_NAME", FALSE);
   if (atom_wmname == GDK_NONE) {
      return "";
   }

   /*
    * Use the root window and display.
    */
   screen = gdk_screen_get_default();
   window = gdk_screen_get_root_window(screen);
   display = gdk_screen_get_display(screen);

   /*
    * Query the X root window for the XWindow that supports checking
    * the window manager name.
    */
   if (!gdk_property_get(window, atom_wmcheck, GDK_NONE, 0, sizeof(data),
                         FALSE, NULL, &data_format, &data_length, &data)) {
      return "";
   }

   /*
    * Ensure that we retrieved the data in 32-bit mode.
    */
   if (data_format != 32 || !data) {
      g_free(data);
      return "";
   }

   /*
    * Convert the XWindow id to a GdkNativeWindow.
    */
   native = *((GdkNativeWindow *)data);
   g_free(data);

   /*
    * Make sure we retrieved a valid XWindow.
    */
   if (!native) {
      return "";
   }

   /*
    * Retrieve a GdkWindow for the display and GdkNativeWindow.
    */
   window = gdk_window_foreign_new_for_display(display, native);
   if (!window) {
      return "";
   }

   /*
    * Retrieve the _NET_WM_NAME property from the XWindow that supports
    * the property.
    */
   if (!gdk_property_get(window, atom_wmname, GDK_NONE, 0, 1024,
                         FALSE, NULL, &data_format, &data_length, &data)) {
      g_object_unref(window);
      return "";
   }

   /*
    * We are done with the foreign window.
    */
   g_object_unref(window);

   /*
    * Ensure that we retrieved the data in 8-bit mode.
    */
   if (data_format != 8 || !data) {
      g_free(data);
      return "";
   }

   Util::string ret((char *)data, data_length);
   g_free(data);
   return ret;
#endif
}


} // namespace cdk
