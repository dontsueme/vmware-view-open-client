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


#include <arpa/inet.h>
#include <glib.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkhbbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <libxml/uri.h>
#ifdef __linux__
#include <linux/if_ether.h>
#endif
// Mac OS X requires sys/socket.h before net/if.h.
#include <sys/socket.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern "C" {
#include <unicode/uidna.h>
}


#include "util.hh"
#include "app.hh"

extern "C" {
#include "file.h"
#include "posix.h"
#include "strutil.h"
}


// Here to avoid conflict with vm_basic_types.h
#include <gdk/gdkx.h>


#define MAX_HOSTNAME_LENGTH 255


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

   AtkObject *atk = gtk_widget_get_accessible(GTK_WIDGET(button));
   atk_object_set_name(atk, gtk_label_get_text(GTK_LABEL(l)));

   return GTK_BUTTON(button);
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
 * cdk::Util::IDNToASCII --
 *
 *      Convert a string from UTF-8/IDN to Punycode/ASCII.
 *
 *      For explanations of IDN, see:
 *      https://developer.mozilla.org/En/Internationalized_Domain_Names_(IDN)_Support_in_Mozilla_Browsers
 *      http://www.ietf.org/rfc/rfc3490.txt
 *      http://www.ietf.org/rfc/rfc3491.txt
 *      http://www.ietf.org/rfc/rfc3492.txt
 *
 * Results:
 *      An ASCII representation of the UTF-8 hostname, or an empty
 *      string if the conversion failed.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

string
IDNToASCII(const string &text) // IN
{
   /*
    * To convert UTF-8 to ASCII, first we need to convert it to UTF-16
    * for ICU, then convert it to 16-bit "ASCII", and then back to
    * 8-bit ASCII (which is ASCII) for xmlParse().
    */
   long utf16Len;
   GError *error = NULL;
   gunichar2 *utf16Text = g_utf8_to_utf16(text.c_str(), -1, NULL, &utf16Len,
                                          &error);
   if (error) {
      Log("Could not convert text \"%s\" to UTF-16: %s\n", text.c_str(),
          error->message);
      g_error_free(error);
      return "";
   }

   int32_t idnLen = 2 * utf16Len;
   UChar *idnText;
   UErrorCode status;
   int32_t len;
   /*
    * If we don't allocate enough characters on the first attempt,
    * IDNToASCII() will return how many characters we need.  If the
    * second attempt fails, just give up rather than looping forever.
    */
tryConversion:
   idnText = g_new(UChar, idnLen + 1);
   status = U_ZERO_ERROR;
   len = uidna_IDNToASCII((const UChar *)utf16Text, utf16Len,
                          idnText, idnLen, UIDNA_DEFAULT, NULL,
                          &status);
   if (len > idnLen) {
      g_free(idnText);
      // Guard against multiple loops.
      if (idnLen != 2 * utf16Len) {
         Log("The ASCII length was greater than we allocated after two "
             "attempts; giving up on \"%s\".", text.c_str());
         g_free(idnText);
         g_free(utf16Text);
         return "";
      }
      idnLen = len;
      goto tryConversion;
   } else if (U_FAILURE(status)) {
      Log("Could not convert text \"%s\" to IDN: %s\n", text.c_str(),
          u_errorName(status));
      g_free(idnText);
      g_free(utf16Text);
      return "";
   }

   // Convert it back to UTF-8/ASCII.
   char *utf8Idn = g_utf16_to_utf8((gunichar2 *)idnText, len, NULL, NULL,
                                   NULL);
   string ret(utf8Idn);
   g_free(idnText);
   g_free(utf8Idn);
   g_free(utf16Text);
   return ret;
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
   string text(label);
   /*
    * If there are multibyte characters, we need to convert from IDN
    * to ASCII (punycode).
    */
   if (g_utf8_strlen(text.c_str(), -1) != (long)text.length()) {
      text = IDNToASCII(text);
   }
   if (text.empty()) {
      return text;
   }

   xmlURIPtr parsed = xmlParseURI(text.c_str());
   if (parsed == NULL || parsed->server == NULL) {
      text = "https://" + text;
      /* xmlParseURI requires that the protocol be specified in order to parse
       * correctly.  In the event that the parsing fails, this line will add
       * the protocol in as "https://" and attempt to reparse.  If it fails
       * the second time, it will fall back to default values.
       */
      parsed = xmlParseURI(text.c_str());
   }

   /*
    * For some invalid URLs, path will contain a bunch of stuff.
    * Since the user shouldn't be specifying a path here, use that to
    * catch a few strange parsing errors.
    * See bugs 379938 & 367370.
    */
   if (parsed != NULL &&
       (parsed->path == NULL || parsed->path[0] == '\0') &&
       parsed->server && strlen(parsed->server) <= MAX_HOSTNAME_LENGTH) {
      bool tmpSecure = !parsed->scheme || !strcmp(parsed->scheme, "https");
      if (secure) {
	 *secure = tmpSecure;
      }
      if (port) {
	 *port = parsed->port ? parsed->port : (tmpSecure ? 443 : 80);
      }
      text = parsed->server ? parsed->server : "";
   } else {
      text = "";
   }
   xmlFreeURI(parsed);
   return text;
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
   ASSERT(rv == 0);
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

   if (selfPath == BINDIR) {
      selfPath = systemPath;
   } else {
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

ClientInfoMap
GetClientInfo(const string broker, // IN
              int port)            // IN
{
   ClientInfoMap info = GetNICInfo(broker, port);

   char name[MAX_HOSTNAME_LENGTH + 1];
   if (gethostname(name, MAX_HOSTNAME_LENGTH) == 0) {
      name[MAX_HOSTNAME_LENGTH] = '\0';
      info["Machine_Name"] = name;
   } else {
      Warning("gethostname() failed: %s\n", strerror(errno));
   }
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

   time_t now = time(NULL);
   long gmt_offset = localtime(&now)->tm_gmtoff;
   info["TimeOffset_GMT"] = Format("%.2d:%.2d", gmt_offset / 3600,
                                   abs(gmt_offset) % 3600 / 60);

#ifdef __linux__
   info["Type"] = "Linux";
#elif defined(__APPLE__)
   info["Type"] = "Mac";
#endif

   return info;
}


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

   int sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock < 0) {
      Warning("socket() failed while compiling client info: %s\n",
              strerror(errno));
      return info;
   }

   struct sockaddr_in server;
   memcpy(&server.sin_addr.s_addr, serverInfo->h_addr, serverInfo->h_length);
   server.sin_family = AF_INET;
   server.sin_port = htons(port);

   if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
      Warning("connect() failed while compiling client info: %s\n",
              strerror(errno));
      close(sock);
      return info;
   }

   struct sockaddr local;
   socklen_t addrSize = sizeof(local);
   if (getsockname(sock, &local, &addrSize) < 0) {
      Warning("getsockname() failed to get the local IP address while "
              "compiling client info: %s\n", strerror(errno));
      close(sock);
      return info;
   }

   char buffer[256];
   if (inet_ntop(local.sa_family, &local.sa_data[2], buffer,
                 sizeof(buffer)) == NULL) {
      Warning("inet_ntop() failed to format the local IP address while "
              "compiling client info: %s\n", strerror(errno));
      close(sock);
      return info;
   }
   info["IP_Address"] = buffer;

#ifdef __linux__
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
      if (memcmp(&local.sa_data[2], &ifr.ifr_addr.sa_data[2],
          4 * sizeof(local.sa_data[2])) == 0) {
         // Get the MAC Address of the network card.
         if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
             Warning("ioctl() failed to get the local MAC address while "
                     "compiling client info: %s\n", strerror(errno));
         } else {
            info["MAC_Address"] = Format("%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
                      (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[0],
                      (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[1],
                      (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[2],
                      (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[3],
                      (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[4],
                      (unsigned int)(unsigned char)ifr.ifr_hwaddr.sa_data[5]);
         }
         break;
      }
   }
#endif
   close(sock);
   return info;
}


} // namespace Util
} // namespace cdk
