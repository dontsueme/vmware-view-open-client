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
#include "prefs.hh"
#include "window.hh"


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
   if (!Init(argc, argv)) {
      return 1;
   }

   Preference_Init();

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
   mWindow = CreateWindow();

   // Quit when window closes.
   g_signal_connect(G_OBJECT(mWindow->GetWindow()), "destroy",
                    G_CALLBACK(gtk_main_quit), NULL);

   /*
    * So, building the UI needs to be after the constructor in case
    * any subclasses override things such as GetFullscreen().
    */
   mWindow->RequestBroker();

   mWindow->Show();

   gtk_main();

   Fini();

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::App::CreateWindow --
 *
 *      Creates a new Window.
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
App::CreateWindow()
{
   return new Window();
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
   if (mWindow) {
      mWindow->Close();
   } else {
      exit(0);
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
   App *app = reinterpret_cast<App *>(GetSharedApp());
   ASSERT(app);
   ASSERT(app->mWindow);

   /*
    * It would be nice if there was a va_list variant of
    * gtk_message_dialog_new().
    */
   va_list args;
   va_start(args, format);
   Util::string label = Util::FormatV(format.c_str(), args);
   va_end(args);

   app->mWindow->ShowDialog(type, "%s", label.c_str());
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
   Util::string localeDir = Util::GetUsefulPath(LOCALEDIR, "../share/locale");
   if (localeDir.empty()) {
      Util::UserWarning(_("Could not find locale directory; falling back "
                          "to %s\n"), LOCALEDIR);
      localeDir = LOCALEDIR;
   }
   return localeDir;
}


} // namespace cdk
