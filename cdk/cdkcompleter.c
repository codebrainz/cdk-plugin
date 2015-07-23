/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#include <cdk/cdkcompleter.h>
#include <cdk/cdkutils.h>
#include <cdk/cdkplugin.h>
#include <geanyplugin.h>
#include <clang-c/Index.h>

#define PRIV_SCI(self) (self)->priv->doc->editor->sci
#define SSM(sci, msg, uptr, sptr) \
  scintilla_send_message (SCINTILLA (sci), (guint)(msg), (uptr_t)(uptr), (sptr_t)(sptr))

struct CdkCompleterPrivate_
{
  GeanyDocument *doc;
  gulong sci_handler;
  CdkPlugin *plugin;
};

enum
{
  PROP_0,
  PROP_DOCUMENT,
  PROP_PLUGIN,
  NUM_PROPERTIES,
};

static GParamSpec *cdk_completer_properties[NUM_PROPERTIES] = { NULL };

static void cdk_completer_finalize (GObject *object);
static void cdk_completer_get_property (GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);
static void cdk_completer_set_property (GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);

G_DEFINE_TYPE (CdkCompleter, cdk_completer, G_TYPE_OBJECT)

static void
cdk_completer_class_init (CdkCompleterClass *klass)
{
  GObjectClass *g_object_class;

  g_object_class = G_OBJECT_CLASS (klass);

  g_object_class->finalize = cdk_completer_finalize;
  g_object_class->get_property = cdk_completer_get_property;
  g_object_class->set_property = cdk_completer_set_property;

  cdk_completer_properties[PROP_DOCUMENT] =
    g_param_spec_pointer ("document",
                          "GeanyDocument",
                          "The Geany document using this completer",
                          G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_completer_properties[PROP_PLUGIN] =
    g_param_spec_object ("plugin",
                         "Plugin",
                         "The main plugin that owns this CdkCompleter",
                         CDK_TYPE_PLUGIN,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  g_object_class_install_properties (g_object_class,
                                     NUM_PROPERTIES,
                                     cdk_completer_properties);

  g_type_class_add_private ((gpointer)klass, sizeof (CdkCompleterPrivate));
}

static void
cdk_completer_finalize (GObject *object)
{
  CdkCompleter *self;

  g_return_if_fail (CDK_IS_COMPLETER (object));

  self = CDK_COMPLETER (object);

  if (DOC_VALID (self->priv->doc))
    {
      if (self->priv->sci_handler > 0)
        g_signal_handler_disconnect (PRIV_SCI (self), self->priv->sci_handler);
      g_object_remove_weak_pointer (G_OBJECT (PRIV_SCI (self)), (gpointer*) &self->priv->doc);
    }

  G_OBJECT_CLASS (cdk_completer_parent_class)->finalize (object);
}

static void
cdk_completer_init (CdkCompleter *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_COMPLETER, CdkCompleterPrivate);
  self->priv->doc = NULL;
  self->priv->plugin = NULL;
}

static void
cdk_completer_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  CdkCompleter *self = CDK_COMPLETER (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_pointer (value, cdk_completer_get_document (self));
      break;
    case PROP_PLUGIN:
      g_value_set_object (value, self->priv->plugin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cdk_completer_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  CdkCompleter *self = CDK_COMPLETER (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      cdk_completer_set_document (self, g_value_get_pointer (value));
      break;
    case PROP_PLUGIN:
      self->priv->plugin = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CdkCompleter *
cdk_completer_new (struct CdkPlugin_ *plugin, GeanyDocument *doc)
{
  return g_object_new (CDK_TYPE_COMPLETER,
                       "plugin", plugin,
                       "document", doc,
                       NULL);
}

struct GeanyDocument *
cdk_completer_get_document (CdkCompleter *self)
{
  g_return_val_if_fail (CDK_IS_COMPLETER (self), NULL);
  return self->priv->doc;
}

static void
cdk_completer_complete (CdkCompleter *self,
                        gint word_start,
                        gint current_pos,
                        const gchar *current_word)
{
  GeanyDocument *doc = self->priv->doc;
  if (! DOC_VALID (doc))
    return;
  ScintillaObject *sci = doc->editor->sci;
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (self->priv->plugin, doc);
  if (tu == NULL)
    return;

  struct CXUnsavedFile usf;
  struct CXUnsavedFile *usf_ptr = NULL;
  guint n_usf = 0;

  if (doc->changed)
    {
      usf.Filename = doc->real_path;
      usf.Contents = cdk_document_get_contents (doc);
      usf.Length = cdk_document_get_length (doc);
      usf_ptr = &usf;
      n_usf = 1;
    }

  gint len = (gint) current_pos - (gint) word_start;
  // libclang uses 1-based line, Scintilla uses 0-based line
  guint line = SSM (sci, SCI_LINEFROMPOSITION, current_pos, 0) + 1;
  guint col = SSM (sci, SCI_GETCOLUMN, current_pos, 0);

  CXCodeCompleteResults *comp_res =
    clang_codeCompleteAt (tu, doc->real_path, line, col, usf_ptr, n_usf,
                          clang_defaultCodeCompleteOptions ());

  GString *autoc_str = g_string_new ("");
  for (guint i = 0; i < comp_res->NumResults; i++)
    {
      CXCompletionResult *res = &comp_res->Results[i];
      CXCompletionString str = res->CompletionString;
      guint n_chunks = clang_getNumCompletionChunks (str);
      //g_print("pri=%u", clang_getCompletionPriority (str));
      for (guint j = 0; j < n_chunks; j++)
        {
          enum CXCompletionChunkKind kind = clang_getCompletionChunkKind (str, j);
          if (kind == CXCompletionChunk_TypedText)
            {
              CXString name = clang_getCompletionChunkText (str, j);
              const gchar *name_str = clang_getCString (name);
              if (len == 0 || g_str_has_prefix (name_str, current_word))
                {
                  g_string_append (autoc_str, name_str);
                  g_string_append_c (autoc_str, '\n');
                }
              clang_disposeString (name);
              break;
            }
        }
    }

  clang_disposeCodeCompleteResults (comp_res);

  gchar *compl_list = g_string_free (autoc_str, FALSE);
  g_strstrip (compl_list);
  if (strlen (compl_list) > 0)
    SSM (sci, SCI_AUTOCSHOW, current_pos - word_start, compl_list);
  g_free (compl_list);
}

static void
cdk_completer_handle_key (CdkCompleter *completer,
                          ScintillaObject *sci,
                          gint offset)
{
  gint prev_ch = '\0';
  gint word_start = SSM (sci, SCI_WORDSTARTPOSITION, offset, TRUE);
  gint chr = '\0';

  if (offset > 1)
    prev_ch = SSM (sci, SCI_GETCHARAT, offset - 2, 0);
  if (offset > 0)
    chr = SSM (sci, SCI_GETCHARAT, offset - 1, 0);

  // if dereferencing a struct/class member or at least 3 chars into
  // a word, then show the autocomplete
  if (chr == '.' || (prev_ch == '-' && chr == '>') || ((offset - word_start) > 2))
    {
      struct Sci_TextRange tr;
      tr.lpstrText = g_malloc0 (offset - word_start + 1);
      tr.chrg.cpMin = word_start;
      tr.chrg.cpMax = offset;
      SSM (sci, SCI_GETTEXTRANGE, 0, &tr);
      cdk_completer_complete (completer, word_start, offset, tr.lpstrText);
      g_free (tr.lpstrText);
    }
}

static void
cdk_completer_sci_notify (CdkCompleter *completer,
                          gint unused,
                          SCNotification *notif,
                          ScintillaObject *sci)
{
  GeanyDocument *doc = cdk_completer_get_document (completer);
  if (! DOC_VALID (doc))
    return;
  if (notif->nmhdr.code == SCN_CHARADDED)
    {
      gint offset = SSM (sci, SCI_GETCURRENTPOS, 0, 0);
      cdk_completer_handle_key (completer, sci, offset);
    }
}

void
cdk_completer_set_document (CdkCompleter *self, struct GeanyDocument *doc)
{
  g_return_if_fail (CDK_IS_COMPLETER (self));
  g_return_if_fail (doc == NULL || DOC_VALID (doc));

  if (doc == self->priv->doc)
    return;

  if (DOC_VALID (self->priv->doc))
    {
      if (self->priv->sci_handler > 0)
        g_signal_handler_disconnect (PRIV_SCI (self), self->priv->sci_handler);
      g_object_remove_weak_pointer (G_OBJECT (PRIV_SCI (self)), (gpointer*) &self->priv->doc);
    }

  self->priv->doc = doc;
  self->priv->sci_handler = 0;

  if (DOC_VALID (self->priv->doc))
    {
      self->priv->sci_handler =
        g_signal_connect_swapped (PRIV_SCI (self),
                                  "sci-notify",
                                  G_CALLBACK (cdk_completer_sci_notify),
                                  self);
      g_object_add_weak_pointer (G_OBJECT (PRIV_SCI (self)), (gpointer*) &self->priv->doc);

      SSM (doc->editor->sci, SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT, 0);
      SSM (doc->editor->sci, SCI_AUTOCSETSEPARATOR, '\n', 0);
    }

  g_object_notify (G_OBJECT (self), "document");
}
