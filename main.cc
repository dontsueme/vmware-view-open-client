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
 * main.cc --
 *
 *      This module implements the main entry point for the Linux View client.
 */

#include <glib/gi18n.h>


#include "app.hh"
#include "util.hh"

extern "C" {
#include "hostinfo.h"
#include "panic.h"
#include "vm_atomic.h"
#include "vmware.h"
}


#define VMWARE_VIEW "vmware-view"


using namespace cdk;


/*
 *-----------------------------------------------------------------------------
 *
 * main --
 *
 *      Start the application and off we go.
 *
 * Results:
 *      0 (successful completion)
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
main(int argc,    // IN: the arg count
     char **argv) // IN: the arg array
{
   Atomic_Init();

   Util::string localeDir = Util::GetUsefulPath(LOCALEDIR, "../share/locale");
   if (localeDir.empty()) {
      Util::UserWarning(_("Could not find locale directory; falling back "
                          "to %s\n"), LOCALEDIR);
      localeDir = LOCALEDIR;
   } else {
      Log("Using locale directory %s\n", localeDir.c_str());
   }
   bindtextdomain(VMWARE_VIEW, localeDir.c_str());
   bind_textdomain_codeset(VMWARE_VIEW, "UTF-8");
   textdomain(VMWARE_VIEW);

   /*
    * This needs to go after bindtextdomain so it handles GOption localization
    * properly.
    */
   gtk_init(&argc, &argv);

   cdk::App theApp(argc, argv);
   gtk_main();

   return 0;
}
