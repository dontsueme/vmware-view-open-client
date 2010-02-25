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
 * prefs.cc --
 *
 *    Preferences management.
 */


#ifdef VIEW_GTK
#include <gtk/gtk.h>
#else
#include <glib.h>
#endif


#include "prefs.hh"
#include "protocols.hh"


extern "C" {
#include "productState.h"
#include "util.h"
}


#define VMWARE_HOME_DIR "~/.vmware"
#define PREFERENCES_FILE_NAME VMWARE_HOME_DIR"/view-preferences"

#define VMWARE_SYS_DIR "/etc/vmware"
#define SYSTEM_PREFS_FILE_NAME VMWARE_SYS_DIR"/view-default-config"
#define MANDATORY_PREFS_FILE_NAME VMWARE_SYS_DIR"/view-mandatory-config"

#define MAX_BROKER_MRU 10
#define VIEW_DEFAULT_MMR_PATH "/usr/lib/mmr"
#define VMWARE_VIEW "vmware-view"


namespace cdk {


/*
 * Initialise static data.
 */

Prefs *Prefs::sPrefs = NULL;


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::Prefs --
 *
 *      Constructor.  Initialize and load the preferences Dictionary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Prefs::Prefs()
   : mPassword(NULL)
{
   char *prefPath = Util_ExpandString(PREFERENCES_FILE_NAME);
   ASSERT_MEM_ALLOC(prefPath);

   mPrefPath = prefPath;
   ASSERT(!mPrefPath.empty());
   free(prefPath);

   mSysDict = Dictionary_Create();
   ASSERT_MEM_ALLOC(mSysDict);

   mDict = Dictionary_Create();
   ASSERT_MEM_ALLOC(mDict);

   mOptDict = Dictionary_Create();
   ASSERT_MEM_ALLOC(mOptDict);

   mMandatoryDict = Dictionary_Create();
   ASSERT_MEM_ALLOC(mMandatoryDict);

   Msg_Reset(true);
   if (!Util_MakeSureDirExistsAndAccessible(VMWARE_HOME_DIR, 0755)) {
      Util::UserWarning(_("Creating ~/.vmware failed: %s\n"),
                        Msg_GetMessagesAndReset());
   }

   // This may fail if the file doesn't exist yet.
   Dictionary_Load(mSysDict, SYSTEM_PREFS_FILE_NAME, DICT_NOT_DEFAULT);
   Dictionary_Load(mDict, mPrefPath.c_str(), DICT_NOT_DEFAULT);
   Dictionary_Load(mMandatoryDict, MANDATORY_PREFS_FILE_NAME, DICT_NOT_DEFAULT);

   if (GetDefaultUser().empty()) {
      const char *user = g_get_user_name();
      if (user && strcmp(user, "root")) {
         SetDefaultUser(user);
      }
   }
   if (GetMMRPath().empty()) {
      SetMMRPath(VIEW_DEFAULT_MMR_PATH);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::~Prefs --
 *
 *      Destructor.  Destroy the preferences Dictionary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Set sPrefs to NULL if this is the default Prefs instance.
 *
 *-----------------------------------------------------------------------------
 */

Prefs::~Prefs()
{
   Dictionary_Free(mMandatoryDict);
   Dictionary_Free(mOptDict);
   Dictionary_Free(mDict);
   Dictionary_Free(mSysDict);

   if (sPrefs == this) {
      sPrefs = NULL;
   }

   ClearPassword();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetPrefs --
 *
 *      Static accessor for the default Prefs instance.
 *
 * Results:
 *      Prefs instance.
 *
 * Side effects:
 *      sPrefs may be initialized with a new Prefs instance.
 *
 *-----------------------------------------------------------------------------
 */

Prefs *
Prefs::GetPrefs()
{
   if (!sPrefs) {
      sPrefs = new Prefs();
   }
   return sPrefs;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetDictionaryForKey --
 *
 *      Gets the appropriate dictionary for reading the value of a
 *      given key.
 *
 *      Dictionaries are searched in a way which allows system admins
 *      to enforce certain options.
 *
 * Results:
 *      The first dictionary containing a value for key in our "path."
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Dictionary *
Prefs::GetDictionaryForKey(Util::string key) // IN
   const
{
#define CHECK_DICT(dict)                                \
   if (Dictionary_IsDefined(dict, key.c_str())) {       \
      return dict;                                      \
   }

   CHECK_DICT(mMandatoryDict);
   CHECK_DICT(mOptDict);
   CHECK_DICT(mDict);

#undef CHECK_DICT

   return mSysDict;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetString --
 *
 *      Private helper to wrap Dict_GetString.
 *
 * Results:
 *      The preference value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Prefs::GetString(Util::string key,        // IN
                 Util::string defaultVal) // IN
   const
{
   char *val = NULL;
   val = Dict_GetString(GetDictionaryForKey(key), defaultVal.c_str(),
                        key.c_str());
   Util::string retVal = val;
   free(val);
   return retVal;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetBool --
 *
 *      Private helper to wrap Dict_GetBool.
 *
 * Results:
 *      The preference value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

bool
Prefs::GetBool(Util::string key, // IN
               bool defaultVal)  // IN
   const
{
   return Dict_GetBool(GetDictionaryForKey(key), defaultVal, key.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetInt --
 *
 *      Private helper to wrap Dict_GetInt.
 *
 * Results:
 *      The preference value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int32
Prefs::GetInt(Util::string key, // IN
              int32 defaultVal) // IN
   const
{
   return Dict_GetLong(GetDictionaryForKey(key), defaultVal, key.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetString --
 *
 *      Private helper to wrap Dict_SetString & Dictionary_Write.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes the preferences file.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetString(Util::string key, // IN
                 Util::string val) // IN
{
   Dictionary_Unset(mOptDict, key.c_str());
   Dict_SetString(mDict, val.c_str(), key.c_str());
   Dictionary_Write(mDict, mPrefPath.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetBool --
 *
 *      Private helper to wrap Dict_SetBool & Dictionary_Write.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes the preferences file.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetBool(Util::string key, // IN
               bool val)        // IN
{
   Dictionary_Unset(mOptDict, key.c_str());
   Dict_SetBool(mDict, val, key.c_str());
   Dictionary_Write(mDict, mPrefPath.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetInt --
 *
 *      Private helper to wrap Dict_SetLong & Dictionary_Write.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes the preferences file.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetInt(Util::string key, // IN
              int32 val)       // IN
{
   Dictionary_Unset(mOptDict, key.c_str());
   Dict_SetLong(mDict, val, key.c_str());
   Dictionary_Write(mDict, mPrefPath.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetBrokerMRU --
 *
 *      Accessor for the broker MRU list stored in the preferences.
 *      These are the view.broker0-10 keys.
 *
 * Results:
 *      List of brokers.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
Prefs::GetBrokerMRU()
   const
{
   std::vector<Util::string> brokers;

   for (int brokerIdx = 0; brokerIdx <= MAX_BROKER_MRU; brokerIdx++) {
      Util::string key = Util::Format("view.broker%d", brokerIdx);
      Util::string val = GetString(key);
      if (!val.empty()) {
         brokers.push_back(val);
      }
   }

   return brokers;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::AddBrokerMRU --
 *
 *      Add a broker name as the view.broker0 preference key.  Rewrites the
 *      view.broker1-10 keys to remove the broker name to avoid duplicates.
 *
 *      XXX: Should pause preference writing until the end?  Maybe this is
 *      unneccessary if Dictionary_Write pauses automatically.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::AddBrokerMRU(Util::string first) // IN
{
   // Get current broker MRU so we don't overwrite the first one
   std::vector<Util::string> brokers = GetBrokerMRU();

   SetString("view.broker0", first);

   unsigned int brokerIdx = 1;
   for (std::vector<Util::string>::iterator i = brokers.begin();
        i != brokers.end() && brokerIdx <= MAX_BROKER_MRU; i++) {
      if (*i != first) {
         SetString(Util::Format("view.broker%d", brokerIdx++), *i);
      }
   }
   while (brokerIdx < brokers.size()) {
      SetString(Util::Format("view.broker%d", brokerIdx++), "");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::ClearPassword --
 *
 *      Clear the password from memory.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password is free and clear.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::ClearPassword()
{
   if (mPassword) {
      ZERO_STRING(mPassword);
      g_free(mPassword);
      mPassword = NULL;
   }
}


} // namespace cdk


/*
 * These need to be outside of the namespace so the keys don't need to
 * be declared in the header.
 */

#define MAKE_KEY(NAME, allowKey, valKey)                              \
   static const char *const KEY_ALLOW_##NAME = "view."#allowKey;      \
   static const char *const KEY_##NAME = "view."#valKey;

#define PREF_FUNC(valType, getType, KEY_NAME, FuncName, allowKey, valKey, defVal) \
   MAKE_KEY(KEY_NAME, allowKey, valKey)                                 \
                                                                        \
   valType                                                              \
   cdk::Prefs::Get##FuncName()                                          \
      const                                                             \
   {                                                                    \
      if (GetBool(KEY_ALLOW_##KEY_NAME, true)) {                        \
         return Get##getType(KEY_##KEY_NAME, defVal);                   \
      }                                                                 \
      return defVal;                                                    \
   }                                                                    \
                                                                        \
   void                                                                 \
   cdk::Prefs::Set##FuncName(valType val)                               \
   {                                                                    \
      if (GetBool(KEY_ALLOW_##KEY_NAME, true)) {                        \
         Set##getType(KEY_##KEY_NAME, val);                             \
      } else {                                                          \
         Log("Not saving "#FuncName" (%s=false)\n", KEY_ALLOW_##KEY_NAME); \
      }                                                                 \
   }

// We define the prefs first to create the keys that are used in ParseArgs

#define PREF_STRING(KEY_NAME, FuncName, allowKey, valKey, defVal)       \
   PREF_FUNC(cdk::Util::string, String, KEY_NAME, FuncName, allowKey, valKey, defVal)

#define PREF_BOOL(KEY_NAME, FuncName, allowKey, valKey, defVal) \
   PREF_FUNC(bool, Bool, KEY_NAME, FuncName, allowKey, valKey, defVal)

#define PREF_INT(KEY_NAME, FuncName, allowKey, valKey, defVal)  \
   PREF_FUNC(int, Int, KEY_NAME, FuncName, allowKey, valKey, defVal)

PREF_STRING(BACKGROUND,       Background,      allowBackground,      background, "")
PREF_STRING(CUSTOM_LOGO,      CustomLogo,      allowCustomLogo,      customLogo, "")
PREF_STRING(DEFAULT_BROKER,   DefaultBroker,   allowDefaultBroker,   defaultBroker, "")
PREF_STRING(DEFAULT_DESKTOP,  DefaultDesktop,  allowDefaultDesktop,  defaultDesktop, "")
PREF_STRING(DEFAULT_DOMAIN,   DefaultDomain,   allowDefaultDomain,   defaultDomain, "")
PREF_STRING(DEFAULT_PROTOCOL, DefaultProtocol, allowDefaultProtocol, defaultProtocol, "")
PREF_STRING(DEFAULT_USER,     DefaultUser,     allowDefaultUser,     defaultUser,   "")
PREF_STRING(MMR_PATH,         MMRPath,         allowMMRPath,         mmrPath, "")
PREF_STRING(RDESKTOP_OPTIONS, RDesktopOptions, allowRDesktopOptions, rdesktopOptions, "")

PREF_BOOL(AUTO_CONNECT,    AutoConnect,    allowAutoConnect,    autoConnect,    false)
PREF_BOOL(FULL_SCREEN,     FullScreen,     allowFullScreen,     fullScreen,     false)
PREF_BOOL(NON_INTERACTIVE, NonInteractive, allowNonInteractive, nonInteractive, false)
PREF_BOOL(DEFAULT_SHOW_BROKER_OPTIONS, DefaultShowBrokerOptions,
          allowDefaultShowBrokerOptions, defaultShowBrokerOptions, false)
PREF_BOOL(DISABLE_METACITY_KEYBINDING_WORKAROUND,
          DisableMetacityKeybindingWorkaround,
          allowDisableMetacityKeybindingWorkaround,
          disableMetacityKeybindingWorkaround,
          false)
PREF_BOOL(ALLOW_WM_BINDINGS, AllowWMBindings, allowAllowWMBindings, allowWMBindings, false)

MAKE_KEY(DEFAULT_DESKTOP_SIZE, allowDefaultDesktopSize, defaultDesktopSize)
MAKE_KEY(DEFAULT_CUSTOM_DESKTOP_SIZE, allowDefaultCustomDesktopSize,
         defaultCustomDesktopSize)
MAKE_KEY(DEFAULT_DESKTOP_WIDTH, allowDefaultDesktopWidth, defaultDesktopWidth)
MAKE_KEY(DEFAULT_DESKTOP_HEIGHT, allowDefaultDesktopHeight, defaultDesktopHeight)


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetDefaultDesktopSize --
 *
 *      Returns the default desktop size.
 *
 * Results:
 *      The desktop size.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Prefs::DesktopSize
Prefs::GetDefaultDesktopSize()
   const
{
   if (GetBool(KEY_ALLOW_DEFAULT_DESKTOP_SIZE, true)) {
      int ret = GetInt(KEY_DEFAULT_DESKTOP_SIZE, FULL_SCREEN);
      return (DesktopSize)CLAMP(ret, ALL_SCREENS, CUSTOM_SIZE);
   }
   return FULL_SCREEN;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetDefaultDesktopSize --
 *
 *      Sets the default desktop size.
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
Prefs::SetDefaultDesktopSize(Prefs::DesktopSize size)
{
   ASSERT(CLAMP(size, ALL_SCREENS, CUSTOM_SIZE) == size);
   if (GetBool(KEY_ALLOW_DEFAULT_DESKTOP_SIZE, true)) {
      SetInt(KEY_DEFAULT_DESKTOP_SIZE, size);
   } else {
      Log("Not saving the default desktop size "
          "(%s=false).\n", KEY_ALLOW_DEFAULT_DESKTOP_SIZE);
   }
}



/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetDefaultCustomDesktopSize --
 *
 *      Gets the height and width of the custom size.
 *
 * Results:
 *      A rectangle denoting the bounds of the selected custom size.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::GetDefaultCustomDesktopSize(GdkRectangle *rect) // OUT
{
   ASSERT(rect);
   if (GetBool(KEY_ALLOW_DEFAULT_CUSTOM_DESKTOP_SIZE, true)) {
      rect->width = MAX(640, GetInt(KEY_DEFAULT_DESKTOP_WIDTH, 1024));
      rect->height = MAX(480, GetInt(KEY_DEFAULT_DESKTOP_HEIGHT, 768));
   } else {
      rect->width = 1024;
      rect->height = 768;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetDefaultCustomDesktopSize --
 *
 *      Stores the height and width of the custom desktop size.
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
Prefs::SetDefaultCustomDesktopSize(GdkRectangle *rect) // IN
{
   ASSERT(rect);
   if (GetBool(KEY_ALLOW_DEFAULT_CUSTOM_DESKTOP_SIZE, true)) {
      SetInt(KEY_DEFAULT_DESKTOP_WIDTH, rect->width);
      SetInt(KEY_DEFAULT_DESKTOP_HEIGHT, rect->height);
   } else {
      Log("Not saving the default custom desktop size "
          "(%s=false).\n", KEY_ALLOW_DEFAULT_CUSTOM_DESKTOP_SIZE);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::ParseArgs --
 *
 *      Parse command line options, setting appropriate prefs for the
 *      flags given.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May exit if --version or invalid options are passed.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::ParseArgs(int *argcp,         // IN/OUT
                 char ***argvp,      // IN/OUT
                 bool allowFileOpts) // IN
{
   char *optBroker = NULL;
   char *optUser = NULL;
   char *optPassword = NULL;
   char *optDomain = NULL;
   char *optDesktop = NULL;
   gboolean optNonInteractive = false;
   gboolean optFullScreen = false;
   char *optBackground = NULL;
   char *optFile = NULL;
   char **optRedirect = NULL;
   gboolean optVersion = false;
   char *optCustomLogo = NULL;
   char *optMMRPath = NULL;
   char *optRDesktop = NULL;
   char **optUsb = NULL;
   gboolean optAllowWMBindings = false;
   char *optProtocol = NULL;

   GOptionEntry optEntries[] = {
      { "keep-wm-bindings", 'K', 0, G_OPTION_ARG_NONE, &optAllowWMBindings,
        N_("Keep window manager key bindings.") },
      { "serverURL", 's', 0, G_OPTION_ARG_STRING, &optBroker,
        N_("Specify connection broker."), N_("<broker URL>") },
      { "userName", 'u', 0, G_OPTION_ARG_STRING, &optUser,
        N_("Specify user name for password authentication."), N_("<user name>") },
      { "password", 'p', 0, G_OPTION_ARG_STRING, &optPassword,
        N_("Specify password for password authentication."), N_("<password>") },
      { "domainName", 'd', 0, G_OPTION_ARG_STRING, &optDomain,
        N_("Specify domain for password authentication."), N_("<domain name>") },
      { "desktopName", 'n', 0, G_OPTION_ARG_STRING, &optDesktop,
        N_("Specify desktop by name."), N_("<desktop name>") },
      { "nonInteractive", 'q', 0, G_OPTION_ARG_NONE, &optNonInteractive,
        N_("Connect automatically if enough values are given on the command "
           "line."), NULL },
      { "fullscreen", '\0', 0, G_OPTION_ARG_NONE, &optFullScreen,
        N_("Enable full screen mode."), NULL },
      { "background", 'b', 0 , G_OPTION_ARG_FILENAME, &optBackground,
        N_("Image file to use as background in full screen mode."), N_("<image>") },
      { "redirect", 'r', 0 , G_OPTION_ARG_STRING_ARRAY, &optRedirect,
        N_("Forward device redirection to rdesktop"), N_("<device info>") },
      { "logo", '\0', 0, G_OPTION_ARG_FILENAME, &optCustomLogo,
        N_("Display a custom logo."), N_("<image>") },
      { "mmrPath", 'm', 0, G_OPTION_ARG_STRING, &optMMRPath,
        N_("Directory location containing Wyse MMR libraries."),
        N_("<mmr directory>") },
      { "rdesktopOptions", '\0', 0, G_OPTION_ARG_STRING, &optRDesktop,
        N_("Command line options to forward to rdesktop."),
        N_("<rdesktop options>") },
      { "usb", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &optUsb,
        N_("Options for USB forwarding."), N_("<usb options>") },
      { "protocol", '\0', 0, G_OPTION_ARG_STRING, &optProtocol,
        N_("Preferred connection protocol [RDP|PCOIP|RGS|localvm]"),
        N_("<protocol name>") },
      { NULL }
   };

   GOptionEntry optFileEntries[] = {
      { "file", 'f', 0 , G_OPTION_ARG_FILENAME, &optFile,
        N_("File containing additional command line arguments."),
        N_("<file path>") },
      { "version", '\0', 0, G_OPTION_ARG_NONE, &optVersion,
        N_("Display version information and exit."), NULL },
      { NULL }
   };

   GOptionContext *context =
      g_option_context_new(_("- connect to VMware View desktops"));

   GError *fileError = NULL;
   if (allowFileOpts) {
      g_option_context_add_main_entries(context, optFileEntries, NULL);

      /*
       * Only the --file argument will be known to the context when it first
       * parses argv, so we should ignore other arguments (and leave them be)
       * until after the file argument has been fully dealt with.
       */
      g_option_context_set_ignore_unknown_options(context, true);

      g_option_context_set_help_enabled(context, false);

      // First, we only want to parse out the --file option.
      if (!g_option_context_parse(context, argcp, argvp, &fileError)) {
         Util::UserWarning(_("Error parsing command line: %s\n"),
                           fileError->message);
      }

      if (optVersion) {
         printf(_(
"%s %s\n\n"
"VMware is a registered trademark or trademark (the \"Marks\") of VMware, Inc.\n"
"in the United States and/or other jurisdictions and is not licensed to you\n"
"under the terms of the LGPL version 2.1. If you distribute VMware View Open\n"
"Client unmodified in either binary or source form or the accompanying\n"
"documentation unmodified, you may not remove, change, alter or otherwise\n"
"modify the Marks in any manner. If you make minor modifications to VMware\n"
"View Open Client or the accompanying documentation, you may, but are not\n"
"required to, continue to distribute the unaltered Marks with your binary or\n"
"source distributions. If you make major functional changes to VMware View\n"
"Open Client or the accompanying documentation, you may not distribute the\n"
"Marks with your binary or source distribution and you must remove all\n"
"references to the Marks contained in your distribution. All other use or\n"
"distribution of the Marks requires the prior written consent of VMware.\n"
"All other marks and names mentioned herein may be trademarks of their\n"
"respective companies.\n\n"
"Copyright © 1998-2010 VMware, Inc. All rights reserved.\n"
"This product is protected by U.S. and international copyright and\n"
"intellectual property laws.\n"
"VMware software products are protected by one or more patents listed at\n%s\n\n"),
                ProductState_GetName(), ProductState_GetVersion(),
                // TRANSLATORS: Ignore this; we will localize with appropriate URL.
                _("http://www.vmware.com/go/patents"));
         exit(0);
      }

      /*
       * Hold on to the error--we might get the same message the next time we
       * parse, and we only want to show it once.
       */
   }

   g_option_context_add_main_entries(context, optEntries, NULL);

   // If --file was specified and it exists, it will be opened and parsed.
   if (optFile) {
      GOptionContext *context =
         g_option_context_new(_("- connect to VMware View desktops"));
      g_option_context_add_main_entries(context, optEntries, NULL);

      gchar *contents = NULL;
      gsize length = 0;

      GError *error = NULL;
      gint argc = 0;
      gchar **argv = NULL;

      if (g_file_get_contents(optFile, &contents, &length, &error) &&
          g_shell_parse_argv(Util::Format(VMWARE_VIEW " %s", contents).c_str(),
                             &argc, &argv, &error)) {
         ParseArgs(&argc, &argv, false);
      }

      g_strfreev(argv);
      g_free(contents);
      g_free(optFile);
   }

#if defined(VIEW_GTK) && GTK_CHECK_VERSION(2, 6, 0)
   g_option_context_add_group(context, gtk_get_option_group(true));
#endif

   /*
    * Now, parse the rest of the options out of argv.  By doing this parsing
    * here, it will allows the commandline options to override the config
    * file options.
    */
   g_option_context_set_ignore_unknown_options(context, false);
   g_option_context_set_help_enabled(context, true);
   GError *error = NULL;
   // Show the error message only if it's not the same as the one shown above.
   if (!g_option_context_parse(context, argcp, argvp, &error) &&
       (!fileError || Str_Strcmp(fileError->message, error->message) != 0)) {
      Util::UserWarning(_("Error parsing command line: %s\n"), error->message);
   }
   g_clear_error(&fileError);
   g_clear_error(&error);

   if (optBroker && GetBool(KEY_ALLOW_DEFAULT_BROKER, true)) {
      Dict_SetString(mOptDict, optBroker, KEY_DEFAULT_BROKER);
   }
   g_free(optBroker);

   if (optUser && GetBool(KEY_ALLOW_DEFAULT_USER, true)) {
      Dict_SetString(mOptDict, optUser, KEY_DEFAULT_USER);
   }
   g_free(optUser);

   if (optPassword && Str_Strcmp(optPassword, "-") == 0) {
      char *tmp = getpass(_("Password: "));
      optPassword = g_strdup(tmp);
      ZERO_STRING(tmp);
   }
   if (optPassword) {
      ClearPassword();
      mPassword = optPassword;
   }

   if (optDomain && GetBool(KEY_ALLOW_DEFAULT_DOMAIN, true)) {
      Dict_SetString(mOptDict, optDomain, KEY_DEFAULT_DOMAIN);
   }
   g_free(optDomain);

   if (optDesktop && GetBool(KEY_ALLOW_DEFAULT_DESKTOP, true)) {
      Dict_SetString(mOptDict, optDesktop, KEY_DEFAULT_DESKTOP);
   }
   g_free(optDesktop);

   if (optNonInteractive) {
      Log("Using non-interactive mode.\n");
   }
   if (optNonInteractive && GetBool(KEY_ALLOW_NON_INTERACTIVE, true)) {
      Dict_SetBool(mOptDict, optNonInteractive, KEY_NON_INTERACTIVE);
   }

   if (optFullScreen && GetBool(KEY_ALLOW_FULL_SCREEN, true)) {
      Dict_SetBool(mOptDict, optFullScreen, KEY_FULL_SCREEN);
   }

   if (optBackground && GetBool(KEY_ALLOW_BACKGROUND, true)) {
      Dict_SetString(mOptDict, optBackground, KEY_BACKGROUND);
   }
   g_free(optBackground);

   if (optRedirect && GetBool("view.allowRDesktopRedirects", true)) {
      mRDesktopRedirects.clear();
      for (char **opt = optRedirect; opt && *opt; opt++) {
         mRDesktopRedirects.push_back(*opt);
      }
   }
   g_strfreev(optRedirect);

   if (optUsb && GetBool("view.allowUsbOptions", true)) {
      mUsbOptions.clear();
      for (char **opt = optUsb; opt && *opt; opt++) {
         mUsbOptions.push_back(*opt);
      }
   }
   g_strfreev(optUsb);

   if (optCustomLogo && GetBool(KEY_ALLOW_CUSTOM_LOGO, true)) {
      Dict_SetString(mOptDict, optCustomLogo, KEY_CUSTOM_LOGO);
   }
   g_free(optCustomLogo);

   if (optMMRPath && GetBool(KEY_ALLOW_MMR_PATH, true)) {
      Dict_SetString(mOptDict, optMMRPath, KEY_MMR_PATH);
   }
   g_free(optMMRPath);

   if (optRDesktop && GetBool(KEY_ALLOW_RDESKTOP_OPTIONS, true)) {
      Dict_SetString(mOptDict, optRDesktop, KEY_RDESKTOP_OPTIONS);
   }
   g_free(optRDesktop);

   if (optProtocol && GetBool(KEY_ALLOW_DEFAULT_PROTOCOL, true)) {
      Protocols::ProtocolType proto = Protocols::GetProtocolFromName(optProtocol);
      if (proto != Protocols::UNKNOWN) {
         Dict_SetString(mOptDict, Protocols::GetName(proto).c_str(),
                        KEY_DEFAULT_PROTOCOL);
      } else {
         Util::UserWarning(_("Unknown protocol: %s\n"), optProtocol);
      }
   }
   g_free(optProtocol);

   if (optAllowWMBindings && GetBool(KEY_ALLOW_WM_BINDINGS, true)) {
      Dict_SetBool(mOptDict, optAllowWMBindings, KEY_ALLOW_WM_BINDINGS);
   }
}


} // namespace cdk
