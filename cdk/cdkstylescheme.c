/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkstylescheme.h>
#include <gdk/gdk.h>
#include <clang-c/Index.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

struct CdkStyleSchemePrivate_
{
  gchar      *filename;
  gchar      *name;
  GHashTable *style_map;
  gboolean    in_scheme_tag;
  glong       style_id;
  guint32     fore_color;
  guint32     back_color;
  gboolean    bold;
  gboolean    italic;
  gchar      *font;
  guint       size;
};

enum
{
  PROP_0,
  PROP_FILENAME,
  PROP_NAME,
  NUM_PROPERTIES,
};

enum
{
  SIG_RELOADED,
  NUM_SIGNALS,
};

static GParamSpec *cdk_style_scheme_properties[NUM_PROPERTIES] = { NULL };
static gulong cdk_style_scheme_signals[NUM_SIGNALS] = { 0 };

static void cdk_style_scheme_finalize (GObject *object);
static void cdk_style_scheme_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void cdk_style_scheme_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);

static GHashTable *style_name_map = NULL;
static GHashTable *token_map = NULL;
static GHashTable *cursor_map = NULL;

static inline glong
style_id_from_name (const gchar *name)
{
  if (style_name_map == NULL || name == NULL)
    return -1;
  return GPOINTER_TO_SIZE (g_hash_table_lookup (style_name_map, name));
}

G_DEFINE_TYPE (CdkStyleScheme, cdk_style_scheme, G_TYPE_OBJECT)

static void
cdk_style_scheme_class_init (CdkStyleSchemeClass *klass)
{
  GObjectClass *g_object_class;

  g_object_class = G_OBJECT_CLASS (klass);

  g_object_class->finalize = cdk_style_scheme_finalize;
  g_object_class->get_property = cdk_style_scheme_get_property;
  g_object_class->set_property = cdk_style_scheme_set_property;

  cdk_style_scheme_signals[SIG_RELOADED] =
    g_signal_new ("reloaded",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  cdk_style_scheme_properties[PROP_FILENAME] =
    g_param_spec_string ("filename",
                         "Filename",
                         "Path to the style scheme XML file",
                         "",
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_style_scheme_properties[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "Display name of the style scheme",
                         "",
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  g_object_class_install_properties (g_object_class, NUM_PROPERTIES, cdk_style_scheme_properties);

  g_type_class_add_private ((gpointer)klass, sizeof (CdkStyleSchemePrivate));
}

static void
cdk_style_scheme_finalize (GObject *object)
{
  CdkStyleScheme *self;

  g_return_if_fail (CDK_IS_STYLE_SCHEME (object));

  self = CDK_STYLE_SCHEME (object);

  g_free (self->priv->filename);
  g_free (self->priv->name);

  g_hash_table_destroy (self->priv->style_map);

  G_OBJECT_CLASS (cdk_style_scheme_parent_class)->finalize (object);
}

static void
cdk_style_scheme_init (CdkStyleScheme *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_STYLE_SCHEME, CdkStyleSchemePrivate);
  self->priv->filename = NULL;
  self->priv->name = NULL;
  self->priv->style_map =
    g_hash_table_new_full (g_direct_hash, g_direct_equal,
                           NULL, (GDestroyNotify) cdk_style_free);
}

static void
cdk_style_scheme_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  CdkStyleScheme *self = CDK_STYLE_SCHEME (object);

  switch (prop_id)
    {
    case PROP_FILENAME:
      g_value_set_string (value, cdk_style_scheme_get_filename (self));
      break;
    case PROP_NAME:
      g_value_set_string (value, cdk_style_scheme_get_name (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cdk_style_scheme_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  CdkStyleScheme *self = CDK_STYLE_SCHEME (object);

  switch (prop_id)
    {
    case PROP_FILENAME:
      cdk_style_scheme_set_filename (self, g_value_get_string (value));
      break;
    case PROP_NAME:
      cdk_style_scheme_set_name (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CdkStyleScheme *
cdk_style_scheme_new (const gchar *filename)
{
  return g_object_new (CDK_TYPE_STYLE_SCHEME, "filename", filename, NULL);
}

CdkStyle *
cdk_style_scheme_get_style (CdkStyleScheme *self, CdkStyleID style_id)
{
  g_return_val_if_fail (CDK_IS_STYLE_SCHEME (self), NULL);
  return g_hash_table_lookup (self->priv->style_map, GSIZE_TO_POINTER (style_id));
}

const gchar *
cdk_style_scheme_get_filename (CdkStyleScheme *self)
{
  g_return_val_if_fail (CDK_IS_STYLE_SCHEME (self), NULL);
  return self->priv->filename;
}

void
cdk_style_scheme_set_filename (CdkStyleScheme *self,
                               const gchar *filename)
{
  g_return_if_fail (CDK_IS_STYLE_SCHEME (self));
  g_return_if_fail (filename != NULL);

  if (g_strcmp0 (filename, self->priv->filename) != 0)
    {
      g_free (self->priv->filename);
      self->priv->filename = g_strdup (filename);
      cdk_style_scheme_reload (self);
      g_object_notify (G_OBJECT (self), "filename");
    }
}

const gchar *
cdk_style_scheme_get_name (CdkStyleScheme *self)
{
  g_return_val_if_fail (CDK_IS_STYLE_SCHEME (self), NULL);
  return self->priv->name;
}

void
cdk_style_scheme_set_name (CdkStyleScheme *self, const gchar *name)
{
  g_return_if_fail (CDK_IS_STYLE_SCHEME (self));
  if (g_strcmp0 (name, self->priv->name) != 0)
    {
      g_free (self->priv->name);
      self->priv->name = g_strdup (name);
      g_object_notify (G_OBJECT (self), "name");
    }
}

static gchar *
lookup_attr (const gchar  *name,
             const gchar **attr_names,
             const gchar **attr_values)
{
  if (name == NULL || attr_names == NULL || attr_values == NULL)
    return NULL;
  guint i = 0;
  for (const gchar **attr = attr_names; *attr != NULL; i++, attr++)
    {
      if (g_strcmp0 (*attr, name) == 0)
        return g_strdup (attr_values[i]);
    }
  return NULL;
}

static guint32
parse_color (const gchar *cstr)
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

static void
on_start_element (G_GNUC_UNUSED GMarkupParseContext *context,
                  const gchar                       *el_name,
                  const gchar                      **attr_names,
                  const gchar                      **attr_values,
                  gpointer                           user_data,
                  G_GNUC_UNUSED GError             **error)
{
  CdkStyleScheme *self = CDK_STYLE_SCHEME (user_data);

  // Read the <scheme> tag
  if (g_strcmp0 (el_name, "scheme") == 0)
    {
      gchar *name = lookup_attr ("name", attr_names, attr_values);
      cdk_style_scheme_set_name (self, name ? name : "Untitled");
      //g_debug ("found <scheme> tag: %s", name);
      g_free (name);
      self->priv->in_scheme_tag = TRUE;
      return;
    }
  // Read the <style> tags
  else if (self->priv->in_scheme_tag &&
           g_strcmp0 (el_name, "style") == 0)
    {
      gchar *style_name = lookup_attr ("name", attr_names, attr_values);
      if (style_name == NULL)
        {
          self->priv->style_id = -1;
          g_warning ("encountered <style> tag without a name, skipping");
          return;
        }
      else
        {
          self->priv->style_id = style_id_from_name (style_name);
          if (self->priv->style_id < 0)
            {
              g_warning ("unknown style name '%s'", style_name);
              g_free (style_name);
              return;
            }

          //g_debug ("found <style> tag: %s", style_name);
          //g_free (style_name);
        }

      // hardcoded fallbacks
      self->priv->fore_color = 0;
      self->priv->back_color = 0x00ffffff;
      self->priv->bold       = FALSE;
      self->priv->italic     = FALSE;
      self->priv->font       = NULL;
      self->priv->size       = -1;

      gchar *fore_str = lookup_attr ("fore", attr_names, attr_values);
      if (fore_str != NULL)
        self->priv->fore_color = parse_color (fore_str);
      g_free (fore_str);

      gchar *back_str = lookup_attr ("back", attr_names, attr_values);
      if (back_str != NULL)
        self->priv->back_color = parse_color (back_str);
      g_free (back_str);

      gchar *bold_str = lookup_attr ("bold", attr_names, attr_values);
      if (bold_str != NULL)
        self->priv->bold = (g_strcmp0 (bold_str, "true") == 0);
      g_free (bold_str);

      gchar *italic_str = lookup_attr ("italic", attr_names, attr_values);
      if (italic_str != NULL)
        self->priv->italic = (g_strcmp0 (italic_str, "true") == 0);
      g_free (italic_str);

      self->priv->font = lookup_attr ("font", attr_names, attr_values);

      gchar *size_str = lookup_attr ("size", attr_names, attr_values);
      if (size_str != NULL)
        {
          errno = 0;
          glong size = strtol (size_str, NULL, 10);
          if (errno == 0)
            self->priv->size = size;
          g_free (size_str);
        }

      //g_debug ("style(%s | %u); fore=0x%06x, back=0x%06x, bold=%d, italic=%d",
      //         style_name, (guint) self->priv->style_id, self->priv->fore_color, self->priv->back_color, self->priv->bold, self->priv->italic);
      g_free (style_name);
    }
}

static void
on_end_element (G_GNUC_UNUSED GMarkupParseContext *context,
                const gchar                       *el_name,
                gpointer                           user_data,
                G_GNUC_UNUSED GError             **error)
{
  CdkStyleScheme *self = CDK_STYLE_SCHEME (user_data);

  if (g_strcmp0 (el_name, "scheme") == 0)
    {
      self->priv->in_scheme_tag = FALSE;
      return;
    }
  else if (self->priv->in_scheme_tag &&
           self->priv->style_id >= 0 &&
           g_strcmp0 (el_name, "style") == 0)
    {
      CdkStyle *style = cdk_style_new ();
      style->fore = self->priv->fore_color;
      style->back = self->priv->back_color;
      style->bold = self->priv->bold;
      style->italic = self->priv->italic;
      style->font = self->priv->font;
      style->size = self->priv->size;

      g_hash_table_insert (self->priv->style_map,
                           GSIZE_TO_POINTER (self->priv->style_id),
                           style);
      //g_debug ("added style '%u'", (guint) self->priv->style_id);
      self->priv->style_id = -1;
    }
}

gboolean
cdk_style_scheme_reload (CdkStyleScheme *self)
{
  g_return_val_if_fail (CDK_IS_STYLE_SCHEME (self), FALSE);

  g_hash_table_remove_all (self->priv->style_map);

  gchar *contents = NULL;
  gsize length = 0;
  GError *error = NULL;
  if (! g_file_get_contents (self->priv->filename, &contents, &length, &error))
    {
      g_warning ("failed to read style scheme file '%s': %s",
                 self->priv->filename, error->message);
      g_error_free (error);
      return FALSE;
    }

  GMarkupParser parser;
  memset (&parser, 0, sizeof (GMarkupParser));
  parser.start_element = on_start_element;
  parser.end_element = on_end_element;

  GMarkupParseContext *ctx =
    g_markup_parse_context_new (&parser, 0, self, NULL);
  error = NULL;
  if (! g_markup_parse_context_parse (ctx, contents, length, &error))
    {
      g_warning ("error parsing XML style scheme file '%s': %s",
                 self->priv->filename, error->message);
      g_error_free (error);
      g_free (contents);
      g_markup_parse_context_free (ctx);
      return FALSE;
    }

  g_free (contents);
  g_markup_parse_context_free (ctx);

  //g_debug ("loaded style scheme from XML file '%s'", self->priv->filename);
  g_signal_emit_by_name (self, "reloaded");

  return TRUE;
}

static inline void
add_map (const gchar *name, CdkStyleID id)
{
  g_hash_table_insert (style_name_map, g_strdup (name), GSIZE_TO_POINTER (id));
}

static inline void
add_token (guint token_kind, CdkStyleID id)
{
  g_hash_table_insert (token_map, GSIZE_TO_POINTER (token_kind), GSIZE_TO_POINTER (id));
}

static inline void
add_cursor (guint cursor_kind, CdkStyleID id)
{
  g_hash_table_insert (cursor_map, GSIZE_TO_POINTER (cursor_kind), GSIZE_TO_POINTER (id));
}

void
cdk_style_schemes_init (void)
{
  if (style_name_map != NULL)
    return;

  style_name_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  token_map = g_hash_table_new (g_direct_hash, g_direct_equal);
  cursor_map = g_hash_table_new (g_direct_hash, g_direct_equal);

  // Map style scheme style names to CdkStyleIDs
  add_map ("default", CDK_STYLE_DEFAULT);
  add_map ("comment", CDK_STYLE_COMMENT);
  add_map ("member_ref", CDK_STYLE_MEMBER_REF);
  add_map ("identifier", CDK_STYLE_IDENTIFIER);
  add_map ("keyword", CDK_STYLE_KEYWORD);
  add_map ("literal", CDK_STYLE_LITERAL);
  add_map ("number", CDK_STYLE_NUMBER);
  add_map ("preprocessor", CDK_STYLE_PREPROCESSOR);
  add_map ("punctuation", CDK_STYLE_PUNCTUATION);
  add_map ("string", CDK_STYLE_STRING);
  add_map ("type_name", CDK_STYLE_TYPE_NAME);
  add_map ("function_call", CDK_STYLE_FUNCTION_CALL);
  add_map ("character", CDK_STYLE_CHARACTER);
  add_map ("diagnostic_warning", CDK_STYLE_DIAGNOSTIC_WARNING);
  add_map ("diagnostic_error", CDK_STYLE_DIAGNOSTIC_ERROR);
  add_map ("annotation_warning", CDK_STYLE_ANNOTATION_WARNING);
  add_map ("annotation_error", CDK_STYLE_ANNOTATION_ERROR);

  // Map libclang CXTokenKinds to CdkStyleIDs
  add_token (CXToken_Punctuation, CDK_STYLE_PUNCTUATION);
  add_token (CXToken_Keyword, CDK_STYLE_KEYWORD);
  add_token (CXToken_Identifier, CDK_STYLE_IDENTIFIER);
  add_token (CXToken_Literal, CDK_STYLE_LITERAL);
  add_token (CXToken_Comment, CDK_STYLE_COMMENT);

  // Map libclang CXCursorKinds (more specific) to CdkStyleIDs
  add_cursor (CXCursor_TypeRef, CDK_STYLE_TYPE_NAME);
  add_cursor (CXCursor_MemberRef, CDK_STYLE_MEMBER_REF);
  add_cursor (CXCursor_MemberRefExpr, CDK_STYLE_MEMBER_REF);
  add_cursor (CXCursor_CallExpr, CDK_STYLE_FUNCTION_CALL);
  add_cursor (CXCursor_StringLiteral, CDK_STYLE_STRING);
  add_cursor (CXCursor_CharacterLiteral, CDK_STYLE_CHARACTER);
  add_cursor (CXCursor_IntegerLiteral, CDK_STYLE_NUMBER);
  add_cursor (CXCursor_FloatingLiteral, CDK_STYLE_NUMBER);
  add_cursor (CXCursor_ImaginaryLiteral, CDK_STYLE_NUMBER);
  // ...
}

void
cdk_style_schemes_cleanup (void)
{
  if (style_name_map == NULL)
    return;

  g_hash_table_destroy (style_name_map);
  g_hash_table_destroy (token_map);
  g_hash_table_destroy (cursor_map);
}

CdkStyleID
cdk_style_id_for_token_kind (guint token_kind)
{
  if (G_UNLIKELY (token_map == NULL))
    return CDK_STYLE_DEFAULT;
  return GPOINTER_TO_SIZE (g_hash_table_lookup (token_map, GSIZE_TO_POINTER (token_kind)));
}

CdkStyleID
cdk_style_id_for_cursor_kind (guint cursor_kind)
{
  if (G_UNLIKELY (cursor_map == NULL))
    return CDK_STYLE_DEFAULT;
  return GPOINTER_TO_SIZE (g_hash_table_lookup (cursor_map, GSIZE_TO_POINTER (cursor_kind)));
}

gboolean
cdk_style_id_is_for_syntax (CdkStyleID id)
{
  switch (id)
    {
    case CDK_STYLE_DIAGNOSTIC_ERROR:
    case CDK_STYLE_DIAGNOSTIC_WARNING:
    case CDK_STYLE_ANNOTATION_ERROR:
    case CDK_STYLE_ANNOTATION_WARNING:
      return FALSE;
    default:
      return TRUE;
    }
}
