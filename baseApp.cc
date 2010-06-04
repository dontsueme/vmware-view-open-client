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
 * baseApp.cc --
 *
 *      Implementation of BaseApp - program initialization and support.
 */

#include "baseApp.hh"


extern "C" {
#include "vm_basic_types.h"
#include "log.h"
#include "poll.h"
#include "productState.h"
#include "sig.h"
#include "vm_atomic.h"
#include "vm_version.h"
}


#include "basicHttp.h"


/*
 * Define and use these alternate PRODUCT_VIEW_* names until vm_version.h
 * uses the View naming scheme.
 */
#define PRODUCT_VIEW_SHORT_NAME "View"
#define PRODUCT_VIEW_NAME MAKE_NAME("View Manager")
#define PRODUCT_VIEW_CLIENT_NAME_FOR_LICENSE PRODUCT_VIEW_CLIENT_NAME

#define VMWARE_VIEW "vmware-view"


namespace cdk {


BaseApp *BaseApp::sApp = NULL;


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::BaseApp --
 *
 *      Constructor.  Sets sApp if it hasn't been set yet.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

BaseApp::BaseApp()
{
   if (!sApp) {
      sApp = this;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::IntegrateGLibLogging --
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
BaseApp::IntegrateGLibLogging(void)
{
   g_set_printerr_handler(BaseApp::WarningHelper);
   g_log_set_default_handler(BaseApp::OnGLibLog, NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::OnGLibLog --
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
BaseApp::OnGLibLog(const gchar *domain,  // IN
                   GLogLevelFlags level, // IN
                   const gchar *message, // IN
                   gpointer user_data)   // IN: ignored
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
 * cdk::BaseApp::WarningHelper --
 *
 *      A helper function to call Warning() from a glib callback.
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
BaseApp::WarningHelper(const gchar *string) // IN
{
   Warning("%s", string);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::Init --
 *
 *      Main initialization function.  Initialize all of the VM
 *      libraries we use, and libraries common to all ports.
 *
 * Results:
 *      true on success, false on failure.
 *
 * Side effects:
 *      Libraries are initialized.
 *
 *-----------------------------------------------------------------------------
 */

bool
BaseApp::Init(int argc,     // IN
              char *argv[]) // IN
{
   Atomic_Init();
#ifdef USE_GLIB_THREADS
   if (!g_thread_supported()) {
      g_thread_init(NULL);
   }
#endif
#ifndef __MINGW32__
   VThread_Init(VTHREAD_UI_ID, VMWARE_VIEW);
#endif

   /*
    * XXX: Should use PRODUCT_VERSION_STRING for the third arg, but
    * that doesn't know about the vdi version.
    */
   ProductState_Set(PRODUCT_VDM_CLIENT, PRODUCT_VIEW_CLIENT_NAME,
                    VIEW_CLIENT_VERSION_NUMBER " " BUILD_NUMBER,
                    BUILD_NUMBER_NUMERIC, 0,
                    PRODUCT_VIEW_CLIENT_NAME_FOR_LICENSE,
                    PRODUCT_VERSION_STRING_FOR_LICENSE);

   // XXX: figure out why arm doesn't have CODESET defined.
#ifdef CODESET
   setlocale(LC_ALL, "");

   /*
    * If the charset isn't supported by unicode, Log_Init() will
    * Panic(); this attempts to avoid that.
    */
   const char *codeset = nl_langinfo(CODESET);
   bool validEncoding =
      Unicode_IsEncodingValid(Unicode_EncodingNameToEnum(codeset));
   if (!validEncoding) {
      unsetenv("LANG");
   }

   /*
    * We want the first line of our log file to be in C format so that
    * our log collection script can parse it.
    */
   setlocale(LC_ALL, "C");
#endif
   InitLogging();
   setlocale(LC_ALL, "");
#ifdef CODESET
   if (!validEncoding) {
      Log("Encoding \"%s\" is not supported; ignoring $LANG.\n", codeset);
   }
#endif

   Util::string localeDir = GetLocaleDir();
   Log("Using locale directory %s\n", localeDir.c_str());

   bindtextdomain(VMWARE_VIEW, localeDir.c_str());
   bind_textdomain_codeset(VMWARE_VIEW, "UTF-8");
   textdomain(VMWARE_VIEW);

   printf(_("Using log file %s\n"), Log_GetFileName());

   InitPoll();
   BasicHttp_Init(Poll_Callback, Poll_CallbackRemove);

   InitPrefs();

#ifdef VIEW_POSIX
   Sig_Init();
#endif

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

   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::Fini --
 *
 *      De-initialize some libraries.  Likely this doesn't get called.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Libraries are denitialized.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseApp::Fini()
{
   Log_Exit();
#ifdef VIEW_POSIX
   Sig_Exit();
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::InitLogging --
 *
 *      Perform logging initialization.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Initializes logging.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseApp::InitLogging()
{
   if (!Log_Init(NULL, VMWARE_VIEW ".log.filename", VMWARE_VIEW)) {
      Warning("Could not initialize logging.\n");
   }
   IntegrateGLibLogging();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::TriageError --
 *
 *      Default implementation of TriageError; simply show the error
 *      dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Error dialog may be shown.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseApp::TriageError(CdkError error,              // IN/UNUSED
                     const Util::string &message, // IN
                     const Util::string &details, // IN
                     va_list args)                // IN
{
   ShowErrorDialog(message, details, args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::ShowError --
 *
 *      Show an error dialog
 *
 * Results:
 *      None
 *
 * Side effects:
 *      TriageError may exit the process depending on the effective app's error
 *      handling policy.
 *
 *-----------------------------------------------------------------------------
 */

void
BaseApp::ShowError(CdkError error,              // IN
                   const Util::string &message, // IN
                   const Util::string &details, // IN
                   ...)
{
   BaseApp *app = GetSharedApp();
   ASSERT(app);

   va_list args;
   va_start(args, details);

   app->TriageError(error, message, details, args);

   va_end(args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::ShowInfo --
 *
 *      Show an information dialog
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
BaseApp::ShowInfo(const Util::string &message, // IN
                  const Util::string &details, // IN
                  ...)
{
   BaseApp *app = GetSharedApp();
   ASSERT(app);

   va_list args;
   va_start(args, details);

   app->ShowInfoDialog(message, details, args);
   va_end(args);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::BaseApp::ShowWarning --
 *
 *      Show a warning dialog
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
BaseApp::ShowWarning(const Util::string &message, // IN
                     const Util::string &details, // IN
                     ...)
{
   BaseApp *app = GetSharedApp();
   ASSERT(app);

   va_list args;
   va_start(args, details);

   app->ShowWarningDialog(message, details, args);
   va_end(args);
}


} // namespace cdk
