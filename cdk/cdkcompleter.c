/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkcompleter.h>
#include <cdk/cdkutils.h>
#include <cdk/cdkplugin.h>
#include <geanyplugin.h>
#include <clang-c/Index.h>

struct CdkCompleterPrivate_
{
  gulong sci_handler;
  uptr_t prev_autoc_order;
  uptr_t prev_autoc_sep;
};

enum
{
  PROP_0,
  NUM_PROPERTIES,
};

static void cdk_completer_finalize (GObject *object);
static void cdk_completer_sci_notify (CdkCompleter *self,
                                      gint unused,
                                      SCNotification *notif,
                                      ScintillaObject *sci);

G_DEFINE_TYPE (CdkCompleter, cdk_completer, CDK_TYPE_DOCUMENT_HELPER)

static void
cdk_completer_initialize_document (CdkDocumentHelper *object,
                                   GeanyDocument *document)
{
  CdkCompleter *self = CDK_COMPLETER (object);
  ScintillaObject *sci = document->editor->sci;

  self->priv->prev_autoc_order = cdk_sci_send (sci, SCI_AUTOCGETORDER, 0, 0);
  self->priv->prev_autoc_sep = cdk_sci_send (sci, SCI_AUTOCGETSEPARATOR, 0, 0);

  cdk_sci_send (sci, SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT, 0);
  cdk_sci_send (sci, SCI_AUTOCSETSEPARATOR, '\n', 0);
  self->priv->sci_handler =
    g_signal_connect_swapped (sci, "sci-notify",
                              G_CALLBACK (cdk_completer_sci_notify), self);
}

static void
cdk_completer_deinitialize_document (CdkCompleter  *self,
                                     GeanyDocument *document)
{
  ScintillaObject *sci = document->editor->sci;

  if (self->priv->sci_handler > 0)
    g_signal_handler_disconnect (sci, self->priv->sci_handler);

  cdk_sci_send (sci, SCI_AUTOCSETORDER, self->priv->prev_autoc_order, 0);
  cdk_sci_send (sci, SCI_AUTOCSETSEPARATOR, self->priv->prev_autoc_sep, 0);
}

static void
cdk_completer_class_init (CdkCompleterClass *klass)
{
  GObjectClass *g_object_class;
  CdkDocumentHelperClass *dh_object_class;

  g_object_class = G_OBJECT_CLASS (klass);
  dh_object_class = CDK_DOCUMENT_HELPER_CLASS (klass);

  dh_object_class->initialize = cdk_completer_initialize_document;

  g_object_class->finalize = cdk_completer_finalize;

  g_type_class_add_private ((gpointer)klass, sizeof (CdkCompleterPrivate));
}

static void
cdk_completer_finalize (GObject *object)
{
  CdkCompleter *self;

  g_return_if_fail (CDK_IS_COMPLETER (object));

  self = CDK_COMPLETER (object);

  GeanyDocument *doc = cdk_document_helper_get_document (CDK_DOCUMENT_HELPER (self));
  cdk_completer_deinitialize_document (self, doc);

  G_OBJECT_CLASS (cdk_completer_parent_class)->finalize (object);
}

static void
cdk_completer_init (CdkCompleter *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_COMPLETER, CdkCompleterPrivate);
}

CdkCompleter *
cdk_completer_new (struct CdkPlugin_ *plugin, GeanyDocument *doc)
{
  return g_object_new (CDK_TYPE_COMPLETER, "plugin", plugin, "document", doc, NULL);
}

static void
cdk_completer_complete (CdkCompleter *self,
                        gint word_start,
                        gint current_pos,
                        const gchar *current_word)
{
  CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
  GeanyDocument *doc = cdk_document_helper_get_document (helper);
  ScintillaObject *sci = doc->editor->sci;
  CdkPlugin *plugin = cdk_document_helper_get_plugin (helper);
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (plugin, doc);
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
  guint line = cdk_sci_send (sci, SCI_LINEFROMPOSITION, current_pos, 0) + 1;
  guint col = cdk_sci_send (sci, SCI_GETCOLUMN, current_pos, 0) - 1;

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
    cdk_sci_send (sci, SCI_AUTOCSHOW, current_pos - word_start, compl_list);
  g_free (compl_list);
}

static void
cdk_completer_handle_key (CdkCompleter *self,
                          ScintillaObject *sci,
                          gint offset)
{
  gint prev_ch = '\0';
  gint word_start = cdk_sci_send (sci, SCI_WORDSTARTPOSITION, offset, TRUE);
  gint chr = '\0';

  if (offset > 1)
    prev_ch = cdk_sci_send (sci, SCI_GETCHARAT, offset - 2, 0);
  if (offset > 0)
    chr = cdk_sci_send (sci, SCI_GETCHARAT, offset - 1, 0);

  // if dereferencing a struct/class member or at least 3 chars into
  // a word, then show the autocomplete
  if (chr == '.' || (prev_ch == '-' && chr == '>') || ((offset - word_start) > 2))
    {
      struct Sci_TextRange tr;
      tr.lpstrText = g_malloc0 (offset - word_start + 1);
      tr.chrg.cpMin = word_start;
      tr.chrg.cpMax = offset;
      cdk_sci_send (sci, SCI_GETTEXTRANGE, 0, &tr);
      cdk_completer_complete (self, word_start, offset, tr.lpstrText);
      g_free (tr.lpstrText);
    }
}

static void
cdk_completer_sci_notify (CdkCompleter *self,
                          G_GNUC_UNUSED gint unused,
                          SCNotification *notif,
                          ScintillaObject *sci)
{
  if (notif->nmhdr.code == SCN_CHARADDED)
    {
      gint offset = cdk_sci_send (sci, SCI_GETCURRENTPOS, 0, 0);
      cdk_completer_handle_key (self, sci, offset);
    }
}
