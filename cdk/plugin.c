/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkplugin.h>
#include <cdk/cdkstyle.h>
#include <cdk/cdkstylescheme.h>
#include <cdk/cdkutils.h>
#include <geanyplugin.h>

static gint update_timeout = 250;
static gulong update_handler = 0;
static GtkWidget *project_page = NULL;
static GtkTextView *cflags_textview = NULL;
static GtkTextView *files_textview = NULL;
static CdkPlugin *cdk_plugin = NULL;
GeanyData *geany_data;
GeanyPlugin *geany_plugin;

static inline gboolean cdk_project_is_open ()
{
  return cdk_plugin_is_project_open (cdk_plugin);
}

static void on_project_open (G_GNUC_UNUSED GObject *object,
  GKeyFile *config, G_GNUC_UNUSED gpointer user_data)
{
  cdk_plugin_open_project (cdk_plugin, config);
}

static void on_project_close (G_GNUC_UNUSED GObject *object,
  G_GNUC_UNUSED gpointer user_data)
{
  cdk_plugin_close_project (cdk_plugin);
}

static void on_project_save (G_GNUC_UNUSED GObject *object,
  GKeyFile *config, G_GNUC_UNUSED gpointer user_data)
{
  cdk_plugin_save_project (cdk_plugin, config);
}

static void on_document_open (G_GNUC_UNUSED GObject *object,
  GeanyDocument *doc, G_GNUC_UNUSED gpointer user_data)
{
  if (cdk_project_is_open ())
    cdk_plugin_add_document (cdk_plugin, doc);
}

static void on_document_close (G_GNUC_UNUSED GObject *object,
  GeanyDocument *doc, G_GNUC_UNUSED gpointer user_data)
{
  if (cdk_project_is_open ())
    cdk_plugin_remove_document (cdk_plugin, doc);
}

static void on_document_save (G_GNUC_UNUSED GObject *object,
  GeanyDocument *doc, G_GNUC_UNUSED gpointer user_data)
{
  if (cdk_project_is_open ())
    cdk_plugin_update_document (cdk_plugin, doc);
}

static void on_document_activate (G_GNUC_UNUSED GObject *object,
  GeanyDocument *doc, G_GNUC_UNUSED gpointer user_data)
{
  if (cdk_project_is_open ())
    {
      cdk_plugin_set_current_document (cdk_plugin, doc);
      cdk_plugin_update_document (cdk_plugin, doc);
    }
}

static void on_document_filetype_set (G_GNUC_UNUSED GObject *object,
  GeanyDocument *doc, G_GNUC_UNUSED GeanyFiletype *old_ft,
  G_GNUC_UNUSED gpointer user_data)
{
  if (cdk_project_is_open ())
    {
      cdk_plugin_remove_document (cdk_plugin, doc);
      cdk_plugin_add_document (cdk_plugin, doc);
    }
}

static void on_project_dialog_open (G_GNUC_UNUSED GObject *object,
  GtkWidget *notebook, G_GNUC_UNUSED gpointer user_data)
{
  if (! cdk_project_is_open ())
    return;

  if (! GTK_IS_WIDGET (project_page))
    {
      GtkBuilder *builder = gtk_builder_new ();
      gchar *ui_file = g_build_filename (CDK_PLUGIN_UI_DIR, "projectpanel.glade", NULL);
      GError *error = NULL;
      if (! gtk_builder_add_from_file (builder, ui_file, &error))
        {
          g_critical ("failed to load CDK project panel from file '%s': %s", ui_file, error->message);
          g_error_free (error);
          g_free (ui_file);
          g_object_unref (builder);
          return;
        }
      g_free (ui_file);
      project_page = GTK_WIDGET (gtk_builder_get_object (builder, "cdk_project_main_box"));
      gtk_notebook_append_page (GTK_NOTEBOOK (notebook), project_page, gtk_label_new("CDK"));

      cflags_textview = GTK_TEXT_VIEW (gtk_builder_get_object (builder, "cdk_cflags_text"));
      files_textview = GTK_TEXT_VIEW (gtk_builder_get_object (builder, "cdk_files_text"));

      PangoFontDescription *pfd = pango_font_description_from_string ("Monospace 9");
      gtk_widget_override_font (GTK_WIDGET (cflags_textview), pfd);
      gtk_widget_override_font (GTK_WIDGET (files_textview), pfd);
      pango_font_description_free (pfd);

      GtkWidget *swin = GTK_WIDGET (gtk_builder_get_object (builder, "cdk_cflags_swin"));
      gtk_widget_set_size_request (swin, -1, 64);

      g_object_unref (builder);
    }

  const gchar *cflags = cdk_plugin_get_cflags (CDK_PLUGIN (cdk_plugin));
  gtk_text_buffer_set_text (gtk_text_view_get_buffer (cflags_textview), cflags, -1);

  const gchar *const *files_ = cdk_plugin_get_files (CDK_PLUGIN (cdk_plugin), NULL);
  gchar **rfiles = cdk_relpaths (files_, geany_data->app->project->base_path);
  gchar *files = g_strjoinv ("\n", (GStrv) rfiles);
  g_strfreev (rfiles);
  gtk_text_buffer_set_text (gtk_text_view_get_buffer (files_textview), files, -1);
  g_free(files);
}

static void on_project_dialog_close (G_GNUC_UNUSED GObject *object,
  G_GNUC_UNUSED GtkWidget *notebook, G_GNUC_UNUSED gpointer user_data)
{
}

static gchar *text_view_get_text (GtkTextView *tv)
{
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (tv);
  GtkTextIter start, end;
  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);
  return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void on_project_dialog_confirmed (G_GNUC_UNUSED GObject *object,
  G_GNUC_UNUSED GtkWidget *notebook, G_GNUC_UNUSED gpointer user_data)
{
  if (! cdk_project_is_open ())
    return;

  gchar *cflags_text = text_view_get_text (cflags_textview);
  gchar *files_text = text_view_get_text (files_textview);
  gchar **files_list_abs = g_strsplit (files_text, "\n", 0);
  gchar **files_list =
    cdk_relpaths ((const gchar *const *)files_list_abs,
                  geany_data->app->project->base_path);
  g_strfreev (files_list_abs);
  gint files_len = g_strv_length (files_list);

  GKeyFile *config = g_key_file_new ();
  g_key_file_set_string (config, "cdk", "cflags", cflags_text);
  g_free (cflags_text);
  g_key_file_set_string_list (config, "cdk", "files", (const gchar *const *) files_list, files_len);
  g_strfreev (files_list);

  // FIXME: don't reload the whole thing here
  cdk_plugin_close_project (cdk_plugin);
  cdk_plugin_open_project (cdk_plugin, config);
  g_key_file_free (config);

  guint i;
  foreach_document (i)
    {
      on_document_open(NULL, documents[i], NULL);
    }
}

static gboolean on_update_timeout (GeanyDocument *doc)
{
  if (cdk_project_is_open ())
    cdk_plugin_update_document (cdk_plugin, doc);
  update_handler = 0;
  return FALSE;
}

static gboolean on_editor_notify (G_GNUC_UNUSED GObject *object,
  GeanyEditor *editor, SCNotification *nt, G_GNUC_UNUSED gpointer user_data)
{
  if (cdk_project_is_open () &&
      nt->nmhdr.code == SCN_MODIFIED &&
      (nt->modificationType & SC_MOD_INSERTTEXT ||
       nt->modificationType & SC_MOD_DELETETEXT) &&
      update_handler == 0)
    {
      update_handler = g_timeout_add (update_timeout,
        (GSourceFunc) on_update_timeout, editor->document);
    }
  return FALSE;
}

//
// Geany plugin implementation
//

PLUGIN_VERSION_CHECK (224)

PLUGIN_SET_INFO (_("C/C++ Development Kit"),
                 _("Provides advanced IDE features for C and C++"),
                 "0.1",
                 "Matthew Brush <matt@geany.org>")

#define PC(name, cb, data) \
  plugin_signal_connect(geany_plugin, NULL, name, FALSE, G_CALLBACK(cb), data)

void plugin_init (G_GNUC_UNUSED GeanyData *unused)
{
  plugin_module_make_resident (geany_plugin);

  cdk_style_schemes_init ();

  cdk_plugin = cdk_plugin_new ();
  g_object_set_data (G_OBJECT (geany_data->main_widgets->window),
                     "cdk-plugin", cdk_plugin);

  PC("project-open", on_project_open, NULL);
  PC("project-close", on_project_close, NULL);
  PC("project-save", on_project_save, NULL);
  PC("project-dialog-open", on_project_dialog_open, NULL);
  PC("project-dialog-close", on_project_dialog_close, NULL);
  PC("project-dialog-confirmed", on_project_dialog_confirmed, NULL);
  PC("document-new", on_document_open, NULL);
  PC("document-open", on_document_open, NULL);
  PC("document-close", on_document_close, NULL);
  PC("document-save", on_document_save, NULL);
  PC("document-activate", on_document_activate, NULL);
  PC("document-filetype-set", on_document_filetype_set, NULL);
  PC("editor-notify", on_editor_notify, NULL);

  // if a project was already open, open the CDK project
  if (geany_data->app->project != NULL)
    {
      GKeyFile *config = g_key_file_new ();
      if (g_key_file_load_from_file (config,
                                     geany_data->app->project->file_name,
                                     G_KEY_FILE_NONE,
                                     NULL))
        {
          cdk_plugin_open_project (cdk_plugin, config);
        }
      g_key_file_free (config);

      // try and add any documents already opened
      guint i = 0;
      foreach_document(i)
        {
          on_document_open (NULL, documents[i], NULL);
        }

      GeanyDocument *doc = document_get_current ();
      if (DOC_VALID (doc))
        cdk_plugin_set_current_document (cdk_plugin, doc);
    }
}

void plugin_cleanup (void)
{
  if (cdk_project_is_open ())
    cdk_plugin_close_project (cdk_plugin);

  g_object_set_data (G_OBJECT (geany_data->main_widgets->window),
                     "cdk-plugin", NULL);

  if (update_handler != 0)
    {
      g_source_remove (update_handler);
      update_handler = 0;
    }

  if (GTK_IS_WIDGET (project_page))
    {
      gtk_widget_destroy (GTK_WIDGET (project_page));
      project_page = NULL;
    }

  cflags_textview = NULL;
  files_textview = NULL;

  if (CDK_IS_PLUGIN (cdk_plugin))
    {
      g_object_unref (cdk_plugin);
      cdk_plugin = NULL;
    }

  cdk_style_schemes_cleanup ();
}
