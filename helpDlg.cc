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
 * helpDlg.cc --
 *
 *    Help file display dialog.
 */


#include "helpDlg.hh"


#include "gtm.h"


namespace cdk {


GtkWidget *HelpDlg::sDialog = NULL;


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpDlg::ShowHelp --
 *
 *      Returns pointer to HelpDlg.  If no HelpDlg currently exists,
 *      a new one is constructed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      New window is shown or existed window is given focus.
 *
 *-----------------------------------------------------------------------------
 */

void
HelpDlg::ShowHelp(GtkWindow *parent) // IN
{
   if (sDialog) {
      gtk_window_present(GTK_WINDOW(sDialog));
      return;
   }

   sDialog = gtk_dialog_new_with_buttons(gtk_window_get_title(parent),
                                         parent, GTK_DIALOG_NO_SEPARATOR,
                                         NULL);

   g_object_add_weak_pointer(G_OBJECT(sDialog), (gpointer *)&sDialog);
   gtk_dialog_add_button(GTK_DIALOG(sDialog), GTK_STOCK_CLOSE,
                         GTK_RESPONSE_CLOSE);
   gtk_dialog_set_default_response(GTK_DIALOG(sDialog), GTK_RESPONSE_CLOSE);
   g_signal_connect(sDialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);

   GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_show(GTK_WIDGET(scrolledWindow));
   gtk_box_pack_start(GTK_BOX(GTK_DIALOG(sDialog)->vbox),
                      GTK_WIDGET(scrolledWindow), true, true, 0);
   gtk_widget_set_size_request(GTK_WIDGET(scrolledWindow), 500, 250);
   gtk_container_set_border_width(GTK_CONTAINER(scrolledWindow), VM_SPACING);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledWindow),
                                       GTK_SHADOW_IN);

   Util::string helpText = ReadHelpFile();

   GtkTextView *textView = GTK_TEXT_VIEW(gtk_text_view_new());
   gtk_widget_show(GTK_WIDGET(textView));
   gtk_container_add(GTK_CONTAINER(scrolledWindow), GTK_WIDGET(textView));
   gtk_text_view_set_editable(textView, false);
   gtk_text_view_set_wrap_mode(textView, GTK_WRAP_WORD);

   GError *error = NULL;
   if (!gtm_set_markup(gtk_text_view_get_buffer(textView), helpText.c_str(),
                       &error)) {
      Warning("Error parsing help file: %s.\n", error->message);
      g_error_free(error);
   }

   gtk_widget_show(sDialog);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpDlg::ReadHelpFile --
 *
 *      Reads in the contents of the help file.
 *
 *
 * Results:
 *      Util::string containing the help file content.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
HelpDlg::ReadHelpFile()
{
   char *fileContents = NULL;

   Util::string helpDir = Util::GetUsefulPath(HELPDIR, "../doc/help");
   if (helpDir.empty()) {
      // Try again with the Debian help directory.
      helpDir = Util::GetUsefulPath(DEBHELPDIR, "../doc/help");
   }
   if (helpDir.empty()) {
      Util::UserWarning(_("User help directory not found; falling back "
                          "to %s.\n"), HELPDIR);
      helpDir = HELPDIR;
   }

   Util::string locale = setlocale(LC_MESSAGES, NULL);
   if (locale.empty() || locale == "C" || locale == "POSIX") {
      locale = "en";
   }
   size_t index = locale.find("_");

   if (index != Util::string::npos) {
      locale = locale.substr(0, index);
   }

   if (!GetHelpContents(helpDir.c_str(), locale.c_str(), &fileContents) &&
       locale != "en") {
      // No help file for this locale; try "en"
      g_free(fileContents);
      fileContents = NULL;
      GetHelpContents(helpDir.c_str(), "en", &fileContents);
   }

   return fileContents;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpDlg::GetHelpContents --
 *
 *      Reads in the contents of the help file.
 *
 *
 * Results:
 *      true if the file was successfully read, false otherwise
 *
 * Side effects:
 *      helpText points to the contents of the help file
 *
 *-----------------------------------------------------------------------------
 */

bool
HelpDlg::GetHelpContents(const char *directory, // IN
                         const char *locale,    // IN
                         char **helpText)       // IN/OUT
{
   GError *error = NULL;

   Util::string fileName = Util::Format("integrated_help-%s.txt", locale);
   char *file = g_build_filename(directory, fileName.c_str(), NULL);

   // Attempt to read the file
   bool readSuccess = g_file_get_contents(file, helpText, NULL, &error);
   if (!readSuccess) {
      *helpText = g_markup_printf_escaped(_("An error occurred while reading "
         "the help file: %s.\n"),
         error->message);
      g_error_free(error);
      Log("%s\n", *helpText);
   }

   g_free(file);
   return readSuccess;
}


} // namespace cdk
