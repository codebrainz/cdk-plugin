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
}
