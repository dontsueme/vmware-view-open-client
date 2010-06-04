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
 * helpSupportDlg.cc --
 *
 *    Implementation of the HelpSupportDlg.
 */


#include "helpSupportDlg.hh"


#include "gtm.h"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::HelpSupportDlg --
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

HelpSupportDlg::HelpSupportDlg()
   : mDialog(NULL),
     mHelpTextView(NULL),
     mParent(NULL)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::~HelpSupportDlg --
 *
 *      Destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

HelpSupportDlg::~HelpSupportDlg()
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::Run --
 *
 *      Displays the help and support dialog.
 *
 *
 * Results:
 *      None
 *
 * Side effects:
 *      New window is shown or existing window is given focus.
 *
 *-----------------------------------------------------------------------------
 */

void
HelpSupportDlg::Run()
{
   if (mDialog) {
      InsertHelpText();
      gtk_window_present(GTK_WINDOW(mDialog));
      return;
   }

   mDialog = GTK_DIALOG(gtk_dialog_new_with_buttons(
                           gtk_window_get_title(mParent),
                           mParent, GTK_DIALOG_NO_SEPARATOR,
                           NULL));

   g_object_add_weak_pointer(G_OBJECT(mDialog), (gpointer *)&mDialog);
   GtkButton *button = Util::CreateButton(GTK_STOCK_CLOSE);
   gtk_box_pack_start(GTK_BOX(mDialog->action_area), GTK_WIDGET(button),
                      false, false, 0);
   g_signal_connect_swapped(button, "clicked",
                            G_CALLBACK(gtk_widget_destroy), mDialog);

   GtkWidget *notebook = gtk_notebook_new();
   gtk_widget_show(notebook);
   gtk_box_pack_start(GTK_BOX(GTK_DIALOG(mDialog)->vbox),
                      notebook, true, true, 0);
   gtk_notebook_set_homogeneous_tabs(GTK_NOTEBOOK(notebook), true);

   GtkLabel *label = GTK_LABEL(gtk_label_new_with_mnemonic(_("_Help")));
   gtk_misc_set_padding(GTK_MISC(label), VM_SPACING, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), CreateHelpTab(),
                            GTK_WIDGET(label));
   label = GTK_LABEL(gtk_label_new_with_mnemonic(_("_Support Information")));
   gtk_misc_set_padding(GTK_MISC(label), VM_SPACING, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), CreateSupportTab(),
                            GTK_WIDGET(label));

   gtk_widget_show(GTK_WIDGET(mDialog));
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::CreateHelpTab --
 *
 *      Creates the widget to put into the help tab for the notebook.
 *
 *
 * Results:
 *      GtkWidget * containing help dialog contents.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkWidget *
HelpSupportDlg::CreateHelpTab()
{
   GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_show(scrolledWindow);
   gtk_widget_set_size_request(scrolledWindow, 500, 250);
   gtk_container_set_border_width(GTK_CONTAINER(scrolledWindow), VM_SPACING);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledWindow),
                                       GTK_SHADOW_IN);

   mHelpTextView = GTK_TEXT_VIEW(gtk_text_view_new());
   gtk_widget_show(GTK_WIDGET(mHelpTextView));
   gtk_container_add(GTK_CONTAINER(scrolledWindow), GTK_WIDGET(mHelpTextView));
   gtk_text_view_set_editable(mHelpTextView, false);
   gtk_text_view_set_wrap_mode(mHelpTextView, GTK_WRAP_WORD);
   g_object_add_weak_pointer(G_OBJECT(mHelpTextView),
                             (gpointer *)&mHelpTextView);

   InsertHelpText();

   return scrolledWindow;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::InsertHelpText --
 *
 *      Reads in help text to mHelpTextView.
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
HelpSupportDlg::InsertHelpText()
{
   ASSERT(mHelpTextView);

   GError *error = NULL;
   if (!gtm_set_markup(gtk_text_view_get_buffer(mHelpTextView),
                       ReadHelpFile().c_str(), &error)) {
      Warning("Error parsing help file: %s.\n", error->message);
      g_error_free(error);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::ReadHelpFile --
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
HelpSupportDlg::ReadHelpFile()
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

   /*
    * This has some progressive fallback for locales.  For example,
    * fr_CA.UTF-8 will be tried as that, then fr_CA, then fr, finally
    * defaulting to en.  If that fails, we just use the error text for
    * the dialog.
    */
   while (!GetHelpContents(helpDir.c_str(), locale.c_str(), &fileContents)) {
      size_t index = locale.find(".");
      if (index == Util::string::npos) {
         index = locale.find("_");
         if (index == Util::string::npos) {
            if (locale != "en") {
               g_free(fileContents);
               fileContents = NULL;
               // Try to load the en one as a last-gasp effort.
               GetHelpContents(helpDir.c_str(), "en", &fileContents);
            }
            break;
         }
      }
      g_free(fileContents);
      fileContents = NULL;
      locale = locale.substr(0, index);
   }

   Util::string ret = fileContents;
   g_free(fileContents);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::GetHelpContents --
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
HelpSupportDlg::GetHelpContents(const char *directory,       // IN
                                const char *locale,          // IN
                                char **helpText)             // IN/OUT
{
   GError *error = NULL;

   char *file = g_build_filename(directory, locale,
                                 (mHelpContext + ".txt").c_str(),
                                 NULL);

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


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::CreateSupportTab --
 *
 *      Creates the widget to put into the support tab for the notebook.
 *
 *
 * Results:
 *      GtkWidget * containing support dialog contents.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkWidget *
HelpSupportDlg::CreateSupportTab()
{
   GtkTable *table = GTK_TABLE(gtk_table_new(1, 3, false));
   gtk_widget_show(GTK_WIDGET(table));
   gtk_container_set_border_width(GTK_CONTAINER(table), VM_SPACING);
   gtk_table_set_row_spacings(table, VM_SPACING);
   gtk_table_set_col_spacings(table, VM_SPACING);

   GtkTextView *textView = GetSupportView(mSupportFile);

   if (textView) {
      AddTitle(table, _("Support Information"));

      GtkWidget *scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
      gtk_widget_show(GTK_WIDGET(scrolledWindow));
      AddWidget(table, GTK_WIDGET(scrolledWindow));
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                     GTK_POLICY_AUTOMATIC,
                                     GTK_POLICY_ALWAYS);
      gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledWindow),
                                          GTK_SHADOW_IN);

      gtk_container_add(GTK_CONTAINER(scrolledWindow), GTK_WIDGET(textView));
   }

   CreateProductInformationSection(table);
   CreateHostInformationSection(table);
   CreateConnectionInformationSection(table);

   return GTK_WIDGET(table);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::CreateProductInformationSection --
 *
 *      Adds the "Product Information" section to the passed in table.
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
HelpSupportDlg::CreateProductInformationSection(GtkTable *table) // IN
{
   AddTitle(table, _("Product Information"));
   AddPair(table, _("Product:"), ProductState_GetName());
   AddPair(table, _("Version:"), GetVersionString());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::CreateHostInformationSection --
 *
 *      Adds the "Host Information" section to the passed in table.
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
HelpSupportDlg::CreateHostInformationSection(GtkTable *table) // IN
{
   AddTitle(table, _("Host Information"));
   AddPair(table, _("Host Name:"), Util::GetClientHostName());
   if (ShowLogLocation()) {
      AddPair(table, _("Log File:"), Log_GetFileName());
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::CreateConnectionInformationSection --
 *
 *      Adds the "Connection Information" section to the passed in table.
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
HelpSupportDlg::CreateConnectionInformationSection(GtkTable *table) // IN
{
   AddTitle(table, _("Connection Information"));
   AddPair(table, _("VMware View Server:"), mBrokerHostName);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::CreateLabel --
 *
 *      Creates a left aligned, selectable label with the given text.
 *      gtk_widget_show is called on the new label before it is returned.
 *
 * Results:
 *      A GtkLabel
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkLabel *
HelpSupportDlg::CreateLabel(const Util::string &text) // IN
{
   GtkLabel *l = GTK_LABEL(gtk_label_new(text.c_str()));
   gtk_widget_show(GTK_WIDGET(l));
   gtk_misc_set_alignment(GTK_MISC(l), 0, 0);
   gtk_label_set_selectable(l, true);

   return l;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::AppendRow --
 *
 *      Append a new row to table and return new row number.
 *
 * Results:
 *      The index of the newly added row.
 *
 * Side effects:
 *      A row is added to the table
 *
 *-----------------------------------------------------------------------------
 */

guint
HelpSupportDlg::AppendRow(GtkTable *table) // IN
{
   guint rows;
   guint columns;
   g_object_get(table, "n-rows", &rows, "n-columns", &columns, NULL);
   gtk_table_resize(table, rows + 1, columns);
   return rows;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::AddTitle --
 *
 *      Add a label with bold text into the table.
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
HelpSupportDlg::AddTitle(GtkTable *table,          // IN
                         const Util::string &text) // IN
{
   guint row = AppendRow(table);

   GtkLabel *l = CreateLabel("");
   gtk_table_attach_defaults(table, GTK_WIDGET(l), 0, 3, row, row + 1);
   gtk_label_set_markup(l, Util::Format("<b>%s</b>", text.c_str()).c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::AddPair --
 *
 *      Appends two labels to the table: one is a label, the second is data.
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
HelpSupportDlg::AddPair(GtkTable *table,           // IN
                        const Util::string &label, // IN
                        const Util::string &data)  // IN
{
   guint row = AppendRow(table);

   GtkLabel *l = CreateLabel(label);
   gtk_table_attach_defaults(table, GTK_WIDGET(l), 1, 2, row, row + 1);
   l = CreateLabel(data);
   gtk_table_attach_defaults(table, GTK_WIDGET(l), 2, 3, row, row + 1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::AddWidget --
 *
 *      Appends the given widget to the table.  It will span the 2nd and 3rd
 *      columns.
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
HelpSupportDlg::AddWidget(GtkTable *table,   // IN
                          GtkWidget *widget) // IN
{
   guint row = AppendRow(table);

   gtk_table_attach_defaults(table, widget, 1, 3, row, row + 1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::HelpSupportDlg::GetSupportView --
 *
 *      Read in the contents of the passed in file and return a
 *      GtkTextView with the contents of the file.
 *
 * Results:
 *      GtkTextView *
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkTextView *
HelpSupportDlg::GetSupportView(const Util::string &filePath) // IN
{
   GError *error = NULL;
   char *supportText = NULL;

   if (!g_file_get_contents(filePath.c_str(), &supportText, NULL, &error)) {
      Log(_("An error occurred while reading the support file: %s.\n"),
          error->message);
      g_error_free(error);
      return NULL;
   }

   GtkTextView *textView = GTK_TEXT_VIEW(gtk_text_view_new());
   gtk_widget_show(GTK_WIDGET(textView));
   gtk_text_view_set_editable(textView, false);
   gtk_text_view_set_wrap_mode(textView, GTK_WRAP_WORD);

   gtk_text_buffer_set_text(gtk_text_view_get_buffer(textView),
                            supportText, -1);
   g_free(supportText);
   return textView;
}


} // namespace cdk
