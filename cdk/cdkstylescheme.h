/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDKSTYLESCHEME_H_
#define CDKSTYLESCHEME_H_ 1

#include <cdk/cdkstyle.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CDK_TYPE_STYLE_SCHEME            (cdk_style_scheme_get_type ())
#define CDK_STYLE_SCHEME(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDK_TYPE_STYLE_SCHEME, CdkStyleScheme))
#define CDK_STYLE_SCHEME_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CDK_TYPE_STYLE_SCHEME, CdkStyleSchemeClass))
#define CDK_IS_STYLE_SCHEME(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDK_TYPE_STYLE_SCHEME))
#define CDK_IS_STYLE_SCHEME_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDK_TYPE_STYLE_SCHEME))
#define CDK_STYLE_SCHEME_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CDK_TYPE_STYLE_SCHEME, CdkStyleSchemeClass))

typedef struct CdkStyleScheme_        CdkStyleScheme;
typedef struct CdkStyleSchemeClass_   CdkStyleSchemeClass;
typedef struct CdkStyleSchemePrivate_ CdkStyleSchemePrivate;

struct CdkStyleScheme_
{
  GObject parent;
  CdkStyleSchemePrivate *priv;
};

struct CdkStyleSchemeClass_
{
  GObjectClass parent_class;
};

void cdk_style_schemes_init (void);
void cdk_style_schemes_cleanup (void);

GType cdk_style_scheme_get_type (void);
CdkStyleScheme *cdk_style_scheme_new (const gchar *filename);
const gchar *cdk_style_scheme_get_filename (CdkStyleScheme *self);
void cdk_style_scheme_set_filename (CdkStyleScheme *self, const gchar *filename);
const gchar *cdk_style_scheme_get_name (CdkStyleScheme *self);
void cdk_style_scheme_set_name (CdkStyleScheme *self, const gchar *name);
CdkStyle *cdk_style_scheme_get_style (CdkStyleScheme *self, CdkStyleID style_id);
gboolean cdk_style_scheme_reload (CdkStyleScheme *self);

CdkStyleID cdk_style_id_for_token_kind (guint token_kind);
CdkStyleID cdk_style_id_for_cursor_kind (guint cursor_kind);
gboolean cdk_style_id_is_for_syntax (CdkStyleID id);

G_END_DECLS

#endif /* CDKSTYLESCHEME_H_ */
