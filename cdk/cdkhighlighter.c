/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#include <cdk/cdkhighlighter.h>
#include <cdk/cdkstyle.h>
#include <cdk/cdk.h>
#include <geanyplugin.h>
#include <SciLexer.h>
#include <clang-c/Index.h>

#define SSM(sci, msg, uptr, sptr) \
  scintilla_send_message (SCINTILLA (sci), (guint)(msg), (uptr_t)(uptr), (sptr_t)(sptr))

struct CdkHighlighterPrivate_
{
  CdkPlugin *plugin;        // the plugin that owns this
  GeanyDocument *doc;       // the GeanyDocument being highlighted
  gulong editor_notif_hnd;  // editor-notify (sci-notify) handler
  gulong update_hnd;        // update handler
  gint timeout;             // timeout for update handler
  gint start_pos, end_pos;  // start/end of update range
  CdkStyleScheme *scheme;   // the style scheme being applied
  gulong scheme_reload_hnd; // handler to update h/l when scheme is reloaded
};

enum
{
  SIG_HIGHLIGHTED,
  NUM_SIGNALS,
};

enum
{
  PROP_0,
  PROP_PLUGIN,
  PROP_DOCUMENT,
  PROP_SCHEME,
  NUM_PROPERTIES,
};

static gulong cdk_highlighter_signals[NUM_SIGNALS] = { 0 };
static GParamSpec *cdk_highlighter_properties[NUM_PROPERTIES] = { NULL };

static void cdk_highlighter_finalize (GObject *object);
static void cdk_highlighter_get_property (GObject *object, guint prop_id,
                                          GValue *value, GParamSpec *pspec);
static void cdk_highlighter_set_property (GObject *object, guint prop_id,
                                          const GValue *value, GParamSpec *pspec);
static gboolean cdk_highlighter_editor_notify (GtkWidget *widget,
                                               gint unused,
                                               SCNotification *nt,
                                               CdkHighlighter *self);

G_DEFINE_TYPE (CdkHighlighter, cdk_highlighter, G_TYPE_OBJECT)

static void
cdk_highlighter_class_init (CdkHighlighterClass *klass)
{
  GObjectClass *g_object_class;

  g_object_class = G_OBJECT_CLASS (klass);

  g_object_class->finalize = cdk_highlighter_finalize;
  g_object_class->get_property = cdk_highlighter_get_property;
  g_object_class->set_property = cdk_highlighter_set_property;

  cdk_highlighter_signals[SIG_HIGHLIGHTED] =
    g_signal_new ("highlighted",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  cdk_highlighter_properties[PROP_PLUGIN] =
    g_param_spec_object ("plugin",
                         "Plugin",
                         "The plugin that owns this highlighter",
                         CDK_TYPE_PLUGIN,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_highlighter_properties[PROP_DOCUMENT] =
    g_param_spec_pointer ("document",
                          "Document",
                          "The document this highlighter highlights",
                          G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_highlighter_properties[PROP_SCHEME] =
    g_param_spec_object ("style-scheme",
                         "StyleScheme",
                         "The style scheme to use while highlighting",
                         CDK_TYPE_STYLE_SCHEME,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  g_object_class_install_properties (g_object_class, NUM_PROPERTIES,
                                     cdk_highlighter_properties);

  g_type_class_add_private ((gpointer)klass, sizeof (CdkHighlighterPrivate));
}

static void
cdk_highlighter_finalize (GObject *object)
{
  CdkHighlighter *self;

  g_return_if_fail (CDK_IS_HIGHLIGHTER (object));

  self = CDK_HIGHLIGHTER (object);

  if (DOC_VALID (self->priv->doc) && self->priv->editor_notif_hnd > 0)
    g_signal_handler_disconnect (self->priv->doc->editor->sci, self->priv->editor_notif_hnd);

  if (self->priv->update_hnd > 0)
    g_source_remove (self->priv->update_hnd);

  if (G_IS_OBJECT (self->priv->scheme))
    {
      if (self->priv->scheme_reload_hnd > 0)
        g_signal_handler_disconnect (self->priv->scheme, self->priv->scheme_reload_hnd);
      g_object_unref (self->priv->scheme);
    }

  G_OBJECT_CLASS (cdk_highlighter_parent_class)->finalize (object);
}

static void
cdk_highlighter_init (CdkHighlighter *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_HIGHLIGHTER, CdkHighlighterPrivate);
  self->priv->plugin = NULL;
  self->priv->doc = NULL;
  self->priv->update_hnd = 0;
  self->priv->timeout = 500;
  self->priv->scheme = NULL;
}

static void
cdk_highlighter_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  CdkHighlighter *self = CDK_HIGHLIGHTER (object);

  switch (prop_id)
    {
    case PROP_PLUGIN:
      g_value_set_object (value, self->priv->plugin);
      break;
    case PROP_DOCUMENT:
      g_value_set_pointer (value, self->priv->doc);
      break;
    case PROP_SCHEME:
      g_value_set_object (value, cdk_highlighter_get_style_scheme (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cdk_highlighter_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  CdkHighlighter *self = CDK_HIGHLIGHTER (object);

  switch (prop_id)
    {
    case PROP_PLUGIN:
      cdk_highlighter_set_plugin (self, g_value_get_object (value));
      break;
    case PROP_DOCUMENT:
      cdk_highlighter_set_document (self, g_value_get_pointer (value));
      break;
    case PROP_SCHEME:
      cdk_highlighter_set_style_scheme (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CdkHighlighter *
cdk_highlighter_new (CdkPlugin *plugin, struct GeanyDocument *doc)
{
  return g_object_new (CDK_TYPE_HIGHLIGHTER,
                       "plugin", plugin,
                       "document", doc, NULL);
}

CdkPlugin *
cdk_highlighter_get_plugin (CdkHighlighter *self)
{
  g_return_val_if_fail (CDK_IS_HIGHLIGHTER (self), NULL);
  return self->priv->plugin;
}

void
cdk_highlighter_set_plugin (CdkHighlighter *self, CdkPlugin *plugin)
{
  g_return_if_fail (CDK_IS_HIGHLIGHTER (self));
  g_return_if_fail (CDK_IS_PLUGIN (plugin));
  self->priv->plugin = plugin;
  if (CDK_IS_PLUGIN (self->priv->plugin))
    cdk_highlighter_set_style_scheme (self, cdk_plugin_get_style_scheme (plugin));
  g_object_notify (G_OBJECT (self), "plugin");
}

struct GeanyDocument *
cdk_highlighter_get_document (CdkHighlighter *self)
{
  g_return_val_if_fail (CDK_IS_HIGHLIGHTER (self), NULL);
  return self->priv->doc;
}

void
cdk_highlighter_set_document (CdkHighlighter *self, struct GeanyDocument *doc)
{
  g_return_if_fail (CDK_IS_HIGHLIGHTER (self));

  if (DOC_VALID (self->priv->doc))
    {
      if (self->priv->editor_notif_hnd > 0)
        g_signal_handler_disconnect (self->priv->doc->editor->sci, self->priv->editor_notif_hnd);
      self->priv->editor_notif_hnd = 0;
    }

  // TODO: save the previous lexer and restore it when removing document
  self->priv->doc = doc;
  if (DOC_VALID (self->priv->doc))
    {
      ScintillaObject *sci = self->priv->doc->editor->sci;

      // disable the built-in lexer
      SSM (sci, SCI_SETLEXER, SCLEX_CONTAINER, 0);

      CdkStyle *def_style = cdk_style_scheme_get_style (self->priv->scheme, CDK_STYLE_DEFAULT);
      if (def_style != NULL)
        {
          // apply default style to all styles first
          SSM (sci, SCI_STYLESETFORE, STYLE_DEFAULT, def_style->fore);
          SSM (sci, SCI_STYLESETBACK, STYLE_DEFAULT, def_style->back);
          SSM (sci, SCI_STYLESETBOLD, STYLE_DEFAULT, def_style->bold);
          SSM (sci, SCI_STYLESETITALIC, STYLE_DEFAULT, def_style->italic);
        }
      else
        {
          // sane fallback for all styles
          SSM (sci, SCI_STYLESETFORE, STYLE_DEFAULT, 0x000000);
          SSM (sci, SCI_STYLESETBACK, STYLE_DEFAULT, 0xffffff);
          SSM (sci, SCI_STYLESETBOLD, STYLE_DEFAULT, FALSE);
          SSM (sci, SCI_STYLESETITALIC, STYLE_DEFAULT, FALSE);
        }
      SSM (sci, SCI_STYLECLEARALL, 0, 0);

      // set the styles used by the highlighter
      for (gint i = 0; i < CDK_NUM_STYLES; i++)
        {
          CdkStyle *style = cdk_style_scheme_get_style (self->priv->scheme, i);

          if (style == NULL)
            continue;

          SSM (sci, SCI_STYLESETFORE, i, style->fore);
          SSM (sci, SCI_STYLESETBACK, i, style->back);
          SSM (sci, SCI_STYLESETBOLD, i, style->bold);
          SSM (sci, SCI_STYLESETITALIC, i, style->italic);
        }
      //g_debug ("setup %u styles", (guint) CDK_NUM_STYLES);
      self->priv->editor_notif_hnd =
        g_signal_connect (sci, "sci-notify", G_CALLBACK (cdk_highlighter_editor_notify), self);
      cdk_highlighter_highlight (self, 0, SSM (sci, SCI_GETLENGTH, 0, 0));
    }
  g_object_notify (G_OBJECT (self), "document");
}

static void
cdk_highlighter_apply_style (CdkHighlighter *self,
                             GeanyDocument *doc,
                             CdkStyleID style_id,
                             guint start_pos,
                             guint end_pos)
{
  ScintillaObject *sci = doc->editor->sci;

  g_assert (SSM (sci, SCI_GETLEXER, 0, 0) == SCLEX_CONTAINER);

  SSM (sci, SCI_STARTSTYLING, start_pos, 0);
  SSM (sci, SCI_SETSTYLING, end_pos - start_pos, style_id);

  //g_debug ("styled token '%u' from '%u' to '%u'", (guint) style_id, start_pos, end_pos);
}

static gboolean
cdk_highlighter_editor_notify (GtkWidget *widget,
                               gint unused,
                               SCNotification *nt,
                               CdkHighlighter *self)
{
  ScintillaObject *sci = SCINTILLA (widget);
  if (nt->nmhdr.code != SCN_STYLENEEDED)
    return FALSE;

  guint start_pos = SSM (sci, SCI_GETENDSTYLED, 0, 0);
  guint line_num = SSM (sci, SCI_LINEFROMPOSITION, start_pos, 0);
  start_pos = SSM (sci, SCI_POSITIONFROMLINE, line_num, 0);

  cdk_highlighter_queue_highlight (self, start_pos, nt->position);

  return TRUE;
}

gboolean
cdk_highlighter_highlight (CdkHighlighter *self,
                           guint start_pos,
                           guint end_pos)
{
  g_return_val_if_fail (CDK_IS_HIGHLIGHTER (self), FALSE);

  GeanyDocument *doc = self->priv->doc;
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (self->priv->plugin, doc);
  if (tu == NULL)
    return FALSE;

  CXToken *tokens = NULL;
  guint n_tokens = 0;
  CXFile file = clang_getFile (tu, doc->real_path);
  CXSourceLocation start_loc = clang_getLocationForOffset (tu, file, start_pos);
  CXSourceLocation end_loc = clang_getLocationForOffset (tu, file, end_pos);
  CXSourceRange range = clang_getRange (start_loc, end_loc);

  clang_tokenize (tu, range, &tokens, &n_tokens);

  CXCursor *cursors = g_malloc0 (n_tokens * sizeof (CXCursor));
  clang_annotateTokens (tu, tokens, n_tokens, cursors);

  for (guint i = 0; i < n_tokens; i++)
    {
      CXCursor cur = cursors[i];
      enum CXCursorKind kind = clang_getCursorKind (cur);
      CdkStyleID style_id = cdk_style_id_for_cursor_kind (kind);
      if (style_id == 0)
        {
          CXTokenKind tkind = clang_getTokenKind (tokens[i]);
          style_id = cdk_style_id_for_token_kind (tkind);
        }

      CXSourceRange range = clang_getTokenExtent (tu, tokens[i]);
      CXSourceLocation start_loc = clang_getRangeStart (range);
      CXSourceLocation end_loc = clang_getRangeEnd (range);
      guint start_pos = 0, end_pos = 0;
      clang_getSpellingLocation (start_loc, NULL, NULL, NULL, &start_pos);
      clang_getSpellingLocation (end_loc, NULL, NULL, NULL, &end_pos);

      cdk_highlighter_apply_style (self, doc, style_id, start_pos, end_pos);
    }

  g_free (cursors);
  clang_disposeTokens (tu, tokens, n_tokens);

  g_signal_emit_by_name (self, "highlighted", doc);

  //g_debug ("highlighted %u tokens", n_tokens);

  return TRUE;
}

static gboolean
on_highlight_later (CdkHighlighter *self)
{
  cdk_highlighter_highlight (self,
                             self->priv->start_pos,
                             self->priv->end_pos);
  self->priv->update_hnd = 0;
  return FALSE;
}

void
cdk_highlighter_queue_highlight (CdkHighlighter *self,
                                 gint start_pos,
                                 gint end_pos)
{
  g_return_if_fail (CDK_IS_HIGHLIGHTER (self));

  // reset the range to highlight if no pending highlight
  if (self->priv->update_hnd == 0)
    {
      self->priv->start_pos = start_pos;
      self->priv->end_pos = end_pos;
    }
  // extend the range to highlight if needed
  else
    {
      if (start_pos < self->priv->start_pos)
        self->priv->start_pos = start_pos;
      if (end_pos > self->priv->end_pos)
        self->priv->end_pos = end_pos;
    }

  if (self->priv->update_hnd == 0)
    {
      self->priv->update_hnd =
        g_timeout_add (self->priv->timeout,
                       (GSourceFunc) on_highlight_later, self);
    }
}

CdkStyleScheme *
cdk_highlighter_get_style_scheme (CdkHighlighter *self)
{
  g_return_val_if_fail (CDK_IS_HIGHLIGHTER (self), NULL);
  return self->priv->scheme;
}

static void
on_scheme_reload (CdkStyleScheme *scheme, CdkHighlighter *self)
{
  if (DOC_VALID (self->priv->doc))
    {
      ScintillaObject *sci = self->priv->doc->editor->sci;
      gint end = SSM (sci, SCI_GETTEXTLENGTH, 0, 0);
      cdk_highlighter_highlight (self, 0, end);
    }
}

void
cdk_highlighter_set_style_scheme (CdkHighlighter *self, CdkStyleScheme *scheme)
{
  g_return_if_fail (CDK_IS_HIGHLIGHTER (self));

  if (scheme != self->priv->scheme)
    {
      if (G_IS_OBJECT (self->priv->scheme))
        {
          if (self->priv->scheme_reload_hnd > 0)
            g_signal_handler_disconnect (self->priv->scheme, self->priv->scheme_reload_hnd);
          self->priv->scheme_reload_hnd = 0;
          g_object_unref (self->priv->scheme);
          self->priv->scheme = NULL;
        }

      if (CDK_IS_STYLE_SCHEME (scheme))
        {
          self->priv->scheme = g_object_ref (scheme);
          self->priv->scheme_reload_hnd =
            g_signal_connect (self->priv->scheme, "reloaded",
                              G_CALLBACK (on_scheme_reload), self);
        }

      // apply the new style scheme
      on_scheme_reload (scheme, self);

      g_object_notify (G_OBJECT (self), "style-scheme");
  }
}
