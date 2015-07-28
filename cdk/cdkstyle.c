/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkstyle.h>
#include <gdk/gdk.h>

GType
cdk_style_get_type (void)
{
  static GType type_id = 0;
  if (G_UNLIKELY (type_id == 0))
    {
      type_id =
        g_boxed_type_register_static ("CdkStyle",
                                      (GBoxedCopyFunc) cdk_style_copy,
                                      (GBoxedFreeFunc) cdk_style_free);
    }
    return type_id;
}

CdkStyle *
cdk_style_new (void)
{
  CdkStyle *style = g_slice_new0 (CdkStyle);
  return style;
}

void
cdk_style_free (CdkStyle *style)
{
  if (style != NULL)
    {
      g_free (style->font);
      g_slice_free (CdkStyle, style);
    }
}

CdkStyle *
cdk_style_copy (CdkStyle *style)
{
  CdkStyle *new_style = NULL;
  if (style != NULL)
    {
      new_style = g_slice_new0 (CdkStyle);
      *new_style = *style;
      if (style->font != NULL)
        new_style->font = g_strdup (style->font);
    }
  return new_style;
}

GType
cdk_style_id_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0))
    {
      static const GEnumValue values[] = {
        { CDK_STYLE_DEFAULT,            "CDK_STYLE_DEFAULT",            "DEFAULT" },
        { CDK_STYLE_COMMENT,            "CDK_STYLE_COMMENT",            "COMMENT" },
        { CDK_STYLE_MEMBER_REF,         "CDK_STYLE_MEMBER_REF",         "MEMBER_REF" },
        { CDK_STYLE_IDENTIFIER,         "CDK_STYLE_IDENTIFIER",         "IDENTIFIER" },
        { CDK_STYLE_KEYWORD,            "CDK_STYLE_KEYWORD",            "KEYWORD" },
        { CDK_STYLE_LITERAL,            "CDK_STYLE_LITERAL",            "LITERAL" },
        { CDK_STYLE_NUMBER,             "CDK_STYLE_NUMBER",             "NUMBER" },
        { CDK_STYLE_PREPROCESSOR,       "CDK_STYLE_PREPROCESSOR",       "PREPROCESSOR" },
        { CDK_STYLE_PUNCTUATION,        "CDK_STYLE_PUNCTUATION",        "PUNCTUATION" },
        { CDK_STYLE_STRING,             "CDK_STYLE_STRING",             "STRING" },
        { CDK_STYLE_TYPE_NAME,          "CDK_STYLE_TYPE_NAME",          "TYPE_NAME" },
        { CDK_STYLE_FUNCTION_CALL,      "CDK_STYLE_FUNCTION_CALL",      "FUNCTION_CALL" },
        { CDK_STYLE_CHARACTER,          "CDK_STYLE_CHARACTER",          "CHARACTER" },
        { CDK_STYLE_DIAGNOSTIC_WARNING, "CDK_STYLE_DIAGNOSTIC_WARNING", "DIAGNOSTIC_WARNING" },
        { CDK_STYLE_DIAGNOSTIC_ERROR,   "CDK_STYLE_DIAGNOSTIC_ERROR",   "DIAGNOSTIC_ERROR" },
        { CDK_STYLE_ANNOTATION_WARNING, "CDK_STYLE_ANNOTATION_WARNING", "ANNOTATION_WARNING" },
        { CDK_STYLE_ANNOTATION_ERROR,   "CDK_STYLE_ANNOTATION_ERROR",   "ANNOTATION_ERROR" },
      };
      type = g_enum_register_static ("CdkStyleID", values);
    }
  return type;
}
