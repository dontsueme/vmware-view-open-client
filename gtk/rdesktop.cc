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
 * rdesktop.cc --
 *
 *    Simple command line wrapper for rdesktop.
 */

#include <boost/bind.hpp>


#include "app.hh"
#include "prefs.hh"
#include "rdesktop.hh"


#define MMR_ERROR_STR "MMR ERROR:"


namespace cdk {


const Util::string RDesktop::RDesktopBinary = "rdesktop";


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::RDesktop --
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

RDesktop::RDesktop()
{
   onErr.connect(boost::bind(OnError, _1));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::Start --
 *
 *      Forks & spawns the rdesktop process (respects $PATH for finding
 *      rdesktop) using ProcHelper::Start.
 *
 *      The "-p -" argument tells rdesktop to read password from stdin.  The
 *      parent process writes the password to its side of the socket along
 *      with a newline.  This avoids passing it on the command line.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Spawns a child process.
 *
 *-----------------------------------------------------------------------------
 */

void
RDesktop::Start(const BrokerXml::DesktopConnection &connection,   // IN
                const Util::string &windowId,                     // IN
                const Util::Rect *geometry,                       // IN
                bool enableMMR,                                   // IN
                const std::vector<Util::string>& devRedirectArgs, // IN/OPT
                GdkScreen *screen)                                // IN/OPT
{
   ASSERT(!connection.address.empty());

   Util::string depthArg;
   int depth = gdk_visual_get_best_depth();
   /*
    * rdesktop 1.6 only supports 8, 15, 16, 24, or 32, so avoid
    * passing in any values it won't understand.
    */
   switch (depth) {
   case 32:
      // but 1.4 doesn't support 32, so cap that.
      depth = 24;
      // fall through...
   case 24:
   case 16:
   case 15:
   case 8:
      depthArg = Util::Format("%d", depth);
      break;
   }

   /*
    * NOTE: Not using -P arg (store bitmap cache on disk).  It slows
    * startup with NFS home directories and can cause considerable
    * disk space usage.
    */
   std::vector<Util::string> args;
   args.push_back("-z");                                   // compress
   args.push_back("-K");                                   // Don't grab the keyboard
   args.push_back("-g");                                   // WxH geometry
   args.push_back(Util::Format("%dx%d", geometry->width,
                               geometry->height).c_str());
   args.push_back("-X"); args.push_back(windowId.c_str()); // XWin to use
   args.push_back("-u");                                   // Username
   args.push_back(connection.username.c_str());
   args.push_back("-d");                                   // Domain
   args.push_back(connection.domainName.c_str());
   args.push_back("-p"); args.push_back("-");              // Read passwd from stdin
   if (!depthArg.empty()) {
      // Connection colour depth
      args.push_back("-a"); args.push_back(depthArg.c_str());
   }

   Util::string options = Prefs::GetPrefs()->GetRDesktopOptions();
   if (!options.empty()) {
      char **argv = NULL;
      GError *error = NULL;
      if (g_shell_parse_argv(options.c_str(), NULL, &argv, &error)) {
         // XXX: If they pass a sound arg here, we don't catch it.
         for (char **arg = argv; *arg; arg++) {
            args.push_back(*arg);
         }
         g_strfreev(argv);
      } else {
         Log("Error retrieving rdesktop options: %s", error->message);
         g_error_free(error);
      }
   }

   Util::string kbdLayout = Prefs::GetPrefs()->GetKbdLayout();
   if (!kbdLayout.empty()) {
      args.push_back("-k");
      args.push_back(kbdLayout);
   }

   bool soundSet = false;
   // Append device redirect at the end, in case of some hinky shell args

#define APPEND_REDIR_ARGS(_redirArgs)                                       \
   G_STMT_START {                                                           \
      std::vector<Util::string> redirArgs = _redirArgs;                     \
      for (std::vector<Util::string>::const_iterator i = redirArgs.begin(); \
           i != redirArgs.end(); i++) {                                     \
         args.push_back("-r"); args.push_back(*i);                          \
         if (!soundSet && i->compare(0, 6, "sound:") == 0) {                \
            soundSet = true;                                                \
         }                                                                  \
      }                                                                     \
   } G_STMT_END

   // Once for passed-in args
   APPEND_REDIR_ARGS(devRedirectArgs);
   // Once for default args
   APPEND_REDIR_ARGS(Prefs::GetPrefs()->GetRDesktopRedirects());

   if (!soundSet) {
      args.push_back("-r"); args.push_back("sound:local");
   }
   if (enableMMR) {
      args.push_back("-r"); args.push_back("rdp_mmr.so:MMRVDX");
   }

   // And I'll form the head.
   args.push_back(Util::Format("%s:%d",
                               connection.address.c_str(),
                               connection.port).c_str());

   ProcHelper::Start(RDesktopBinary, RDesktopBinary, args, 0, screen, connection.password + "\n");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RDesktop::OnError --
 *
 *      Callback for OnErr call in ProcHelper.  Reads the error string and
 *      displays error to the user.
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
RDesktop::OnError(Util::string errorString) // IN
{
   int length = strlen(MMR_ERROR_STR);
   if (errorString.compare(0, length, MMR_ERROR_STR) == 0) {
      size_t pos2 = errorString.find(":", length + 1);

      int errNum = strtol(errorString.substr(length, pos2 - length).c_str(),
                          NULL, 0);
      errorString = errorString.substr(pos2 + 1);

      switch (errNum) {
      case MMR_ERROR_GSTREAMER:
         BaseApp::ShowWarning(_("Gstreamer plugins not found"),
                              _("The required GStreamer plugins could not be "
                                "found. Please check that your path is set "
                                "properly."));
         break;
      default:
         BaseApp::ShowWarning(_("Warning"), "%s", errorString.c_str());
         break;
      }
   }
}


} // namespace cdk
