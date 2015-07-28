/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkhighlighter.h>
#include <cdk/cdkstyle.h>
#include <cdk/cdk.h>
#include <geanyplugin.h>
#include <SciLexer.h>
#include <clang-c/Index.h>
#include <ctype.h>

#define CDK_HIGHLIGHTER_TIMEOUT 500
#define CDK_HL_OCCUR_INDIC INDIC_CONTAINER+10

struct CdkHighlighterPrivate_
{
  gulong editor_notif_hnd;  // editor-notify (sci-notify) handler
  gulong update_hnd;        // update handler
  gint timeout;             // timeout for update handler
  gint start_pos, end_pos;  // start/end of update range
  CdkStyleScheme *scheme;   // the style scheme being applied
  glong prev_lexer;         // the lexer the document had previously
  gboolean hl_occur;        // whether to highlight occurrences of symbol
};

enum
{
  SIG_HIGHLIGHTED,
  NUM_SIGNALS,
};

enum
{
  PROP_0,
  PROP_SCHEME,
  PROP_HL_OCCUR,
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
static void cdk_highlighter_highlight_occurrences (CdkHighlighter *self);

G_DEFINE_TYPE (CdkHighlighter, cdk_highlighter, CDK_TYPE_DOCUMENT_HELPER)

static void
cdk_highlighter_constructed (GObject *object)
{
  G_OBJECT_CLASS (cdk_highlighter_parent_class)->constructed (object);
  // bind the plugin's style-scheme property to this highlighter's
  CdkPlugin *plugin = cdk_document_helper_get_plugin (CDK_DOCUMENT_HELPER (object));
  g_return_if_fail (CDK_IS_PLUGIN (plugin));
  g_object_bind_property (plugin, "style-scheme", object, "style-scheme", G_BINDING_SYNC_CREATE);
}

static void
cdk_highlighter_initialize_document (CdkDocumentHelper *object,
                                     GeanyDocument     *document)
{
  CdkHighlighter *self = CDK_HIGHLIGHTER (object);
  ScintillaObject *sci = document->editor->sci;

  // disable the built-in lexer
  self->priv->prev_lexer = cdk_sci_send (sci, SCI_GETLEXER, 0, 0);
  cdk_sci_send (sci, SCI_SETLEXER, SCLEX_CONTAINER, 0);

  // setup the indicator used for highlight occurrences
  cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_HL_OCCUR_INDIC, INDIC_ROUNDBOX);
  cdk_sci_send (sci, SCI_INDICSETFORE, CDK_HL_OCCUR_INDIC, 0);
  cdk_sci_send (sci, SCI_INDICSETALPHA, CDK_HL_OCCUR_INDIC, 32);
  cdk_sci_send (sci, SCI_INDICSETOUTLINEALPHA, CDK_HL_OCCUR_INDIC, 64);

  self->priv->editor_notif_hnd =
    g_signal_connect (sci, "sci-notify", G_CALLBACK (cdk_highlighter_editor_notify), self);
}

static void
cdk_highlighter_deinitialize_document (CdkHighlighter *self,
                                       GeanyDocument  *document)
{
  ScintillaObject *sci = document->editor->sci;
  if (self->priv->editor_notif_hnd > 0)
    g_signal_handler_disconnect (document->editor->sci, self->priv->editor_notif_hnd);

  // Reset the styles to some sane default, Geany will re-highlight eventually
  cdk_sci_send (sci, SCI_STYLESETFORE, STYLE_DEFAULT, 0);
  cdk_sci_send (sci, SCI_STYLESETBACK, STYLE_DEFAULT, 0xffffff);
  cdk_sci_send (sci, SCI_STYLESETBOLD, STYLE_DEFAULT, FALSE);
  cdk_sci_send (sci, SCI_STYLESETITALIC, STYLE_DEFAULT, FALSE);
  cdk_sci_send (sci, SCI_STYLECLEARALL, 0, 0);

  // Restore the previous lexer
  cdk_sci_send (sci, SCI_SETLEXER, self->priv->prev_lexer, 0);
}

static void
cdk_highlighter_class_init (CdkHighlighterClass *klass)
{
  GObjectClass *g_object_class;
  CdkDocumentHelperClass *dh_object_class;

  g_object_class = G_OBJECT_CLASS (klass);
  dh_object_class = CDK_DOCUMENT_HELPER_CLASS (klass);

  dh_object_class->initialize = cdk_highlighter_initialize_document;

  g_object_class->constructed = cdk_highlighter_constructed;
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

  cdk_highlighter_properties[PROP_SCHEME] =
    g_param_spec_object ("style-scheme",
                         "StyleScheme",
                         "The style scheme to use while highlighting",
                         CDK_TYPE_STYLE_SCHEME,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_highlighter_properties[PROP_HL_OCCUR] =
    g_param_spec_boolean ("highlight-occurrences",
                          "HighlightOccurrences",
                          "Whether to highlight occurrences of symbols",
                          TRUE,
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

  cdk_highlighter_deinitialize_document (self,
    cdk_document_helper_get_document (CDK_DOCUMENT_HELPER (self)));

  if (self->priv->update_hnd > 0)
    g_source_remove (self->priv->update_hnd);

  if (G_IS_OBJECT (self->priv->scheme))
    g_object_unref (self->priv->scheme);

  G_OBJECT_CLASS (cdk_highlighter_parent_class)->finalize (object);
}

static void
cdk_highlighter_init (CdkHighlighter *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_HIGHLIGHTER, CdkHighlighterPrivate);
  self->priv->timeout = CDK_HIGHLIGHTER_TIMEOUT;
  self->priv->hl_occur = TRUE;
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
    case PROP_SCHEME:
      g_value_set_object (value, cdk_highlighter_get_style_scheme (self));
      break;
    case PROP_HL_OCCUR:
      g_value_set_boolean (value, cdk_highlighter_get_highlight_occurrences (self));
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
    case PROP_SCHEME:
      cdk_highlighter_set_style_scheme (self, g_value_get_object (value));
      break;
    case PROP_HL_OCCUR:
      cdk_highlighter_set_highlight_occurrences (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CdkHighlighter *
cdk_highlighter_new (struct CdkPlugin_ *plugin, struct GeanyDocument *doc)
{
  return g_object_new (CDK_TYPE_HIGHLIGHTER, "plugin", plugin, "document", doc, NULL);
}

static void
cdk_highlighter_apply_style (G_GNUC_UNUSED CdkHighlighter *self,
                             GeanyDocument *doc,
                             CdkStyleID style_id,
                             guint start_pos,
                             guint end_pos)
{
  ScintillaObject *sci = doc->editor->sci;

  g_assert (cdk_sci_send (sci, SCI_GETLEXER, 0, 0) == SCLEX_CONTAINER);

  cdk_sci_send (sci, SCI_STARTSTYLING, start_pos, 0);
  cdk_sci_send (sci, SCI_SETSTYLING, end_pos - start_pos, style_id);

  //g_debug ("styled token '%u' from '%u' to '%u'", (guint) style_id, start_pos, end_pos);
}

static gboolean
cdk_highlighter_editor_notify (GtkWidget *widget,
                               G_GNUC_UNUSED gint unused,
                               SCNotification *nt,
                               CdkHighlighter *self)
{
  ScintillaObject *sci = SCINTILLA (widget);

  if (nt->nmhdr.code == SCN_UPDATEUI)
    {
      cdk_highlighter_highlight_occurrences (self);
      return FALSE;
    }
  else if (nt->nmhdr.code != SCN_STYLENEEDED)
    {
      guint start_pos = cdk_sci_send (sci, SCI_GETENDSTYLED, 0, 0);
      guint line_num = cdk_sci_send (sci, SCI_LINEFROMPOSITION, start_pos, 0);
      start_pos = cdk_sci_send (sci, SCI_POSITIONFROMLINE, line_num, 0);

      cdk_highlighter_queue_highlight (self, start_pos, nt->position);

      return TRUE;
    }
  else
    return FALSE;
}

static void
cdk_highlighter_clear_occurrences (G_GNUC_UNUSED CdkHighlighter *self,
                                   ScintillaObject *sci)
{
  cdk_sci_send (sci, SCI_SETINDICATORCURRENT, CDK_HL_OCCUR_INDIC, 0);
  cdk_sci_send (sci, SCI_INDICATORCLEARRANGE, 0,
                cdk_sci_send (sci, SCI_GETLENGTH, 0, 0));
}

static CXCursor
cdk_highlighter_get_cursor_at_pos (G_GNUC_UNUSED CdkHighlighter *self,
                                   CXTranslationUnit tu,
                                   GeanyDocument *doc,
                                   gint offset)
{
  CXFile file = clang_getFile (tu, doc->real_path);
  CXSourceLocation locn = clang_getLocationForOffset (tu, file, offset);
  return clang_getCursor (tu, locn);
}

static void
cdk_highlighter_highlight_occurrences (CdkHighlighter *self)
{
  CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
  GeanyDocument *doc = cdk_document_helper_get_document (helper);
  ScintillaObject *sci = doc->editor->sci;
  CdkPlugin *plugin = cdk_document_helper_get_plugin (helper);
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (plugin, doc);

  cdk_highlighter_clear_occurrences (self, sci);

  gchar *cur_word = cdk_sci_get_current_word (sci);
  if (! cur_word || ! *cur_word || (! isalpha (*cur_word) && *cur_word != '_'))
    { // no current identifier, do nothing
      g_free (cur_word);
      return;
    }

  gint word_len = strlen (cur_word);
  gint first_line = cdk_sci_send (sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
  gint num_lines = cdk_sci_send (sci, SCI_LINESONSCREEN, 0, 0);
  gint last_line = first_line + num_lines;
  gint start_pos = cdk_sci_send (sci, SCI_POSITIONFROMLINE, first_line, 0);
  gint end_pos = cdk_sci_send (sci, SCI_GETLINEENDPOSITION, last_line, 0);

  gint current_pos = cdk_sci_send (sci, SCI_GETCURRENTPOS, 0, 0);
  CXCursor cur_cursor = cdk_highlighter_get_cursor_at_pos (self, tu, doc, current_pos);
  cur_cursor = clang_getCursorReferenced (cur_cursor);

  cdk_sci_send (sci, SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD, 0);

  gint start = start_pos;
  gint n_matches = 0;
  while (TRUE)
    {
      cdk_sci_send (sci, SCI_SETTARGETRANGE, start, end_pos);
      gint result = cdk_sci_send (sci, SCI_SEARCHINTARGET, word_len, cur_word);
      if (result < 0)
        break;

      gint found_start = cdk_sci_send (sci, SCI_GETTARGETSTART, 0, 0);
      gint found_end = cdk_sci_send (sci, SCI_GETTARGETEND, 0, 0);

      CXCursor test_cursor = cdk_highlighter_get_cursor_at_pos (self, tu, doc, found_start);
      test_cursor = clang_getCursorReferenced (test_cursor);
      if (clang_equalCursors (cur_cursor, test_cursor))
        {
          cdk_sci_send (sci, SCI_INDICATORFILLRANGE, found_start, found_end - found_start);
          n_matches++;
        }
      start = found_end;
    }

  if (n_matches <= 1)
    cdk_highlighter_clear_occurrences (self, sci);

  g_free (cur_word);
}

gboolean
cdk_highlighter_highlight (CdkHighlighter *self,
                           gint start_pos,
                           gint end_pos)
{
  g_return_val_if_fail (CDK_IS_HIGHLIGHTER (self), FALSE);

  CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
  GeanyDocument *doc = cdk_document_helper_get_document (helper);
  CdkPlugin *plugin = cdk_document_helper_get_plugin (helper);
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (plugin, doc);
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
      guint start_p = 0, end_p = 0;
      clang_getSpellingLocation (start_loc, NULL, NULL, NULL, &start_p);
      clang_getSpellingLocation (end_loc, NULL, NULL, NULL, &end_p);

      cdk_highlighter_apply_style (self, doc, style_id, start_p, end_p);
    }

  g_free (cursors);
  clang_disposeTokens (tu, tokens, n_tokens);

  g_signal_emit_by_name (self, "highlighted", doc);

  //g_debug ("highlighted %u tokens", n_tokens);

  return TRUE;
}

gboolean
cdk_highlighter_highlight_all (CdkHighlighter *self)
{
  GeanyDocument *doc = cdk_document_helper_get_document (CDK_DOCUMENT_HELPER (self));
  ScintillaObject *sci = doc->editor->sci;
  gint length = cdk_sci_send (sci, SCI_GETLENGTH, 0, 0);
  return cdk_highlighter_highlight (self, 0, length);
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

void
cdk_highlighter_set_style_scheme (CdkHighlighter *self, CdkStyleScheme *scheme)
{
  g_return_if_fail (CDK_IS_HIGHLIGHTER (self));

  if (scheme != self->priv->scheme)
    {
      if (G_IS_OBJECT (self->priv->scheme))
        g_object_unref (self->priv->scheme);
      self->priv->scheme = NULL;

      if (CDK_IS_STYLE_SCHEME (scheme))
        {
          self->priv->scheme = g_object_ref (scheme);

          GeanyDocument *doc = cdk_document_helper_get_document (CDK_DOCUMENT_HELPER (self));
          ScintillaObject *sci = doc->editor->sci;

          // Apply the new scheme
          CdkStyle *def_style = cdk_style_scheme_get_style (self->priv->scheme, CDK_STYLE_DEFAULT);
          if (def_style != NULL)
            {
              // apply default style to all styles first
              cdk_sci_send (sci, SCI_STYLESETFORE, STYLE_DEFAULT, def_style->fore);
              cdk_sci_send (sci, SCI_STYLESETBACK, STYLE_DEFAULT, def_style->back);
              cdk_sci_send (sci, SCI_STYLESETBOLD, STYLE_DEFAULT, def_style->bold);
              cdk_sci_send (sci, SCI_STYLESETITALIC, STYLE_DEFAULT, def_style->italic);
            }
          else
            {
              // sane fallback for all styles
              cdk_sci_send (sci, SCI_STYLESETFORE, STYLE_DEFAULT, 0x000000);
              cdk_sci_send (sci, SCI_STYLESETBACK, STYLE_DEFAULT, 0xffffff);
              cdk_sci_send (sci, SCI_STYLESETBOLD, STYLE_DEFAULT, FALSE);
              cdk_sci_send (sci, SCI_STYLESETITALIC, STYLE_DEFAULT, FALSE);
            }
          cdk_sci_send (sci, SCI_STYLECLEARALL, 0, 0);

          // set the styles used by the highlighter
          for (gint i = 0; i < CDK_NUM_STYLES; i++)
            {
              if (! cdk_style_id_is_for_syntax (i))
                continue;

              CdkStyle *style = cdk_style_scheme_get_style (self->priv->scheme, i);

              if (style == NULL)
                continue;

              cdk_sci_send (sci, SCI_STYLESETFORE, i, style->fore);
              cdk_sci_send (sci, SCI_STYLESETBACK, i, style->back);
              cdk_sci_send (sci, SCI_STYLESETBOLD, i, style->bold);
              cdk_sci_send (sci, SCI_STYLESETITALIC, i, style->italic);
            }

          cdk_highlighter_highlight_all (self);
        }

      g_object_notify (G_OBJECT (self), "style-scheme");
    }
}

gboolean
cdk_highlighter_get_highlight_occurrences (CdkHighlighter *self)
{
  g_return_val_if_fail (CDK_IS_HIGHLIGHTER (self), FALSE);
  return self->priv->hl_occur;
}

void
cdk_highlighter_set_highlight_occurrences (CdkHighlighter *self,
                                           gboolean hl_occur)
{
  g_return_if_fail (CDK_IS_HIGHLIGHTER (self));

  if (hl_occur != self->priv->hl_occur)
    {
      self->priv->hl_occur = hl_occur;
      g_object_notify (G_OBJECT (self), "highlight-occurrences");
    }
}
