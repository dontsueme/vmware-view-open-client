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
 * prefs.hh --
 *
 *    Preferences management.
 */

#ifndef PREFS_HH
#define PREFS_HH


#include <vector>


#include "util.hh"

extern "C" {
#include "dictionary.h"
}


namespace cdk {


class Prefs
{
public:
   typedef enum {
      ALL_SCREENS,
      FULL_SCREEN,
      LARGE_WINDOW,
      SMALL_WINDOW,
      CUSTOM_SIZE
   } DesktopSize;

   Prefs();
   ~Prefs();

   static Prefs *GetPrefs();
   static void SetPrefFilePath(const char *filePath);

   void ParseArgs(int *argcp, char ***argvp) { ParseArgs(argcp, argvp, true); }

   std::vector<Util::string> GetBrokerMRU() const;
   void AddBrokerMRU(Util::string first);

   Util::string GetDefaultBroker() const;
   void SetDefaultBroker(Util::string val);

   Util::string GetDefaultUser() const;
   void SetDefaultUser(Util::string val);

   Util::string GetDefaultDomain() const;
   void SetDefaultDomain(Util::string val);

   DesktopSize GetDefaultDesktopSize() const;
   void SetDefaultDesktopSize(DesktopSize size);

   void GetDefaultCustomDesktopSize(GdkRectangle *rect);
   void SetDefaultCustomDesktopSize(GdkRectangle *rect);

   bool GetDefaultShowBrokerOptions() const;
   void SetDefaultShowBrokerOptions(bool val);

   Util::string GetBackground() const;
   void SetBackground(Util::string background);

   Util::string GetCustomLogo() const;
   void SetCustomLogo(Util::string logo);

   Util::string GetDefaultDesktop() const;
   void SetDefaultDesktop(Util::string desktop);

   Util::string GetMMRPath() const;
   void SetMMRPath(Util::string path);

   Util::string GetRDesktopOptions() const;
   void SetRDesktopOptions(Util::string options);

   Util::string GetDefaultProtocol() const;
   void SetDefaultProtocol(Util::string protocol);

   bool GetDisableMetacityKeybindingWorkaround() const;

   bool GetAutoConnect() const;
   void SetAutoConnect(bool connect);

   bool GetNonInteractive() const;
   void SetNonInteractive(bool interactive);

   bool GetFullScreen() const;
   void SetFullScreen(bool fullScreen);

   std::vector<Util::string> GetRDesktopRedirects() const
      { return mRDesktopRedirects; }

   std::vector<Util::string> GetUsbOptions() const
      { return mUsbOptions; }

   const char *GetPassword() const { return mPassword; }
   void ClearPassword();

   Util::string GetSupportFile() const;

   bool GetAllowWMBindings() const;
   void SetAllowWMBindings(bool allow);

   bool GetKioskMode() const;
   void SetKioskMode(bool kioskMode);

   bool GetOnce() const;
   void SetOnce(bool once);

   int GetInitialRetryPeriod() const;
   void SetInitialRetryPeriod(int initialRetryPeriod);

   int GetMaximumRetryPeriod() const;
   void SetMaximumRetryPeriod(int maximumRetryPeriod);

   Util::string GetKbdLayout() const;
   void SetKbdLayout(Util::string val);

private:
   static Prefs *sPrefs;
   static Util::string sFilePath;

   void ParseArgs(int *argcp, char ***argvp, bool allowFileOpts);

   Util::string GetString(Util::string key, Util::string defaultVal = "") const;
   bool GetBool(Util::string key, bool defaultVal = false) const;
   int32 GetInt(Util::string key, int32 defaultVal = 0) const;

   void SetString(Util::string key, Util::string val);
   void SetBool(Util::string key, bool val);
   void SetInt(Util::string key, int32 val);

   void SetSupportFile(Util::string file);
   void SetDisableMetacityKeybindingWorkaround(bool disable);

   void PrintEnvironmentInfo();

   Dictionary *GetDictionaryForKey(Util::string key) const;

   Dictionary *mDict;
   Dictionary *mOptDict;
   Dictionary *mSysDict;
   Dictionary *mMandatoryDict;

   Util::string mPrefPath;

   std::vector<Util::string> mRDesktopRedirects;
   std::vector<Util::string> mUsbOptions;
   char *mPassword;
};


} // namespace cdk


#endif // PREFS_HH
