/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#include <cdk/cdkutils.h>
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
