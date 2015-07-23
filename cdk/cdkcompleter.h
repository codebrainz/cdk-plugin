/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_COMPLETER_H_
#define CDK_COMPLETER_H_ 1

#include <cdk/cdkdocumenthelper.h>

G_BEGIN_DECLS

#define CDK_TYPE_COMPLETER            (cdk_completer_get_type ())
#define CDK_COMPLETER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDK_TYPE_COMPLETER, CdkCompleter))
#define CDK_COMPLETER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CDK_TYPE_COMPLETER, CdkCompleterClass))
#define CDK_IS_COMPLETER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDK_TYPE_COMPLETER))
#define CDK_IS_COMPLETER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDK_TYPE_COMPLETER))
#define CDK_COMPLETER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CDK_TYPE_COMPLETER, CdkCompleterClass))

typedef struct CdkCompleter_        CdkCompleter;
typedef struct CdkCompleterClass_   CdkCompleterClass;
typedef struct CdkCompleterPrivate_ CdkCompleterPrivate;

struct CdkCompleter_
{
  CdkDocumentHelper parent;
  CdkCompleterPrivate *priv;
};

struct CdkCompleterClass_
{
  CdkDocumentHelperClass parent_class;
};

GType cdk_completer_get_type (void);
CdkCompleter *cdk_completer_new (struct CdkPlugin_ *plugin, struct GeanyDocument *doc);
CdkCompleter *cdk_document_get_completer (struct GeanyDocument *doc);

G_END_DECLS

#endif /* CDK_COMPLETER_H_ */
