/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_DOCUMENT_HELPER_H_
#define CDK_DOCUMENT_HELPER_H_ 1

#include <glib-object.h>

G_BEGIN_DECLS

struct GeanyDocument;
struct CdkPlugin_;

#define CDK_TYPE_DOCUMENT_HELPER            (cdk_document_helper_get_type ())
#define CDK_DOCUMENT_HELPER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDK_TYPE_DOCUMENT_HELPER, CdkDocumentHelper))
#define CDK_DOCUMENT_HELPER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CDK_TYPE_DOCUMENT_HELPER, CdkDocumentHelperClass))
#define CDK_IS_DOCUMENT_HELPER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDK_TYPE_DOCUMENT_HELPER))
#define CDK_IS_DOCUMENT_HELPER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDK_TYPE_DOCUMENT_HELPER))
#define CDK_DOCUMENT_HELPER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CDK_TYPE_DOCUMENT_HELPER, CdkDocumentHelperClass))

typedef struct CdkDocumentHelper_        CdkDocumentHelper;
typedef struct CdkDocumentHelperClass_   CdkDocumentHelperClass;
typedef struct CdkDocumentHelperPrivate_ CdkDocumentHelperPrivate;

struct CdkDocumentHelper_
{
  GObject parent;
  CdkDocumentHelperPrivate *priv;
};

struct CdkDocumentHelperClass_
{
  GObjectClass parent_class;
  void (*initialize)   (CdkDocumentHelper *self, struct GeanyDocument *doc);
  void (*updated)      (CdkDocumentHelper *self, struct GeanyDocument *doc);
};

GType                 cdk_document_helper_get_type     (void);
struct CdkPlugin_    *cdk_document_helper_get_plugin   (CdkDocumentHelper *self);
struct GeanyDocument *cdk_document_helper_get_document (CdkDocumentHelper *self);
void                  cdk_document_helper_updated      (CdkDocumentHelper *self);

G_END_DECLS

#endif /* CDK_DOCUMENT_HELPER_H_ */
