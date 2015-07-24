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
  GType type_id = 0;
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
