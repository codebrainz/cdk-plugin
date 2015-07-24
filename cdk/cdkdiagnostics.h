/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_DIAGNOSTICS_H_
#define CDK_DIAGNOSTICS_H_ 1

#include <cdk/cdkstylescheme.h>
#include <cdk/cdkdocumenthelper.h>

G_BEGIN_DECLS

#define CDK_TYPE_DIAGNOSTICS            (cdk_diagnostics_get_type ())
#define CDK_DIAGNOSTICS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDK_TYPE_DIAGNOSTICS, CdkDiagnostics))
#define CDK_DIAGNOSTICS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CDK_TYPE_DIAGNOSTICS, CdkDiagnosticsClass))
#define CDK_IS_DIAGNOSTICS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDK_TYPE_DIAGNOSTICS))
#define CDK_IS_DIAGNOSTICS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDK_TYPE_DIAGNOSTICS))
#define CDK_DIAGNOSTICS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CDK_TYPE_DIAGNOSTICS, CdkDiagnosticsClass))

typedef struct CdkDiagnostics_        CdkDiagnostics;
typedef struct CdkDiagnosticsClass_   CdkDiagnosticsClass;
typedef struct CdkDiagnosticsPrivate_ CdkDiagnosticsPrivate;

struct CdkDiagnostics_
{
  CdkDocumentHelper parent;
  CdkDiagnosticsPrivate *priv;
};

struct CdkDiagnosticsClass_
{
  CdkDocumentHelperClass parent_class;
};

GType cdk_diagnostics_get_type (void);
CdkDiagnostics *cdk_diagnostics_new (struct CdkPlugin_ *plugin, struct GeanyDocument *doc);
CdkStyleScheme *cdk_diagnostics_get_style_scheme (CdkDiagnostics *self);
void cdk_diagnostics_set_style_scheme (CdkDiagnostics *self, CdkStyleScheme *scheme);
gboolean cdk_diagnostics_get_indicators_enabled (CdkDiagnostics *self);
void cdk_diagnostics_set_indicators_enabled (CdkDiagnostics *self, gboolean enabled);
gboolean cdk_diagnostics_get_markers_enabled (CdkDiagnostics *self);
void cdk_diagnostics_set_markers_enabled (CdkDiagnostics *self, gboolean enabled);

typedef gboolean (*CdkDiagnosticFunc) (CdkDiagnostics *diag,
                                       gpointer cx_diag,
                                       guint position,
                                       gpointer user_data);

typedef gboolean (*CdkDiagnosticRangeFunc) (CdkDiagnostics *diag,
                                            gpointer cx_diag,
                                            guint index,
                                            guint start,
                                            guint end,
                                            gpointer user_data);

gint cdk_diagnostics_foreach (CdkDiagnostics *self, CdkDiagnosticFunc func, gpointer user_data);
gint cdk_diagnostics_foreach_range (CdkDiagnostics *self,  CdkDiagnosticRangeFunc func, gpointer user_data);

G_END_DECLS

#endif /* CDK_DIAGNOSTICS_H_ */
