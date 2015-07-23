/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDK_STYLE_H_
#define CDK_STYLE_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define CDK_TYPE_STYLE (cdk_style_get_type ())

typedef enum
{
  CDK_STYLE_DEFAULT,
  CDK_STYLE_COMMENT,
  CDK_STYLE_MEMBER_REF,
  CDK_STYLE_IDENTIFIER,
  CDK_STYLE_KEYWORD,
  CDK_STYLE_LITERAL,
  CDK_STYLE_NUMBER,
  CDK_STYLE_PREPROCESSOR,
  CDK_STYLE_PUNCTUATION,
  CDK_STYLE_STRING,
  CDK_STYLE_TYPE_NAME,
  CDK_STYLE_FUNCTION_CALL,
  CDK_STYLE_CHARACTER,
  CDK_NUM_STYLES,
}
CdkStyleID;

typedef struct
{
  guint32  fore;
  guint32  back;
  gboolean bold;
  gboolean italic;
}
CdkStyle;

GType cdk_style_get_type (void);
CdkStyle *cdk_style_new (void);
void cdk_style_free (CdkStyle *style);
CdkStyle *cdk_style_copy (CdkStyle *style);

G_END_DECLS

#endif // CDK_STYLE_H_
