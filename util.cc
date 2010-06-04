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
 * util.cc --
 *
 *    CDK utilities.
 */


#ifndef __MINGW32__
#include <arpa/inet.h>
#else
#include <windows.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>


#ifdef VIEW_GTK
#include <gtk/gtkalignment.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkhbbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <libxml/uri.h>
#ifndef __MINGW32__
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#if defined(__linux__)
#include <linux/if_ether.h>
#include <net/if.h>
#endif
#else // __MINGW32__
#include <winsock2.h>
#include <libintl.h>
#include <unistd.h>
#include <ws2tcpip.h>
#endif

#if defined(__APPLE__)
#include <sys/types.h>
/*
 * From getifaddrs(3):
 *
 * If both <net/if.h> and <ifaddrs.h> are being included, <net/if.h>
 * must be included before <ifaddrs.h>.
 */
#include <net/if.h>
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif

#ifdef HAVE_UIDNA_IDNTOASCII
extern "C" {
#include <unicode/uidna.h>
}
#endif // HAVE_UIDNA_IDNTOASCII


#include "util.hh"
#ifdef VIEW_GTK
#include "app.hh"
#endif
#ifdef __APPLE__
#include "cdkProxy.h"
#endif
#include "cdkUrl.h"


extern "C" {
#include "file.h"
#include "posix.h"
#include "strutil.h"
}


#ifdef GDK_WINDOWING_X11
// Here to avoid conflict with vm_basic_types.h
#include <gdk/gdkx.h>
#endif


#define MAX_HOSTNAME_LENGTH 255

#define IMG_KEY "imgKey"
#define LABEL_KEY "labelKey"


namespace cdk {
namespace Util {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::DoNothing --
 *
 *      Implementation for the slot returned by EmptyDoneSlot.
 *      Does nothing.
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
DoNothing()
{
   // Do nothing.
}


static DoneSlot sEmptyDoneSlot(DoNothing);


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::EmptyDoneSlot --
 *
 *      Simple DoneSlot implementation that does nothing.
 *
 * Results:
 *      A DoneSlot.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

DoneSlot
EmptyDoneSlot()
{
   return sEmptyDoneSlot;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::DoLogAbort --
 *
 *      Implementation for the slot returned by MakeLogAbortSlot.  Logs the
 *      exception text if the handler was not called due to cancellation.
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
DoLogAbort(bool cancelled, // IN
           exception err)  // IN
{
   if (!cancelled) {
      Log("Unhandled abort: %s", err.what());
      DEBUG_ONLY(Util_Backtrace(0));
   }
}


static AbortSlot sLogAbortSlot(DoLogAbort);


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::LogAbortSlot --
 *
 *      Simple AbortSlot implementation that logs the abort exception's what
 *      string.
 *
 * Results:
 *      An AbortSlot.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

AbortSlot
LogAbortSlot()
{
   return sLogAbortSlot;
}


#ifdef VIEW_GTK


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetComboBoxEntryText --
 *
 *      Hack around the missing gtk_combo_box_get_active_text in Gtk2.4, by
 *      getting the combo's child entry widget and returning its text.
 *
 * Results:
 *      Active text of the combobox.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
GetComboBoxEntryText(GtkComboBoxEntry *combo) // IN
{
   GtkWidget *w = gtk_bin_get_child(GTK_BIN(combo));
   ASSERT(GTK_IS_ENTRY(w));
   return gtk_entry_get_text(GTK_ENTRY(w));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetComboBoxText --
 *
 *      Hack around the missing gtk_combo_box_get_active_text in Gtk2.4, by
 *      getting the combo's currently selected iterator and returning its text.
 *
 * Results:
 *      Active text of the combobox.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
GetComboBoxText(GtkComboBox *combo) // IN
{
   GtkTreeIter iter;
   Util::string result = "";
   if (gtk_combo_box_get_active_iter(combo, &iter)) {
      gchar *text = NULL;
      gtk_tree_model_get(gtk_combo_box_get_model(combo), &iter, 0, &text, -1);
      result = text;
      g_free(text);
   }
   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::CreateButton --
 *
 *      Hack around the missing gtk_button_set_image in Gtk2.4. Creates a
 *      GtkButton with the given stock ID and optionally override the default
 *      label.  The button contents have VM_SPACING padding on either side and
 *      between them.
 *
 *      Copied and converted to Gtk-C from bora/apps/lib/lui/button.cc.
 *
 * Results:
 *      A new Gtk button.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkButton*
CreateButton(const string stockId, // IN
             string label)         // IN: optional
{
   GtkWidget *button = gtk_button_new();
   ASSERT_MEM_ALLOC(button);
   gtk_widget_show(button);

   GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
   ASSERT_MEM_ALLOC(align);
   gtk_widget_show(align);
   gtk_container_add(GTK_CONTAINER(button), align);
   gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0,
                             VM_SPACING, VM_SPACING);

   GtkWidget *contents = gtk_hbox_new(false, VM_SPACING);
   ASSERT_MEM_ALLOC(contents);
   gtk_widget_show(contents);
   gtk_container_add(GTK_CONTAINER(align), contents);

   GtkWidget *img = gtk_image_new_from_stock(stockId.c_str(),
                                             GTK_ICON_SIZE_BUTTON);
   ASSERT_MEM_ALLOC(img);
   gtk_widget_show(img);
   gtk_box_pack_start(GTK_BOX(contents), img, false, false, 0);

   g_object_set_data(G_OBJECT(button), IMG_KEY, img);

   if (label.empty()) {
      GtkStockItem item = { 0, };
      gboolean found = gtk_stock_lookup(stockId.c_str(), &item);
      ASSERT(found);
      if (found) {
         label = item.label;
      }
   }

   GtkWidget *l = gtk_label_new_with_mnemonic(label.c_str());
   ASSERT_MEM_ALLOC(l);
   gtk_widget_show(l);
   gtk_box_pack_start(GTK_BOX(contents), l, false, false, 0);

   g_object_set_data(G_OBJECT(button), LABEL_KEY, l);

   AtkObject *atk = gtk_widget_get_accessible(GTK_WIDGET(button));
   atk_object_set_name(atk, gtk_label_get_text(GTK_LABEL(l)));

   return GTK_BUTTON(button);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::SetButtonIcon --
 *
 *      Changes the image and label displayed in the button.  This button must
 *      have been created by the Util::CreateButton method.
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
SetButtonIcon(GtkButton *button,     // IN
              const string &stockId, // IN
              string label)          // IN/OPT
{
   GtkWidget *img = GTK_WIDGET(g_object_get_data(G_OBJECT(button), IMG_KEY));
   ASSERT(GTK_IS_IMAGE(img));
   GtkLabel *l = GTK_LABEL(g_object_get_data(G_OBJECT(button), LABEL_KEY));
   ASSERT(GTK_IS_LABEL(l));

   gtk_image_set_from_stock(GTK_IMAGE(img), stockId.c_str(),
                            GTK_ICON_SIZE_BUTTON);

   if (label.empty()) {
      GtkStockItem item = { 0 };
      gtk_stock_lookup(stockId.c_str(), &item);
      label = item.label;
   }

   gtk_label_set_text_with_mnemonic(l, label.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::CreateActionArea --
 *
 *      Creates an action area containing the passed-in buttons.
 *
 * Results:
 *      A GtkHButtonBox.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkWidget *
CreateActionArea(GtkButton *button1, // IN
                 ...)
{
   GtkWidget *actionArea = gtk_hbutton_box_new();
   gtk_box_set_spacing(GTK_BOX(actionArea), VM_SPACING);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(actionArea),
                             GTK_BUTTONBOX_END);
   va_list args;
   va_start(args, button1);
   for (GtkWidget *button = GTK_WIDGET(button1);
        button; button = va_arg(args, GtkWidget *)) {
      gtk_box_pack_start(GTK_BOX(actionArea), button, false, true, 0);
   }
   va_end(args);
   return actionArea;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::OverrideWindowUserTime --
 *
 *      Older versions of Metacity can avoid raising windows due to
 *      focus-stealing avoidance.  This causes an X server roundtrip to get a
 *      valid timestamp, allowing us to set the window's _NET_WM_USER_TIME to
 *      trick Metacity.
 *
 *      Code originally from tomboy/libtomboy/tomboyutil.c, licensed X11.
 *      Updated to conditionally call gdk_property_change instead of
 *      gdk_x11_window_set_user_time, which doesn't exist in Gtk 2.4.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Synchronous X server roundtrip.
 *
 *-----------------------------------------------------------------------------
 */

#ifdef GDK_WINDOWING_X11
void
OverrideWindowUserTime(GtkWindow *window) // IN
{
   if (!GTK_WIDGET_REALIZED(window)) {
      gtk_widget_realize(GTK_WIDGET(window));
   }

   GdkWindow *gdkWin = GTK_WIDGET(window)->window;
   guint32 evTime = gtk_get_current_event_time();

   if (evTime == 0) {
      // Last resort for non-interactive openings.  Causes roundtrip to server.
      gint evMask = gtk_widget_get_events(GTK_WIDGET(window));
      if (!(evMask & GDK_PROPERTY_CHANGE_MASK)) {
         gtk_widget_add_events(GTK_WIDGET(window),
                               GDK_PROPERTY_CHANGE_MASK);
      }
      evTime = gdk_x11_get_server_time(gdkWin);
   }

   DEBUG_ONLY(Log("Setting _NET_WM_USER_TIME to: %d\n", evTime));

#if GTK_CHECK_VERSION(2, 6, 0)
   gdk_x11_window_set_user_time(gdkWin, evTime);
#else
   gdk_property_change(gdkWin, gdk_atom_intern("_NET_WM_USER_TIME", false),
                       gdk_atom_intern("CARDINAL", true), 32,
                       GDK_PROP_MODE_REPLACE, (guchar*)&evTime, 1);
#endif
}
#endif


#endif // VIEW_GTK


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::UserWarning --
 *
 *      Prints a warning to the console and logs it. Currently this is the
 *      only way to display text on the console in released builds.
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
UserWarning(const char *format, // IN
            ...)
{
   va_list arguments;
   va_start(arguments, format);
   string line = FormatV(format, arguments);
   va_end(arguments);
   fprintf(stderr, line.c_str());
   Log(line.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetHostLabel --
 *
 *      Construct a host label based on the hostname, port, and
 *      protocol.
 *
 * Results:
 *      A string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string
GetHostLabel(const string &hostname, // IN
	     unsigned short port,    // IN
	     bool secure)            // IN
{
   if (hostname.empty()) {
      return "";
   }
   /*
    * https is implied, so only show the protocol if not secure.
    * Also, skip the port if it's the default for that protocol.
    */
   unsigned short defaultPort = secure ? 443 : 80;
   return Format(port == defaultPort ? "%s%s" : "%s%s:%hu",
		 secure ? "" : "http://", hostname.c_str(), port);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::ParseHostLabel --
 *
 *      Parse a hostname label.  Determines the protocol (SSL is
 *      implied) and port.
 *
 * Results:
 *      The parsed hostname.
 *
 * Side effects:
 *      port and secure are updated with the values from label, if the
 *      label was parseable (undefined if not).
 *
 *-----------------------------------------------------------------------------
 */

string
ParseHostLabel(const string &label,  // IN
	       unsigned short *port, // OUT/OPT
	       bool *secure)         // OUT/OPT
{
   string ret;
   gboolean aSecure = true;
   char *host = NULL;
   if (CdkUrl_Parse(label.c_str(), NULL, &host, port, NULL, &aSecure)) {
      if (secure) {
         *secure = aSecure;
      }
      ret = host;
      g_free(host);
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetUsefulPath --
 *
 *      Given a fully-qualified system path and a path relative to the
 *      location of the binary, return the system path if the binary is in
 *      BINDIR or the binary's location plus relativePath otherwise.
 *
 * Results:
 *      The systemPath you passed in, or the location of the binary
 *      plus relativePath, or the empty string if the location to be returned
 *      does not exist.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string
GetUsefulPath(const string systemPath,   // IN
              const string relativePath) // IN
{
   char *self;

#if defined(__APPLE__)
   uint32_t pathSize = FILE_MAXPATH;
   self = (char *)malloc(pathSize);
   ASSERT(self);
   int rv = _NSGetExecutablePath(self, &pathSize);
   // condition is needed for OBJDIR=release builds to avoid warnings
   if (rv != 0) {
      ASSERT(rv == 0);
   }
#elif defined(__MINGW32__)
   self = (char *)malloc(FILE_MAXPATH);
   ASSERT(self);
   GetModuleFileName(NULL, self,  FILE_MAXPATH);
#else
   self = Posix_ReadLink("/proc/self/exe");
   ASSERT(self);
#endif

   char *dirname = NULL;
   File_GetPathName(self, &dirname, NULL);
   free(self);
   ASSERT(dirname);

   Util::string selfPath(dirname);
   free(dirname);

#ifdef BINDIR
   if (selfPath == BINDIR) {
      selfPath = systemPath;
   } else
#endif
   {
      selfPath += G_DIR_SEPARATOR_S;
      selfPath += relativePath;
   }
   if (!File_Exists(selfPath.c_str())) {
      Warning("Relative or system path %s does not exist.\n", selfPath.c_str());
      return "";
   }
   return selfPath;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetClientHostName --
 *
 *      Attempts to determine the hostname for this machine.
 *
 * Results:
 *      The hostname of this machine, or an empty string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string
GetClientHostName()
{
   char name[MAX_HOSTNAME_LENGTH + 1];
   if (gethostname(name, MAX_HOSTNAME_LENGTH) == 0) {
      name[MAX_HOSTNAME_LENGTH] = '\0';
      return name;
   } else {
      Warning("gethostname() failed: %s\n", strerror(errno));
      return "";
   }
}

/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetClientInfo --
 *      Collects information about the client and stores it in a std::map.
 *
 * Results:
 *      A mapping of the client information.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#if defined(__linux__) || defined(__APPLE__)
ClientInfoMap
GetClientInfo(const string broker, // IN
              int port)            // IN
{
   ClientInfoMap info = GetNICInfo(broker, port);

   string hostname = GetClientHostName();
   if (!hostname.empty()) {
      info["Machine_Name"] = hostname;
   }
   char name[MAX_HOSTNAME_LENGTH + 1];
   if (getdomainname(name, MAX_HOSTNAME_LENGTH) == 0) {
      name[MAX_HOSTNAME_LENGTH] = '\0';
      info["Machine_Domain"] = name;
   } else {
      Warning("getdomainname() failed: %s\n", strerror(errno));
   }

   info["LoggedOn_Username"] = g_get_user_name();

   string lang = setlocale(LC_MESSAGES, NULL);
   if (lang == "C" || lang == "POSIX" || lang == "") {
      info["Language"] = "en";
   } else {
      info["Language"] = lang;
   }

#ifdef __APPLE__
   info["Type"] = "Mac";

   /*
    * In Obj-C++ you can simply do [[NZimeZone localTimeZone] name].
    */
   CFTimeZoneRef zone = CFTimeZoneCopyDefault();
   CFStringRef zoneName = CFTimeZoneGetName(zone);
   char *tzid = CdkProxy_CFStringToUTF8CString(zoneName);
   if (tzid) {
      info["TZID"] = tzid;
      free(tzid);
   }
   CFRelease(zone);
#else // !__APPLE__
   info["Type"] = "Linux";

   const char *tzid = getenv("TZ");
   char *contents = NULL;
   if (!tzid) {
      if (g_file_get_contents("/etc/timezone", &contents, NULL, NULL)) {
         // Debian
         tzid = g_strstrip(contents);
      } else if (getuid() != 0) {
         // Don't run as root, for security reasons.

         /*
          * SuSE, RHEL and unsupported distro cases use a child process (bash)
          * to parse the clock file for us, so we don't have to do all of the
          * messy * handling of edge cases here, e.g., multiple time zone key
          * words, extraneous white space, commented out time zone key words.
          */

         // Bash script to process clock file.
         static char tz_sh[] = {
            "if [ -f \"/etc/sysconfig/clock\" ]; then\n"
            "   . /etc/sysconfig/clock\n"
            "   if [ \"${TIMEZONE}\" ]; then\n"
            "       echo \"$TIMEZONE\"\n"
            "       exit 0\n"
            "   elif [ \"${ZONE}\" ]; then\n"
            "       echo \"$ZONE\"\n"
            "       exit 0\n"
            "   fi\n"
            "fi\n"
            "exit 0\n"
         };

         char *bash[] = { (char *)"/bin/bash", NULL };
         GPid pid = -1;
         int std_in = -1;
         int std_out = -1;

         static const int maxTzIdLen = 64;
         contents = (char *)g_new0(char, maxTzIdLen);

         // Launch bash process
         Bool spawned = g_spawn_async_with_pipes(
                                NULL, bash, NULL,
                                (GSpawnFlags)0,
                                NULL, NULL, &pid,
                                &std_in, &std_out, NULL,
                                NULL);
         ASSERT(spawned);

         if (spawned) {
            // Write time zone script to bash process stdin.
            if ((size_t)write(std_in, tz_sh, sizeof(tz_sh)) == sizeof(tz_sh)) {
                // Read time zone result from bash script.
                if (read(std_out, contents, maxTzIdLen - 1) != -1) {
                    if (contents[0] != '\0') {
                        /*
                         * bash script returns timezone gleaned from clock
                         * file: strip any trailing space or newline.
                         */
                        tzid = g_strstrip(contents);
                    }
                    // else clock file did not contain an acceptable timezone.
                }
            }

            // Cleanup
            close(std_in);
            close(std_out);
            g_spawn_close_pid(pid);
         }
      }
   }
   if (tzid) {
      info["TZID"] = tzid;
   } else {
      time_t now = time(NULL);
      struct tm tm_now;
      localtime_r(&now, &tm_now);
      info["TZID"] = tm_now.tm_zone;
   }
   g_free(contents);
#endif // !__APPLE__

   return info;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetClientInfo --
 *      Collects information about the client and stores it in a std::map.
 *
 * Results:
 *      A mapping of the client information.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef _WIN32
ClientInfoMap
GetClientInfo(const string broker, // IN
              int port)            // IN

{
   ClientInfoMap info = GetNICInfo(broker, port);

   string hostname = GetClientHostName();
   if (!hostname.empty()) {
      info["Machine_Name"] = hostname;
   }

#if 0
   // XXX - this is incomplete
   char name[MAX_HOSTNAME_LENGTH + 1];
   if (getdomainname(name, MAX_HOSTNAME_LENGTH) == 0) {
      name[MAX_HOSTNAME_LENGTH] = '\0';
      info["Machine_Domain"] = name;
   } else {
      Warning("getdomainname() failed: %s\n", strerror(errno));
   }
#endif

   info["LoggedOn_Username"] = g_get_user_name();

#if 0
   // XXX - incomplete - no setlocale in Windows.
   string lang = setlocale(LC_MESSAGES, NULL);
#endif
   string lang = "C";
   if (lang == "C" || lang == "POSIX" || lang == "") {
      info["Language"] = "en";
   } else {
      info["Language"] = lang;
   }

   TIME_ZONE_INFORMATION tzInfo;
   char tzid[(sizeof(tzInfo.StandardName) / sizeof(wchar_t)) + 1];
   wchar_t *tzname = NULL;
   int tztype = GetTimeZoneInformation(&tzInfo);

   if (tztype == TIME_ZONE_ID_STANDARD) {
      tzname = tzInfo.StandardName;
   } else if (tztype == TIME_ZONE_ID_DAYLIGHT) {
      tzname = tzInfo.DaylightName;
   }
   if (tzname && wcstombs(tzid, tzname, sizeof(tzid)) > 0) {
      // wcstobms does not guarantee null termination of tzid so just to be sure...
      tzid[sizeof(tzid)-1] = '\0';
      info["Windows_Timezone"] = g_strdup(tzid);
   } else {
      Warning("Unable to determine time zone.");
   }

   info["Type"] = "Windows";

   return info;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetMacAddr --
 *
 *      Linux implementation of GetMacAddr.
 *
 * Results:
 *      The mac address of the given IPv4 address.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef __linux__
string
GetMacAddr(int sock,                 // IN
           struct sockaddr_in *addr) // IN
{
   struct ifreq ifr;
   int i = 0;
   while (++i) {
      ifr.ifr_ifindex = i;
      /*
       * First, fetch the device's name. If the device does not exist,
       * ioctl() will return -1 and the loop will end.
       */
      if (ioctl(sock, SIOCGIFNAME, &ifr) < 0 ) {
         break;
      }
      /*
       * Then, fetch the device's IP address.  If it does not have an IP,
       * go on to the next device.
       */
      if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
         continue;
      }

      /*
       * Compare this IP address to the one obtained from getsockname().
       * If they match, then we use ioctl again to obtain the MAC
       * address of that device.
       */
      if (ifr.ifr_addr.sa_family != addr->sin_family) {
         continue;
      }
      struct sockaddr_in *ifraddr = (struct sockaddr_in *)&ifr.ifr_addr;
      if (!memcmp(&addr->sin_addr, &ifraddr->sin_addr,
                  sizeof(addr->sin_addr))) {
         // Get the MAC Address of the network card.
         if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
             Warning("ioctl() failed to get the local MAC address while "
                     "collecting client info: %s\n", strerror(errno));
             break;
         }
         return Format("%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
                       (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[0],
                       (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[1],
                       (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[2],
                       (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[3],
                       (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[4],
                       (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[5]);
      }
   }
   return "";
}
#endif // __linux__


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetMacAddr --
 *
 *      Mac/BSD Implementation of GetMacAddr.
 *
 * Results:
 *      The Mac address for the given IPv4 address.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef __APPLE__
string
GetMacAddr(int sock,                 // IN
           struct sockaddr_in *addr) // IN
{
   string ret;
   struct ifaddrs *addrs = NULL;
   if (getifaddrs(&addrs)) {
      Warning("GetMacAddr: getifaddrs failed: %s\n", strerror(errno));
      return ret;
   }

   for (struct ifaddrs *i = addrs; i; i = i->ifa_next) {
      // Only handle IPv4 addresses.
      if (!i->ifa_addr || i->ifa_addr->sa_family != addr->sin_family) {
         continue;
      }

      // This interface has the same address as our socket.
      struct sockaddr_in *i_addr = (struct sockaddr_in *)i->ifa_addr;
      if (memcmp(&i_addr->sin_addr, &addr->sin_addr, sizeof(addr->sin_addr))) {
         continue;
      }

      /*
       * Well, this is the interface we want; if any of these fail, we
       * should just bail out of this loop anyway.
       */
      int mib[6] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST };
      size_t len;
      mib[5] = if_nametoindex(i->ifa_name);
      if (mib[5] == 0) {
         Warning("GetMacAddr: if_nametoindex failed: %s\n", strerror(errno));
         break;
      }
      if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
         Warning("GetMacAddr: sysctl 1 failed: %s\n", strerror(errno));
         break;
      }
      char *buf = (char *)g_malloc(len);
      if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
         Warning("GetMacAddr: sysctl 2 failed: %s\n", strerror(errno));
         break;
      }

      struct if_msghdr *ifm = (struct if_msghdr *)buf;
      struct sockaddr_dl *sdl = (struct sockaddr_dl *)(ifm + 1);
      unsigned char *ptr = (unsigned char *)LLADDR(sdl);

      ret = Format("%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
                   (unsigned int)(unsigned char)ptr[0],
                   (unsigned int)(unsigned char)ptr[1],
                   (unsigned int)(unsigned char)ptr[2],
                   (unsigned int)(unsigned char)ptr[3],
                   (unsigned int)(unsigned char)ptr[4],
                   (unsigned int)(unsigned char)ptr[5]);

      g_free(buf);
      break;
   }
   freeifaddrs(addrs);
   return ret;
}
#endif // __APPLE__


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetNICInfo --
 *      Gets the IP and MAC address of the active network card and adds them
 *      to info.
 *
 * Results:
 *      If the IP and MAC addresses can be determined, they will be added to
 *      info.  If they can not, info will be left alone.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#if defined(__linux__) || defined(__APPLE__)
ClientInfoMap
GetNICInfo(const string broker, // IN
           int port)            // IN
{
   ClientInfoMap info;

   struct hostent *serverInfo = gethostbyname(broker.c_str());
   if (serverInfo == NULL) {
      Warning("Could not resolve the broker address %s while compiling client "
              "info: %s\n", broker.c_str(), hstrerror(h_errno));
      return info;
   }

   if (serverInfo->h_addrtype != AF_INET) {
      Warning("Skipping NIC info for non-IPv4 address of type %d\n",
              serverInfo->h_addrtype);
      return info;
   }

   struct sockaddr_in server;
   if ((size_t)serverInfo->h_length > sizeof(server.sin_addr.s_addr)) {
      Warning("Server address is larger than expected socket address\n");
      return info;
   }

   int sock = socket(serverInfo->h_addrtype, SOCK_DGRAM, 0);
   if (sock < 0) {
      Warning("socket() failed while compiling client info: %s\n",
              strerror(errno));
      return info;
   }

   memcpy(&server.sin_addr.s_addr, serverInfo->h_addr, serverInfo->h_length);
   server.sin_family = serverInfo->h_addrtype;
   server.sin_port = htons(port);

   if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
      Warning("connect() failed while compiling client info: %s\n",
              strerror(errno));
      close(sock);
      return info;
   }

   struct sockaddr_in local;
   {
      struct sockaddr local_sa;
      socklen_t addrSize = sizeof(local_sa);
      if (getsockname(sock, &local_sa, &addrSize) < 0) {
         Warning("getsockname() failed to get the local IP address while "
                 "compiling client info: %s\n", strerror(errno));
         close(sock);
         return info;
      }
      memcpy(&local, &local_sa, sizeof(local));
   }

   {
      char buffer[256];
      if (inet_ntop(local.sin_family, &local.sin_addr, buffer,
                    sizeof(buffer)) == NULL) {
         Warning("inet_ntop() failed to format the local IP address while "
                 "compiling client info: %s\n", strerror(errno));
         close(sock);
         return info;
      }
      info["IP_Address"] = buffer;
   }

   info["MAC_Address"] = GetMacAddr(sock, &local);

   close(sock);
   return info;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::GetNICInfo --
 *      Gets the IP and MAC address of the active network card and adds them
 *      to info.
 *
 * Results:
 *      If the IP and MAC addresses can be determined, they will be added to
 *      info.  If they can not, info will be left alone.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef _WIN32
ClientInfoMap
GetNICInfo(const string broker, // IN
           int port)            // IN
{
   // XXX - need windows implementation.
   ClientInfoMap info;
   return info;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util:Utf8Casecmp: --
 *
 *      Compare two UTF-8 strings, ignoring case.
 *
 * Results:
 *      An int less than, greater than, or equal to zero if s1 is less
 *      than, greater than, or equal to s2.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
Utf8Casecmp(const char *s1, // IN
            const char *s2) // IN
{
   ASSERT(s1);
   ASSERT(s2);

   char *folded1 = g_utf8_casefold(s1, -1);
   char *folded2 = g_utf8_casefold(s2, -1);

   int ret = g_utf8_collate(folded1, folded2);

   g_free(folded1);
   g_free(folded2);

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util:MkdirWithParents: --
 *
 *      Create a directory and, if needed, its parent directories
 *
 * Results:
 *      0 if the directory was created successfully.
 *      -1 if an error occurred, with errno set by g_mkdir.
 *
 * Side effects:
 *
 *-----------------------------------------------------------------------------
 */

int
MkdirWithParents(const char *path, // IN
                 int mode)         // IN
{
#if GLIB_CHECK_VERSION(2, 8, 0)
   return g_mkdir_with_parents(path, mode);
#else
   char *dirPath = g_str_has_suffix(path, G_DIR_SEPARATOR_S)
                      ? g_strdup(path)
                      : g_strdup_printf("%s%c", path, G_DIR_SEPARATOR);
   bool skipFirst = g_path_is_absolute(path);
   for (char *c = dirPath; *c != '\0'; c++) {
      if (*c != G_DIR_SEPARATOR) {
         continue;
      } else if (skipFirst) {
         skipFirst = false;
         continue;
      }
      *c = '\0';
      if (g_mkdir(dirPath, mode) && errno != EEXIST) {
         int errnoSave = errno;
         g_free(dirPath);
         errno = errnoSave;
         return -1;
      }
      *c = G_DIR_SEPARATOR;
   }
   g_free(dirPath);
   return 0;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Util::EnsureFilePermissions --
 *
 *      Create the file path with the given mode, or ensure the
 *      existing file has the given mode.
 *
 * Results:
 *      true if file was OK.
 *
 * Side effects:
 *      Creates path.
 *
 *-----------------------------------------------------------------------------
 */

bool
EnsureFilePermissions(const char *path, // IN
                      int mode)         // IN
{
   bool fileOk = false;
   if (0 == chmod(path, mode)) {
      fileOk = true;
   } else if (errno == ENOENT) {
      int fd = open(path, O_CREAT, mode);
      if (fd != -1) {
         fileOk = true;
         close(fd);
      } else  {
            Warning(_("File '%s' could not be created: %s\n"),
                    path, strerror(errno));
      }
   } else {
      Warning(_("Could not change mode of file '%s': %s\n"),
              path, strerror(errno));
   }
   return fileOk;
}


} // namespace Util
} // namespace cdk
