/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkutils.h>
#include <cdk/cdkstyle.h>
#include <geanyplugin.h>

const gchar *
cdk_document_get_contents (struct GeanyDocument *doc)
{
  return (const gchar *)
    scintilla_send_message (doc->editor->sci, SCI_GETCHARACTERPOINTER, 0, 0);
}

gsize
cdk_document_get_length (struct GeanyDocument *doc)
{
  return scintilla_send_message (doc->editor->sci, SCI_GETLENGTH, 0, 0);
}

void
cdk_scintilla_set_style (struct _ScintillaObject *sci, guint id, const struct CdkStyle *style)
{
  g_return_if_fail (IS_SCINTILLA (sci));
  g_return_if_fail (style != NULL);

  cdk_sci_send (sci, SCI_STYLESETBACK, id, style->back);
  cdk_sci_send (sci, SCI_STYLESETFORE, id, style->fore);
  cdk_sci_send (sci, SCI_STYLESETBOLD, id, style->bold);
  cdk_sci_send (sci, SCI_STYLESETITALIC, id, style->italic);
  if (style->font != NULL)
    cdk_sci_send (sci, SCI_STYLESETFONT, id, style->font);
  if (style->size > 0)
    cdk_sci_send (sci, SCI_STYLESETSIZE, id, style->size);
}

gchar *
cdk_sci_get_current_word (struct _ScintillaObject *sci)
{
  g_return_val_if_fail (IS_SCINTILLA (sci), NULL);

  gint cur_pos = cdk_sci_send (sci, SCI_GETCURRENTPOS, 0, 0);
  gint word_start = cdk_sci_send (sci, SCI_WORDSTARTPOSITION, cur_pos, TRUE);
  gint word_end = cdk_sci_send (sci, SCI_WORDENDPOSITION, cur_pos, TRUE);

  struct Sci_TextRange tr;
  tr.chrg.cpMin = word_start;
  tr.chrg.cpMax = word_end;
  tr.lpstrText = g_malloc0 ((word_end - word_start) + 1);

  cdk_sci_send (sci, SCI_GETTEXTRANGE, 0, &tr);

  return tr.lpstrText;
}
