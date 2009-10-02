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


#include "util.hh"


namespace cdk {


class BaseApp
{
public:
   static BaseApp *GetSharedApp() { return sApp; }

   virtual int Main(int argc, char **argv) = 0;

protected:
   BaseApp();
   virtual ~BaseApp() { }

   bool Init(int argc, char **argv);
   void Fini();

   virtual void InitPoll() = 0;
   virtual Util::string GetLocaleDir() = 0;

private:
   BaseApp(const BaseApp &app) { }
   BaseApp &operator=(const BaseApp &app) { return *this; }

   static void IntegrateGLibLogging();
   static void OnGLibLog(const gchar *domain,
                         GLogLevelFlags level,
                         const gchar *message);

   static BaseApp *sApp;
};


} // namespace cdk


#endif // BASE_APP_HH
