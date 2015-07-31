/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkplugin.h>
#include <cdk/cdkhighlighter.h>
#include <cdk/cdkcompleter.h>
#include <cdk/cdkdiagnostics.h>
#include <cdk/cdkutils.h>
#include <geanyplugin.h>
#include <clang-c/Index.h>
#include <unistd.h>

typedef struct
{
  CdkPlugin        *plugin;       // the CdkPlugin that owns this
  CdkCompleter     *completer;    // auto-completion helper
  CdkHighlighter   *highlighter;  // syntax highlighting helper
  CdkDiagnostics   *diagnostics;  // diagnostic highlighter/message helper
  CXTranslationUnit tu;           // libclang translation unit
  GeanyDocument    *doc;          // the associated GeanyDocument
}
CdkDocumentData;

struct CdkPluginPrivate_
{
  CXIndex         index;         // libclang index for all TUs
  GHashTable     *file_set;      // set of project files
  gboolean        project_open;  // whether a CDK project is open
  gchar          *cflags;        // compiler flags
  GPtrArray      *files;         // ordered list of project files
  GeanyDocument  *current_doc;   // active document if supported or NULL
  GHashTable     *doc_data;      // maps a document to extra data/helpers
  CdkStyleScheme *scheme;        // scheme to use for highlighters
};

enum
{
  SIG_PROJECT_OPENED,
  SIG_PROJECT_CLOSED,
  SIG_PROJECT_SAVED,
  SIG_DOCUMENT_ADDED,
  SIG_DOCUMENT_REMOVED,
  SIG_DOCUMENT_UPDATED,
  NUM_SIGNALS,
};

enum
{
  PROP_0,
  PROP_CFLAGS,
  PROP_FILES,
  PROP_PROJECT_OPEN,
  PROP_CURRENT_DOCUMENT,
  PROP_SCHEME,
  NUM_PROPERTIES,
};

static gulong cdk_plugin_signals[NUM_SIGNALS] = {0};
static GParamSpec *cdk_plugin_properties[NUM_PROPERTIES] = { NULL };

static void cdk_plugin_finalize (GObject *object);
static void cdk_plugin_get_property (GObject *object, guint prop_id,
                                      GValue *value, GParamSpec *pspec);
static void cdk_plugin_set_property (GObject *object, guint prop_id,
                                      const GValue *value, GParamSpec *pspec);

G_DEFINE_TYPE (CdkPlugin, cdk_plugin, G_TYPE_OBJECT)

static CdkDocumentData *
cdk_document_data_new (void)
{
  return g_slice_new0 (CdkDocumentData);
}

static void
cdk_document_data_free (CdkDocumentData *data)
{
  if (G_UNLIKELY (data == NULL))
    return;

  GeanyDocument *doc = data->doc;
  CdkPlugin *self = data->plugin;

  if (CDK_IS_COMPLETER (data->completer))
    g_object_unref (data->completer);

  if (CDK_IS_HIGHLIGHTER (data->highlighter))
    g_object_unref (data->highlighter);

  if (CDK_IS_DIAGNOSTICS (data->diagnostics))
    g_object_unref (data->diagnostics);

  if (data->tu != NULL)
    clang_disposeTranslationUnit (data->tu);

  g_slice_free (CdkDocumentData, data);

  g_signal_emit_by_name (self, "document-removed", doc);
}

static void
cdk_plugin_class_init (CdkPluginClass *klass)
{
  GObjectClass *g_object_class;

  g_object_class = G_OBJECT_CLASS (klass);

  g_object_class->finalize = cdk_plugin_finalize;
  g_object_class->get_property = cdk_plugin_get_property;
  g_object_class->set_property = cdk_plugin_set_property;

  cdk_plugin_signals[SIG_PROJECT_OPENED] =
    g_signal_new ("project-opened",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  cdk_plugin_signals[SIG_PROJECT_CLOSED] =
    g_signal_new ("project-closed",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  cdk_plugin_signals[SIG_PROJECT_SAVED] =
    g_signal_new ("project-saved",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  cdk_plugin_signals[SIG_DOCUMENT_ADDED] =
    g_signal_new ("document-added",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  cdk_plugin_signals[SIG_DOCUMENT_REMOVED] =
    g_signal_new ("document-removed",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  cdk_plugin_signals[SIG_DOCUMENT_UPDATED] =
    g_signal_new ("document-updated",
                  G_TYPE_FROM_CLASS (g_object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  cdk_plugin_properties[PROP_CFLAGS] =
    g_param_spec_string ("cflags",
                         "CompilerFlags",
                         "Flags passed to compiler",
                         "",
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_plugin_properties[PROP_FILES] =
    g_param_spec_boxed ("files",
                        "SourceFiles",
                        "Main translation unit files",
                        G_TYPE_STRV,
                        G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_plugin_properties[PROP_PROJECT_OPEN] =
    g_param_spec_boolean ("project-open",
                          "ProjectIsOpen",
                          "Whether a Geany/CDK project is open",
                          FALSE,
                          G_PARAM_READABLE);

  cdk_plugin_properties[PROP_CURRENT_DOCUMENT] =
    g_param_spec_pointer ("current-document",
                          "CurrentDocument",
                          "The active document, if any",
                          G_PARAM_READWRITE);

  cdk_plugin_properties[PROP_SCHEME] =
    g_param_spec_object ("style-scheme",
                         "StyleScheme",
                         "The style scheme to apply to documents",
                         CDK_TYPE_STYLE_SCHEME,
                         G_PARAM_READWRITE);

  g_object_class_install_properties (g_object_class, NUM_PROPERTIES,
                                     cdk_plugin_properties);

  g_type_class_add_private ((gpointer)klass, sizeof (CdkPluginPrivate));
}

static void
cdk_plugin_finalize (GObject *object)
{
  CdkPlugin *self;

  g_return_if_fail (CDK_IS_PLUGIN (object));

  self = CDK_PLUGIN (object);

  g_hash_table_destroy (self->priv->file_set);
  g_hash_table_destroy (self->priv->doc_data);

  g_free (self->priv->cflags);
  g_ptr_array_free (self->priv->files, TRUE);

  if (self->priv->index)
    clang_disposeIndex (self->priv->index);

  g_object_set_data (G_OBJECT (geany_data->main_widgets->window), "cdk-plugin", NULL);

  G_OBJECT_CLASS (cdk_plugin_parent_class)->finalize (object);
}

static void
cdk_plugin_init (CdkPlugin *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_PLUGIN, CdkPluginPrivate);

  self->priv->project_open = FALSE;
  self->priv->index = clang_createIndex (TRUE, TRUE);
  self->priv->cflags = g_strdup ("");
  self->priv->files = g_ptr_array_new_with_free_func (g_free);
  self->priv->file_set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->priv->doc_data =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) cdk_document_data_free);

  g_object_set_data (G_OBJECT (geany_data->main_widgets->window), "cdk-plugin", self);

  // Load the default style scheme
  gchar *scheme_fn = g_build_filename (CDK_STYLE_SCHEME_DIR, "default.xml", NULL);
  CdkStyleScheme *scheme = cdk_style_scheme_new (scheme_fn);
  g_free (scheme_fn);
  g_assert (CDK_IS_STYLE_SCHEME (scheme));
  cdk_plugin_set_style_scheme (self, scheme);
  g_object_unref (scheme);
}

static void
cdk_plugin_get_property (GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  CdkPlugin *self = CDK_PLUGIN (object);

  switch (prop_id)
    {
    case PROP_CFLAGS:
      g_value_set_string (value, cdk_plugin_get_cflags (self));
      break;
    case PROP_FILES:
      g_value_set_boxed (value, self->priv->files->pdata);
      break;
    case PROP_PROJECT_OPEN:
      g_value_set_boolean (value, self->priv->project_open);
      break;
    case PROP_CURRENT_DOCUMENT:
      g_value_set_pointer (value, self->priv->current_doc);
      break;
    case PROP_SCHEME:
      g_value_set_object (value, self->priv->scheme);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cdk_plugin_set_property (GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  CdkPlugin *self = CDK_PLUGIN (object);

  switch (prop_id)
    {
    case PROP_CFLAGS:
      cdk_plugin_set_cflags (self, g_value_get_string (value));
      break;
    case PROP_FILES:
      cdk_plugin_set_files (self, g_value_get_boxed (value), -1);
      break;
    case PROP_CURRENT_DOCUMENT:
      cdk_plugin_set_current_document (self, g_value_get_pointer (value));
      break;
    case PROP_SCHEME:
      cdk_plugin_set_style_scheme (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CdkPlugin *
cdk_plugin_new (void)
{
  return g_object_new (CDK_TYPE_PLUGIN, NULL);
}

static inline gboolean
cdk_plugin_is_supported_document (CdkPlugin *self,
                           GeanyDocument *doc)
{
  return (self->priv->project_open &&
           DOC_VALID (doc) &&
           doc->real_path != NULL &&
          (doc->file_type->id == GEANY_FILETYPES_C ||
           doc->file_type->id == GEANY_FILETYPES_CPP ||
           doc->file_type->id == GEANY_FILETYPES_OBJECTIVEC) &&
           g_hash_table_lookup (self->priv->file_set, doc->real_path) != NULL);
}

static CXTranslationUnit
cdk_plugin_create_translation_unit (CdkPlugin *self,
                                    GeanyDocument *doc)
{
  CXTranslationUnit tu = NULL;
  gchar **argv = NULL;
  gint argc = 0;
  GError *err = NULL;

  if (! g_shell_parse_argv (self->priv->cflags, &argc, &argv, &err))
    {
      g_warning ("failed to parse compiler flags: %s", err->message);
      g_error_free (err);
      return NULL;
    }

  enum CXErrorCode error =
    clang_parseTranslationUnit2 (self->priv->index,
                                 doc->real_path,
                                 (const gchar *const *) argv, argc,
                                 NULL, 0,
                                 clang_defaultEditingTranslationUnitOptions (),
                                 &tu);

  g_strfreev (argv);

  if (error == CXError_Success)
    return tu;

  g_critical ("failed to parse translation unit '%s', error '%u'",
              doc->real_path, (guint) error);

  if (tu != NULL)
    clang_disposeTranslationUnit (tu);
  return NULL;
}

gboolean
cdk_plugin_add_document (CdkPlugin *self, struct GeanyDocument *doc)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), FALSE);

  if (! cdk_plugin_is_supported_document (self, doc))
    return FALSE;

  // If the TU isn't valid, don't add the document
  CXTranslationUnit tu = cdk_plugin_create_translation_unit (self, doc);
  if (tu == NULL)
    return FALSE;

  // In case the document was already added, remove previous one first
  cdk_plugin_remove_document (self, doc);

  CdkDocumentData *data = cdk_document_data_new ();
  data->plugin = self;
  data->tu = tu;
  data->highlighter = cdk_highlighter_new (self, doc);
  data->completer = cdk_completer_new (self, doc);
  data->diagnostics = cdk_diagnostics_new (self, doc);
  data->doc = doc;

  cdk_highlighter_set_style_scheme (data->highlighter, self->priv->scheme);

  g_hash_table_insert (self->priv->doc_data, doc, data);
  g_signal_emit_by_name (self, "document-added", doc);

  return cdk_plugin_update_document (self, doc);
}

gboolean
cdk_plugin_remove_document (CdkPlugin *self, struct GeanyDocument *doc)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), FALSE);
  return g_hash_table_remove (self->priv->doc_data, doc);
}

static enum CXErrorCode
cdk_plugin_reparse_unsaved (G_GNUC_UNUSED CdkPlugin *self,
                            CXTranslationUnit tu,
                            GeanyDocument *doc)
{
  struct CXUnsavedFile usf;
  usf.Filename = doc->real_path;
  usf.Contents = cdk_document_get_contents (doc);
  usf.Length = cdk_document_get_length (doc);
  return clang_reparseTranslationUnit (tu, 1, &usf, clang_defaultReparseOptions (tu));
}

gboolean
cdk_plugin_update_document (CdkPlugin *self, struct GeanyDocument *doc)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), FALSE);

  CdkDocumentData *data = g_hash_table_lookup (self->priv->doc_data, doc);
  if (data == NULL)
    return FALSE;

  enum CXErrorCode status = CXError_Success;
  if (doc->changed)
    status = cdk_plugin_reparse_unsaved (self, data->tu, doc);
  else
    status = clang_reparseTranslationUnit (data->tu, 0, NULL, clang_defaultReparseOptions (data->tu));

  if (status == CXError_Success)
    {
      cdk_document_helper_updated (CDK_DOCUMENT_HELPER (data->completer));
      cdk_document_helper_updated (CDK_DOCUMENT_HELPER (data->highlighter));
      cdk_document_helper_updated (CDK_DOCUMENT_HELPER (data->diagnostics));
      g_signal_emit_by_name (self, "document-updated", doc);
      return TRUE;
    }
  return FALSE;
}

struct CXTranslationUnitImpl *
cdk_plugin_get_translation_unit (CdkPlugin *self,
                                 struct GeanyDocument *doc)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), NULL);
  CdkDocumentData *data = g_hash_table_lookup (self->priv->doc_data, doc);
  if (data != NULL)
    return data->tu;
  return NULL;
}

static void
cdk_ptr_array_clear (GPtrArray *arr)
{
  while (arr->len > 0)
    g_ptr_array_remove_index (arr, arr->len - 1);
  if (arr->len != 0)
    g_critical ("failed arr->len == 0? = %lu", (gulong) arr->len);
}

void
cdk_plugin_open_project (CdkPlugin *self, GKeyFile *config)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));
  g_return_if_fail (config != NULL);

  self->priv->project_open = TRUE;
  if (self->priv->index != NULL)
    clang_disposeIndex (self->priv->index);
  self->priv->index = clang_createIndex (TRUE, TRUE);

  if (g_key_file_has_group (config, "cdk"))
    {

      if (g_key_file_has_key (config, "cdk", "cflags", NULL))
        {
          gchar *command = g_key_file_get_string (config, "cdk", "cflags", NULL);
          if (command != NULL)
            cdk_plugin_set_cflags (self, command);
          g_free (command);
        }

      if (g_key_file_has_key (config, "cdk", "files", NULL))
        {
          g_hash_table_remove_all (self->priv->file_set);
          cdk_ptr_array_clear (self->priv->files);

          gsize n_files = 0;
          gchar **files =
            g_key_file_get_string_list (config, "cdk", "files", &n_files, NULL);
          if (files != NULL)
            {
              // files in config file are relative but we need absolute
              // so change temorarily into to the project base dir and
              // expand the paths, then change back into whatever dir
              // we were in.
              gchar *cwd = g_get_current_dir ();
              chdir (geany_data->app->project->base_path);
              for (gsize i = 0; i < n_files; i++)
                {
                  gchar *file = cdk_abspath (files[i]);
                  g_hash_table_insert (self->priv->file_set, file, file);
                  g_ptr_array_add (self->priv->files, g_strdup (file));
                }
              chdir (cwd);
              g_free (cwd);
              g_strfreev (files);
            }

          g_ptr_array_add (self->priv->files, NULL);
        }
    }

  g_object_notify (G_OBJECT (self), "project-open");
  g_signal_emit_by_name (self, "project-opened");
}

void
cdk_plugin_save_project (CdkPlugin *self, GKeyFile *config)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));
  g_return_if_fail (config != NULL);

  if (! self->priv->project_open)
    return;

  g_key_file_set_string (config, "cdk", "cflags", self->priv->cflags);

  // store paths in config file as relative to project dir
  gchar **files = cdk_relpaths ((const gchar *const *) self->priv->files->pdata,
                                geany_data->app->project->base_path);
  g_key_file_set_string_list (config, "cdk", "files",
                              (const gchar *const *) files,
                              self->priv->files->len - 1);
  g_strfreev (files);

  g_signal_emit_by_name (self, "project-saved");
}

void
cdk_plugin_close_project (CdkPlugin *self)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));

  if (! self->priv->project_open)
    return;

  g_hash_table_remove_all (self->priv->doc_data);
  g_hash_table_remove_all (self->priv->file_set);

  if (self->priv->cflags != NULL)
    self->priv->cflags[0] = '\0';
  else
    self->priv->cflags = g_strdup ("");
  cdk_ptr_array_clear (self->priv->files);

  clang_disposeIndex (self->priv->index);
  self->priv->index = clang_createIndex (TRUE, TRUE);

  cdk_plugin_set_current_document (self, NULL);

  self->priv->project_open = FALSE;

  g_signal_emit_by_name (self, "project-closed");
  g_object_notify (G_OBJECT (self), "project-open");
}

gboolean
cdk_plugin_is_project_open (CdkPlugin *self)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), FALSE);
  return self->priv->project_open;
}

struct GeanyDocument *
cdk_plugin_get_current_document (CdkPlugin *self)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), NULL);
  return self->priv->current_doc;
}

void
cdk_plugin_set_current_document (CdkPlugin *self, struct GeanyDocument *doc)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));
  if (doc == NULL ||
      g_hash_table_lookup (self->priv->doc_data, doc) != NULL)
    {
      self->priv->current_doc = doc;
      g_object_notify (G_OBJECT (self), "current-document");
    }
}

const gchar *
cdk_plugin_get_cflags (CdkPlugin *self)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), NULL);
  return self->priv->cflags;
}

const gchar *const *
cdk_plugin_get_files (CdkPlugin *self, gsize *n_files)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), NULL);
  if (n_files != NULL)
    *n_files = self->priv->files->len - 1;
  return (const gchar *const *) self->priv->files->pdata;
}

void
cdk_plugin_set_cflags (CdkPlugin *self,
                       const gchar *cflags)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));

  if (g_strcmp0 (self->priv->cflags, cflags) != 0)
    {
      g_free (self->priv->cflags);
      self->priv->cflags = g_strdup (cflags ? cflags : "");
      g_object_notify (G_OBJECT (self), "cflags");
    }
}

void
cdk_plugin_set_files (CdkPlugin *self,
                      const gchar *const *files,
                      gssize n_files)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));

  cdk_ptr_array_clear (self->priv->files);
  g_hash_table_remove_all (self->priv->file_set);
  if (files && n_files > 0)
    {
      for (const gchar *const *it = files; *it != NULL; it++)
        {
          gchar *f = g_strdup (*it);
          g_hash_table_insert (self->priv->file_set, f, f);
          g_ptr_array_add (self->priv->files, g_strdup (*it));
        }
    }
  g_ptr_array_add (self->priv->files, NULL);

  g_object_notify (G_OBJECT (self), "files");
}

CdkStyleScheme *
cdk_plugin_get_style_scheme (CdkPlugin *self)
{
  g_return_val_if_fail (CDK_IS_PLUGIN (self), NULL);
  return self->priv->scheme;
}

void
cdk_plugin_set_style_scheme (CdkPlugin *self, CdkStyleScheme *scheme)
{
  g_return_if_fail (CDK_IS_PLUGIN (self));

  if (scheme != self->priv->scheme)
    {
      //g_debug("set plugin scheme to %p", (void*)scheme);
      if (G_IS_OBJECT (self->priv->scheme))
        g_object_unref (self->priv->scheme);
      self->priv->scheme = g_object_ref (scheme);
      g_object_notify (G_OBJECT (self), "style-scheme");
    }
}
