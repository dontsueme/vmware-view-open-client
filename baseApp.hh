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
 * baseApp.hh --
 *
 *      A base class for a cdk application.
 */

#ifndef BASE_APP_HH
#define BASE_APP_HH


#include <glib.h>


#include "cdkErrors.h"
#include "util.hh"


namespace cdk {


class BaseApp
{
public:
   static BaseApp *GetSharedApp() { return sApp; }

   virtual int Main(int argc, char **argv) = 0;

   static void ShowError(CdkError error,
                         const Util::string &message,
                         const Util::string &details, ...);
   static void ShowInfo(const Util::string &message,
                        const Util::string &details, ...);
   static void ShowWarning(const Util::string &message,
                           const Util::string &details, ...);
protected:
   BaseApp();
   virtual ~BaseApp() { }

   bool Init(int argc, char **argv);
   void Fini();

   virtual void InitLogging();
   static void IntegrateGLibLogging();

   virtual void InitPoll() = 0;
   virtual Util::string GetLocaleDir() = 0;

   // XXX: Cocoa needs to have InitPrefs created
   virtual void InitPrefs() { }

   virtual void ShowErrorDialog(const Util::string &message,
                                const Util::string &details,
                                va_list args) = 0;
   virtual void ShowInfoDialog(const Util::string &message,
                               const Util::string &details,
                               va_list args) = 0;
   virtual void ShowWarningDialog(const Util::string &message,
                                  const Util::string &details,
                                  va_list args) = 0;
   virtual void TriageError(CdkError error,
                            const Util::string &message,
                            const Util::string &details,
                            va_list args);
private:
   BaseApp(const BaseApp &app) { }
   BaseApp &operator=(const BaseApp &app) { return *this; }

   static void OnGLibLog(const gchar *domain,
                         GLogLevelFlags level,
                         const gchar *message,
                         gpointer user_data);

   static void WarningHelper(const gchar *string);

   static BaseApp *sApp;
};


} // namespace cdk


#endif // BASE_APP_HH
