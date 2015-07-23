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

#if 0
static guint32
cdk_parse_color (const gchar *cstr)
{
  if (G_UNLIKELY (! cstr))
    return 0;

  GdkRGBA rgba;
  if (gdk_rgba_parse (&rgba, cstr))
    {
      guint8 r = rgba.red * 255;
      guint8 g = rgba.green * 255;
      guint8 b = rgba.blue * 255;
      return (b << 16) | (g << 8) | r;
    }

  return 0;
}
#endif

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
    g_slice_free (CdkStyle, style);
}

CdkStyle *
cdk_style_copy (CdkStyle *style)
{
  CdkStyle *new_style = NULL;
  if (style != NULL)
    {
      new_style = g_slice_new0 (CdkStyle);
      *new_style = *style;
    }
  return new_style;
}
