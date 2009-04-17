/*********************************************************
 * Copyright (C) 2004 Tim-Philipp Muller
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
 *
 * This code originally from Gtk's Bugzilla:
 *
 * http://bugzilla.gnome.org/show_bug.cgi?id=59390
 *
 * http://bugzilla.gnome.org/attachment.cgi?id=28723
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 *********************************************************/

/*
 * gtm.c --
 *
 *    Load GMarkup into a GtkTextBuffer.
 */


#include "gtm.h"


/*
 *-----------------------------------------------------------------------------
 *
 * gtm_apply_attributes --
 *
 *      Iterates through the list of PangoAttributes and sets the
 *      appropriate properties for the text tag.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Frees the values and list passed in (attrs)
 *
 *-----------------------------------------------------------------------------
 */

static void
gtm_apply_attributes(GSList *attrs,   /* IN */
                     GtkTextTag *tag) /* IN/OUT */
{
   PangoAttribute *attr;
   PangoAttrType attrType;

   while (attrs != NULL) {
      attr = (PangoAttribute *)attrs->data;

      attrType = attr->klass->type;

      switch (attrType) {
      case PANGO_ATTR_LANGUAGE:
         g_object_set(
            tag, "language",
            pango_language_to_string(((PangoAttrLanguage *)attr)->value), NULL);
         break;
      case PANGO_ATTR_FAMILY:
         g_object_set(tag, "family", ((PangoAttrString *)attr)->value, NULL);
         break;
      case PANGO_ATTR_STYLE:
         g_object_set(tag, "style", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_WEIGHT:
         g_object_set(tag, "weight", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_VARIANT:
         g_object_set(tag, "variant", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_STRETCH:
         g_object_set(tag, "stretch", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_SIZE:
         g_object_set(tag, "size", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_FONT_DESC:
         g_object_set(tag, "font-desc", ((PangoAttrFontDesc *)attr)->desc,
                      NULL);
         break;
      case PANGO_ATTR_FOREGROUND: {
         GdkColor col = { 0, ((PangoAttrColor *)attr)->color.red,
                          ((PangoAttrColor *)attr)->color.green,
                          ((PangoAttrColor *)attr)->color.blue };
         g_object_set(tag, "foreground-gdk", &col, NULL);
         break;
      }
      case PANGO_ATTR_BACKGROUND: {
         GdkColor col = { 0, ((PangoAttrColor *)attr)->color.red,
                          ((PangoAttrColor *)attr)->color.green,
                          ((PangoAttrColor *)attr)->color.blue };
         g_object_set(tag, "background-gdk", &col, NULL);
         break;
      }
      case PANGO_ATTR_UNDERLINE:
         g_object_set(tag, "underline", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_STRIKETHROUGH:
         g_object_set(tag, "strikethrough", ((PangoAttrInt *)attr)->value,
                      NULL);
         break;
      case PANGO_ATTR_RISE:
         g_object_set(tag, "rise", ((PangoAttrInt *)attr)->value, NULL);
         break;
      case PANGO_ATTR_SCALE:
         g_object_set(tag, "scale", ((PangoAttrFloat *)attr)->value, NULL);
         break;
      /*
       * These attributes are not supported for text-tag.
       */
      case PANGO_ATTR_FALLBACK:
         g_warning("Unable to apply attribute 'fallback'.\n");
         break;
      case 17: /* PANGO_ATTR_LETTER_SPACING */
         g_warning("Unable to apply attribute 'letter_spacing'.\n");
         break;
      case 18: /* PANGO_ATTR_UNDERLINE_COLOR */
         g_warning("Unable to apply attribute 'underline_color'.\n");
         break;
      case 19: /* PANGO_ATTR_STRIKETHROUGH_COLOR */
         g_warning("Unable to apply attribute 'strikethrough_color'.\n");
         break;
      case 21: /* PANGO_ATTR_GRAVITY */
         g_warning("Unable to apply attribute 'gravity'.\n");
         break;
      case 22: /* PANGO_ATTR_GRAVITY_HINT */
         g_warning("Unable to apply attribute 'gravity_hint'.\n");
         break;
      default:
         g_warning("Unknown or invalid tag encountered.\n");
         break;
      }

      attrs = g_slist_next(attrs);
   }

   /* Free GSList and contents */
   g_slist_foreach(attrs, (GFunc)pango_attribute_destroy, NULL);
   g_slist_free(attrs);
}


/*
 *-----------------------------------------------------------------------------
 *
 * gtm_get_iter_at_byte_index --
 *
 *      Iterates line by line, counting bytes instead of characters.
 *      Sets iter to the byte position indicated by index.
 *
 * Results:
 *      iter set to position indicated by index.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
gtm_get_iter_at_byte_index(GtkTextBuffer *buffer, /* IN */
                           GtkTextIter *iter,     /* OUT */
                           int index)             /* IN */
{
   int line;
   int lineBytes;
   int lineCount = gtk_text_buffer_get_line_count(buffer);
   for (line = 0; line < lineCount; ++line) {
      gtk_text_buffer_get_iter_at_line(buffer, iter, line);
      lineBytes = gtk_text_iter_get_bytes_in_line(iter);
      if (index < lineBytes) {
         gtk_text_buffer_get_iter_at_line_index(buffer, iter, line, index);
         return;
      }
      index -= lineBytes;
   }
   gtk_text_buffer_get_end_iter(buffer, iter);
}


/*
 *-----------------------------------------------------------------------------
 *
 * gtm_set_markup --
 *
 *      Takes the pango markup and applies it to the given
 *      text buffer.
 *
 * Results:
 *      FALSE on error, error is set.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

gboolean
gtm_set_markup(GtkTextBuffer *buffer, /* IN */
               const char *markup,    /* IN */
               GError **error)        /* OUT/OPT */
{
   GtkTextIter startIter;

   char *text;
   PangoAttrList *attrList;

   gboolean cont = TRUE;
   GtkTextIter endIter;
   GtkTextTag *tag;
   int end;
   int start;
   PangoAttrIterator *paIter;

   g_assert(GTK_IS_TEXT_BUFFER(buffer));
   g_assert(markup != NULL);

   gtk_text_buffer_get_start_iter(buffer, &startIter);

   if (!pango_parse_markup(markup, -1, 0, &attrList, &text, NULL, error)) {
      return FALSE;
   }

   gtk_text_buffer_insert(buffer, &startIter, text, -1);
   g_free(text);

   if (attrList == NULL) {
      return TRUE;
   }

   for (paIter = pango_attr_list_get_iterator(attrList);
        cont; cont = pango_attr_iterator_next(paIter)) {
      pango_attr_iterator_range(paIter, &start, &end);

      tag = gtk_text_buffer_create_tag(buffer, NULL, NULL);

      gtm_apply_attributes(pango_attr_iterator_get_attrs(paIter), tag);

      /* Apply tag over given range */
      gtm_get_iter_at_byte_index(buffer, &startIter, start);
      gtm_get_iter_at_byte_index(buffer, &endIter, end);

      gtk_text_buffer_apply_tag(buffer, tag, &startIter, &endIter);
   }

   pango_attr_iterator_destroy(paIter);
   pango_attr_list_unref(attrList);

   return TRUE;
}
