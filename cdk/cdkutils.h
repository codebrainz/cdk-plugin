/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_UTILS_H
#define CDK_UTILS_H

#include <glib.h>

struct GeanyDocument;
struct GeanyData;
extern struct GeanyData *geany_data;

G_BEGIN_DECLS

const gchar *cdk_document_get_contents (struct GeanyDocument *doc);
gsize cdk_document_get_length (struct GeanyDocument *doc);

#define cdk_plugin_get() \
  ((CdkPlugin*) g_object_get_data (G_OBJECT (geany_data->main_widgets->window), "cdk-plugin"))
#define cdk_plugin_is_loaded() (CDK_IS_PLUGIN (cdk_plugin_get ()))

G_END_DECLS

#endif // CDK_UTILS_H
