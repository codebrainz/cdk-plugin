/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_PLUGIN_H_
#define CDK_PLUGIN_H_ 1

#include <cdk/cdkstylescheme.h>
#include <glib-object.h>

G_BEGIN_DECLS

struct GeanyDocument;
struct CXTranslationUnitImpl;

#define CDK_TYPE_PLUGIN            (cdk_plugin_get_type ())
#define CDK_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDK_TYPE_PLUGIN, CdkPlugin))
#define CDK_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CDK_TYPE_PLUGIN, CdkPluginClass))
#define CDK_IS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDK_TYPE_PLUGIN))
#define CDK_IS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDK_TYPE_PLUGIN))
#define CDK_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CDK_TYPE_PLUGIN, CdkPluginClass))

typedef struct CdkPlugin_        CdkPlugin;
typedef struct CdkPluginClass_   CdkPluginClass;
typedef struct CdkPluginPrivate_ CdkPluginPrivate;

struct CdkPlugin_
{
  GObject parent;
  CdkPluginPrivate *priv;
};

struct CdkPluginClass_
{
  GObjectClass parent_class;
};

GType cdk_plugin_get_type (void);
CdkPlugin *cdk_plugin_new (void);
gboolean cdk_plugin_add_document (CdkPlugin *self, struct GeanyDocument *doc);
gboolean cdk_plugin_remove_document (CdkPlugin *self, struct GeanyDocument *doc);
gboolean cdk_plugin_update_document (CdkPlugin *self, struct GeanyDocument *doc);
struct CXTranslationUnitImpl *cdk_plugin_get_translation_unit (CdkPlugin *self, struct GeanyDocument *doc);
void cdk_plugin_open_project (CdkPlugin *self, GKeyFile *config);
void cdk_plugin_save_project (CdkPlugin *self, GKeyFile *config);
void cdk_plugin_close_project (CdkPlugin *self);
gboolean cdk_plugin_is_project_open (CdkPlugin *self);
struct GeanyDocument *cdk_plugin_get_current_document (CdkPlugin *self);
void cdk_plugin_set_current_document (CdkPlugin *self, struct GeanyDocument *doc);
const gchar *cdk_plugin_get_cflags (CdkPlugin *self);
const gchar *const *cdk_plugin_get_files (CdkPlugin *self, gsize *n_files);
void cdk_plugin_set_cflags (CdkPlugin *self, const gchar *cflags);
void cdk_plugin_set_files (CdkPlugin *self, const gchar *const *files, gssize n_files);
CdkStyleScheme *cdk_plugin_get_style_scheme (CdkPlugin *self);
void cdk_plugin_set_style_scheme (CdkPlugin *self, CdkStyleScheme *scheme);

G_END_DECLS

#endif /* CDK_PLUGIN_H_ */
