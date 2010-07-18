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
 * app.hh --
 *
 *    Application singleton object. It handles initialization of global
 *    libraries and resources.
 *
 */

#ifndef APP_HH
#define APP_HH


#define VMWARE_HOME_DIR "~/.vmware"


#include <gtk/gtk.h>


#include "baseApp.hh"
#include "prefs.hh"
#include "util.hh"


extern "C" {
#include "poll.h"
#include "sig.h"
}


namespace cdk {


class Window;


class App
   : virtual public BaseApp
{
public:
   App();
   virtual ~App();

   // overrides BaseApp
   virtual int Main(int argc, char *argv[]);

protected:
   // Overrides BaseApp
   void InitPoll() { Poll_InitGtk(); }
   Util::string GetLocaleDir();

   virtual void InitPrefs()
      { Prefs::SetPrefFilePath(VMWARE_HOME_DIR); }

   virtual Window *CreateAppWindow();

   virtual void TriageError(CdkError error,
                            const Util::string &message,
                            const Util::string &details,
                            va_list args);
#ifdef VIEW_POSIX
   static void SigTermHandlerHelper(int s, siginfo_t *info, void *clientData);

   virtual void SigTermHandler(int s, siginfo_t *info);
#endif

private:
   virtual void ShowErrorDialog(const Util::string &message,
                                const Util::string &details,
                                va_list args);
   virtual void ShowInfoDialog(const Util::string &message,
                               const Util::string &details,
                               va_list args);
   virtual void ShowWarningDialog(const Util::string &message,
                                  const Util::string &details,
                                  va_list args);
   virtual Util::string GetWindowManagerName();

   Window *mWindow;
};


} // namespace cdk


#endif // APP_HH
