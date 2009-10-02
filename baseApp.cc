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

#include <glib/gi18n.h>


#include "baseApp.hh"


extern "C" {
#include "log.h"
#include "poll.h"
#include "productState.h"
#include "sig.h"
#include "ssl.h"
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
   g_set_printerr_handler((GPrintFunc)Warning);
   g_log_set_default_handler((GLogFunc)BaseApp::OnGLibLog, NULL);
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

   Log_Init(NULL, VMWARE_VIEW ".log.filename", VMWARE_VIEW);
   IntegrateGLibLogging();

   Util::string localeDir = GetLocaleDir();
   Log("Using locale directory %s\n", localeDir.c_str());

   bindtextdomain(VMWARE_VIEW, localeDir.c_str());
   bind_textdomain_codeset(VMWARE_VIEW, "UTF-8");
   textdomain(VMWARE_VIEW);

   printf(_("Using log file %s\n"), Log_GetFileName());

   InitPoll();
   SSL_InitEx(NULL, NULL, NULL, true, false, false);
   BasicHttp_Init(Poll_Callback, Poll_CallbackRemove);

   Sig_Init();

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
   Sig_Exit();
}


} // namespace cdk
