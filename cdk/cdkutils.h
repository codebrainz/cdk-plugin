/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_UTILS_H
#define CDK_UTILS_H

#include <glib.h>

struct GeanyDocument;
struct _ScintillaObject;
struct CdkStyle;
struct _ScintillaObject;
struct GeanyData;
extern struct GeanyData *geany_data;

G_BEGIN_DECLS

const gchar *cdk_document_get_contents (struct GeanyDocument *doc);
gsize cdk_document_get_length (struct GeanyDocument *doc);

void cdk_scintilla_set_style (struct _ScintillaObject *sci, guint id, const struct CdkStyle *style);

#define cdk_plugin_get() \
  ((CdkPlugin*) g_object_get_data (G_OBJECT (geany_data->main_widgets->window), "cdk-plugin"))
#define cdk_plugin_is_loaded() (CDK_IS_PLUGIN (cdk_plugin_get ()))

#define cdk_sci_send(sci, msg, uparam, lparam) \
  scintilla_send_message (SCINTILLA (sci), (guint)(msg), (uptr_t)(uparam), (sptr_t)(lparam))

gchar *cdk_sci_get_current_word (struct _ScintillaObject *sci);

gchar *cdk_abspath (const gchar *path);
gchar *cdk_relpath (const gchar *path, const gchar *rel_dir);

G_END_DECLS

#endif // CDK_UTILS_H
