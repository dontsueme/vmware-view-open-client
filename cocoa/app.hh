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
 * app.hh --
 *
 *      Cocoa implementation of a broker app.
 */

#ifndef APP_HH
#define APP_HH


#import "baseApp.hh"


extern "C" {
#include "poll.h"
}



namespace cdk {


class App
   : virtual public BaseApp
{
public:
   // Overridden from BaseApp.
   int Main(int argc, char *argv[]);

protected:
   // overridden from BaseApp
   void InitPoll() { Poll_InitCF(); }
   Util::string GetLocaleDir();
   virtual void InitLogging();
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
};


} // namespace cdk


#endif // APP_COCOA_H
