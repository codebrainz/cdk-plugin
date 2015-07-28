/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

/**
 * SECTION:cdkdiagnostics
 * @title: Diagnostics
 * @short_description: Diagnostic highlighting and messages.
 * @include: cdk/cdk.h
 *
 * The #CdkDiagnostics class is a #CdkDocumentHelper that updates various
 * diagnostics visualizations whenever the translation unit is reparsed.
 * It's responsible for putting messages in the Compiler tab, putting
 * the "squiggly lines" below warnings and errors, and putting warning
 * and error markers in the left symbol margin.
 */

#include <cdk/cdkdiagnostics.h>
#include <cdk/cdkplugin.h>
#include <cdk/cdkutils.h>
#include <geanyplugin.h>
#include <clang-c/Index.h>
#include <ctype.h>

// RGBA icons in C source
#include <cdk/cdkmarkererror.c>
#include <cdk/cdkmarkerwarning.c>

#define CDK_DIAGNOSTICS_INDIC_WARNING INDIC_CONTAINER
#define CDK_DIAGNOSTICS_INDIC_ERROR   INDIC_CONTAINER+1
#define CDK_DIAGNOSTICS_MARKER_WARNING 23
#define CDK_DIAGNOSTICS_MARKER_ERROR   24

struct CdkDiagnosticsPrivate_
{
  gboolean indicators_enabled;
  gboolean markers_enabled;
  gboolean compiler_messages_enabled;
  CdkStyleScheme *scheme;
  sptr_t prev_w_indic_style;
  sptr_t prev_w_indic_fore;
  sptr_t prev_e_indic_style;
  sptr_t prev_e_indic_fore;
  gulong sci_notify_hnd;
  gboolean annot_on;
  gchar *msgdir;
};

struct CdkDiagnosticsRangeData
{
  CdkDiagnosticRangeFunc func;
  gpointer user_data;
  gint counter;
};

enum
{
  PROP_0,
  PROP_SCHEME,
  PROP_INDICATORS_ENABLED,
  PROP_MARKERS_ENABLED,
  PROP_COMPILER_MESSAGES_ENABLED,
  NUM_PROPERTIES,
};

static GParamSpec *cdk_diagnostics_properties[NUM_PROPERTIES] = { NULL };

static void cdk_diagnostics_finalize (GObject *object);
static void cdk_diagnostics_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void cdk_diagnostics_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void cdk_diagnostics_initialize_document (CdkDocumentHelper *object, GeanyDocument *document);
static void cdk_diagnostics_deinitialize_document (CdkDiagnostics *self, GeanyDocument *document);
static void cdk_diagnostics_updated (CdkDocumentHelper *object, GeanyDocument *document);
static void cdk_diagnostics_clear_indicators (CdkDiagnostics *self, GeanyDocument *document);
static void cdk_diagnostics_clear_markers (CdkDiagnostics *self, GeanyDocument *document);
static void cdk_diagnostics_clear_compiler_messages (CdkDiagnostics *self);

G_DEFINE_TYPE (CdkDiagnostics, cdk_diagnostics, CDK_TYPE_DOCUMENT_HELPER)

static void
cdk_diagnostics_constructed (GObject *object)
{
  G_OBJECT_CLASS (cdk_diagnostics_parent_class)->constructed (object);
  CdkPlugin *plugin = cdk_document_helper_get_plugin (CDK_DOCUMENT_HELPER (object));
  g_return_if_fail (CDK_IS_PLUGIN (plugin));
  g_object_bind_property (plugin, "style-scheme", object, "style-scheme", G_BINDING_SYNC_CREATE);
}

static void
cdk_diagnostics_class_init (CdkDiagnosticsClass *klass)
{
  GObjectClass *g_object_class;
  CdkDocumentHelperClass *dh_object_class;

  g_object_class = G_OBJECT_CLASS (klass);
  dh_object_class = CDK_DOCUMENT_HELPER_CLASS (klass);

  dh_object_class->initialize = cdk_diagnostics_initialize_document;
  dh_object_class->updated = cdk_diagnostics_updated;

  g_object_class->constructed = cdk_diagnostics_constructed;
  g_object_class->finalize = cdk_diagnostics_finalize;
  g_object_class->get_property = cdk_diagnostics_get_property;
  g_object_class->set_property = cdk_diagnostics_set_property;

  /**
   * CdkDiagnostics:style-scheme:
   *
   * The #CdkStyleScheme used to apply highlighting visualizations. This
   * property is bound to the #CdkPlugin:style-scheme property of the
   * #CdkPlugin instance passed to cdk_diagnostics_new().
   */
  cdk_diagnostics_properties[PROP_SCHEME] =
    g_param_spec_object ("style-scheme",
                         "StyleScheme",
                         "The CdkStyleScheme to use for diagnostics styles",
                         CDK_TYPE_STYLE_SCHEME,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  /**
   * CdkDiagnostics:indicators-enabled:
   *
   * Whether to draw squiggly lines under ranges of the source code
   * that have warnings or errors.
   */
  cdk_diagnostics_properties[PROP_INDICATORS_ENABLED] =
    g_param_spec_boolean ("indicators-enabled",
                          "IndicatorsEnabled",
                          "Whether to draw squiggly lines in the editor",
                          TRUE,
                          G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  /**
   * CdkDiagnostics:markers-enabled:
   *
   * Whether to draw warning/error markers in the symbol margin.
   */
  cdk_diagnostics_properties[PROP_MARKERS_ENABLED] =
    g_param_spec_boolean ("markers-enabled",
                          "MarkersEnabled",
                          "Whether to draw icons in the editor margin",
                          TRUE,
                          G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  /**
   * CdkDiagnostics:compiler-messages-enabled:
   *
   * Whether to put diagnostic messages in Geany's Compiler tab.
   */
  cdk_diagnostics_properties[PROP_COMPILER_MESSAGES_ENABLED] =
    g_param_spec_boolean ("compiler-messages-enabled",
                          "CompilerMessagesEnabled",
                          "Whether to show diagnostics in the compiler tab",
                          TRUE,
                          G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  g_object_class_install_properties (g_object_class,
                                     NUM_PROPERTIES,
                                     cdk_diagnostics_properties);

  g_type_class_add_private ((gpointer)klass, sizeof (CdkDiagnosticsPrivate));
}

static void
cdk_diagnostics_finalize (GObject *object)
{
  CdkDiagnostics *self;

  g_return_if_fail (CDK_IS_DIAGNOSTICS (object));

  self = CDK_DIAGNOSTICS (object);

  GeanyDocument *doc = cdk_document_helper_get_document (CDK_DOCUMENT_HELPER (self));
  cdk_diagnostics_deinitialize_document (self, doc);

  G_OBJECT_CLASS (cdk_diagnostics_parent_class)->finalize (object);
}

static void
cdk_diagnostics_init (CdkDiagnostics *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_DIAGNOSTICS, CdkDiagnosticsPrivate);
  self->priv->indicators_enabled = TRUE;
  self->priv->markers_enabled = TRUE;
  self->priv->compiler_messages_enabled = TRUE;
}

static void
cdk_diagnostics_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  CdkDiagnostics *self = CDK_DIAGNOSTICS (object);

  switch (prop_id)
    {
    case PROP_SCHEME:
      g_value_set_object (value, cdk_diagnostics_get_style_scheme (self));
      break;
    case PROP_INDICATORS_ENABLED:
      g_value_set_boolean (value, cdk_diagnostics_get_indicators_enabled (self));
      break;
    case PROP_MARKERS_ENABLED:
      g_value_set_boolean (value, cdk_diagnostics_get_markers_enabled (self));
      break;
    case PROP_COMPILER_MESSAGES_ENABLED:
      g_value_set_boolean (value, cdk_diagnostics_get_compiler_messages_enabled (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cdk_diagnostics_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  CdkDiagnostics *self = CDK_DIAGNOSTICS (object);

  switch (prop_id)
    {
    case PROP_SCHEME:
      cdk_diagnostics_set_style_scheme (self, g_value_get_object (value));
      break;
    case PROP_INDICATORS_ENABLED:
      cdk_diagnostics_set_indicators_enabled (self, g_value_get_boolean (value));
      break;
    case PROP_MARKERS_ENABLED:
      cdk_diagnostics_set_markers_enabled (self, g_value_get_boolean (value));
      break;
    case PROP_COMPILER_MESSAGES_ENABLED:
      cdk_diagnostics_set_compiler_messages_enabled (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/**
 * cdk_diagnostics_new:
 * @plugin: The #CdkPlugin that owns the new instance.
 * @doc: The #GeanyDocument related to this instance.
 *
 * Creates a new #CdkDiagnostics instance.
 *
 * Returns: A new #CdkDiagnostics instance or %NULL on error.
 */
CdkDiagnostics *
cdk_diagnostics_new (struct CdkPlugin_ *plugin,
                     struct GeanyDocument *doc)
{
  return g_object_new (CDK_TYPE_DIAGNOSTICS, "plugin", plugin, "document", doc, NULL);
}

/**
 * cdk_diagnostics_get_style_scheme:
 * @self: The #CdkDiagnostics instance.
 *
 * Getter for the #CdkDiagnostics:style-scheme property.
 *
 * Returns: The #CdkStyleScheme for this instance.
 */
CdkStyleScheme *
cdk_diagnostics_get_style_scheme (CdkDiagnostics *self)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), NULL);
  return self->priv->scheme;
}

/**
 * cdk_diagnostics_set_style_scheme:
 * @self: The #CdkDiagnostics instance.
 * @scheme: The #CdkStyleScheme to use.
 *
 * Setter for the #CdkDiagnostics:style-scheme property.
 */
void
cdk_diagnostics_set_style_scheme (CdkDiagnostics *self,
                                  CdkStyleScheme *scheme)
{
  g_return_if_fail (CDK_IS_DIAGNOSTICS (self));

  if (scheme != self->priv->scheme)
    {
      GeanyDocument *doc = cdk_document_helper_get_document (CDK_DOCUMENT_HELPER (self));
      ScintillaObject *sci = doc->editor->sci;

      if (G_IS_OBJECT (self->priv->scheme))
        g_object_unref (self->priv->scheme);
      self->priv->scheme = NULL;

      cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_WARNING, 0xFFA500);
      cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_DIAGNOSTICS_INDIC_WARNING, INDIC_SQUIGGLE);
      cdk_sci_send (sci, SCI_INDICSETUNDER, CDK_DIAGNOSTICS_INDIC_WARNING, TRUE);

      cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_ERROR, 0xCD3D40);
      cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_DIAGNOSTICS_INDIC_ERROR, INDIC_SQUIGGLE);
      cdk_sci_send (sci, SCI_INDICSETUNDER, CDK_DIAGNOSTICS_INDIC_ERROR, TRUE);


      if (CDK_IS_STYLE_SCHEME (scheme))
        {
          CdkStyle *style;
          self->priv->scheme = g_object_ref (scheme);
          style = cdk_style_scheme_get_style (self->priv->scheme, CDK_STYLE_DIAGNOSTIC_WARNING);
          if (style != NULL)
            cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_WARNING, style->fore);
          style = cdk_style_scheme_get_style (self->priv->scheme, CDK_STYLE_DIAGNOSTIC_ERROR);
          if (style != NULL)
            cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_ERROR, style->fore);
          style = cdk_style_scheme_get_style (self->priv->scheme, CDK_STYLE_ANNOTATION_WARNING);
          if (style != NULL)
            cdk_scintilla_set_style (sci, CDK_STYLE_ANNOTATION_WARNING, style);
          style = cdk_style_scheme_get_style (self->priv->scheme, CDK_STYLE_ANNOTATION_ERROR);
          if (style != NULL)
            cdk_scintilla_set_style (sci, CDK_STYLE_ANNOTATION_ERROR, style);
        }

      g_object_notify (G_OBJECT (self), "style-scheme");
    }
}

gboolean
cdk_diagnostics_get_indicators_enabled (CdkDiagnostics *self)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), FALSE);
  return self->priv->indicators_enabled;
}

void
cdk_diagnostics_set_indicators_enabled (CdkDiagnostics *self, gboolean enabled)
{
  g_return_if_fail (CDK_IS_DIAGNOSTICS (self));

  if (enabled != self->priv->indicators_enabled)
    {
      CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
      GeanyDocument *doc = cdk_document_helper_get_document (helper);

      self->priv->indicators_enabled = enabled;

      if (enabled)
        cdk_diagnostics_updated (helper, doc);
      else
        cdk_diagnostics_clear_indicators (self, doc);

      g_object_notify (G_OBJECT (self), "indicators-enabled");
    }
}

gboolean
cdk_diagnostics_get_markers_enabled (CdkDiagnostics *self)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), FALSE);
  return self->priv->markers_enabled;
}

void
cdk_diagnostics_set_markers_enabled (CdkDiagnostics *self,
                                     gboolean enabled)
{
  g_return_if_fail (CDK_IS_DIAGNOSTICS (self));

  if (enabled != self->priv->markers_enabled)
    {
      CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
      GeanyDocument *doc = cdk_document_helper_get_document (helper);

      self->priv->markers_enabled = enabled;

      if (enabled)
        cdk_diagnostics_updated (helper, doc);
      else
        cdk_diagnostics_clear_markers (self, doc);

      g_object_notify (G_OBJECT (self), "markers-enabled");
    }
}

gboolean
cdk_diagnostics_get_compiler_messages_enabled (CdkDiagnostics *self)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), FALSE);
  return self->priv->compiler_messages_enabled;
}

void
cdk_diagnostics_set_compiler_messages_enabled (CdkDiagnostics *self,
                                               gboolean enabled)
{
  g_return_if_fail (CDK_IS_DIAGNOSTICS (self));

  if (enabled != self->priv->compiler_messages_enabled)
    {
      CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
      GeanyDocument *doc = cdk_document_helper_get_document (helper);

      self->priv->compiler_messages_enabled = enabled;

      if (enabled)
        cdk_diagnostics_updated (helper, doc);
      else
        cdk_diagnostics_clear_markers (self, doc);

      g_object_notify (G_OBJECT (self), "markers-enabled");
    }
}

static void
cdk_diagnostics_annotate_line (G_GNUC_UNUSED CdkDiagnostics *self,
                               CXDiagnostic diag,
                               guint line,
                               ScintillaObject *sci)
{
  gint style = CDK_STYLE_DEFAULT;
  enum CXDiagnosticSeverity severity = clang_getDiagnosticSeverity (diag);
  switch (severity)
    {
    case CXDiagnostic_Warning:
      style = CDK_STYLE_ANNOTATION_WARNING;
      break;
    case CXDiagnostic_Error:
    case CXDiagnostic_Fatal:
      style = CDK_STYLE_ANNOTATION_ERROR;
      break;
    default:
      return;
    }

  CXString text = clang_getDiagnosticSpelling (diag);
  CXString option = clang_getDiagnosticOption (diag, NULL);
  gchar *message;

  if (strlen (clang_getCString (option)) == 0)
    message = g_strdup (clang_getCString (text));
  else
    message = g_strdup_printf ("%s [%s]", clang_getCString (text), clang_getCString (option));
  clang_disposeString (text);
  clang_disposeString (option);

  cdk_sci_send (sci, SCI_ANNOTATIONSETTEXT, line - 1, message);
  g_free (message);

  cdk_sci_send (sci, SCI_ANNOTATIONSETSTYLE, line - 1, style);
  cdk_sci_send (sci, SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED, 0);
}

static void
cdk_diagnostics_clear_annotations (CdkDiagnostics *self,
                                   GeanyDocument *document)
{
  ScintillaObject *sci = document->editor->sci;
  self->priv->annot_on = FALSE;
  cdk_sci_send (sci, SCI_ANNOTATIONCLEARALL, 0, 0);
  cdk_sci_send (sci, SCI_ANNOTATIONSETVISIBLE, ANNOTATION_HIDDEN, 0);
}

static gboolean
cdk_diagnostics_find_clicked_line (CdkDiagnostics *self,
                                   CXDiagnostic diag,
                                   guint position,
                                   gpointer user_data)
{
  ScintillaObject *sci = user_data;
  guint clicked_line = cdk_sci_send (sci, SCI_LINEFROMPOSITION, position, 0) + 1;

  CXSourceLocation locn = clang_getDiagnosticLocation (diag);
  guint diag_line = 0;
  clang_getSpellingLocation (locn, NULL, &diag_line, NULL, NULL);

  if (diag_line == clicked_line)
    {
      cdk_diagnostics_annotate_line (self, diag, clicked_line, sci);
      return FALSE; // found it, stop iterating
    }

  return TRUE; // keep going
}

static void
cdk_diagnostics_sci_notify (CdkDiagnostics *self,
                            G_GNUC_UNUSED gint unused,
                            SCNotification *notification,
                            ScintillaObject *sci)
{
  if (notification->nmhdr.code != SCN_MARGINCLICK || notification->margin != 1)
    return;

  if (self->priv->annot_on)
    {
      self->priv->annot_on = FALSE;
      cdk_sci_send (sci, SCI_ANNOTATIONCLEARALL, 0, 0);
      cdk_sci_send (sci, SCI_ANNOTATIONSETVISIBLE, ANNOTATION_HIDDEN, 0);
    }
  else
    {
      self->priv->annot_on = TRUE;
      cdk_diagnostics_foreach (self, cdk_diagnostics_find_clicked_line, sci);
      cdk_sci_send (sci, SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED, 0);
    }
}

static void
cdk_diagnostics_initialize_document (CdkDocumentHelper *object,
                                     GeanyDocument *document)
{
  CdkDiagnostics *self = CDK_DIAGNOSTICS (object);
  ScintillaObject *sci = document->editor->sci;

  self->priv->prev_w_indic_style = cdk_sci_send (sci, SCI_INDICGETSTYLE, CDK_DIAGNOSTICS_INDIC_WARNING, 0);
  self->priv->prev_w_indic_fore = cdk_sci_send (sci, SCI_INDICGETFORE, CDK_DIAGNOSTICS_INDIC_WARNING, 0);
  self->priv->prev_e_indic_style = cdk_sci_send (sci, SCI_INDICGETSTYLE, CDK_DIAGNOSTICS_INDIC_ERROR, 0);
  self->priv->prev_e_indic_fore = cdk_sci_send (sci, SCI_INDICGETFORE, CDK_DIAGNOSTICS_INDIC_ERROR, 0);

  cdk_sci_send (sci, SCI_RGBAIMAGESETWIDTH, cdk_marker_warning.width, 0);
  cdk_sci_send (sci, SCI_RGBAIMAGESETHEIGHT, cdk_marker_warning.height, 0);
  cdk_sci_send (sci, SCI_MARKERDEFINERGBAIMAGE, CDK_DIAGNOSTICS_MARKER_WARNING, cdk_marker_warning.pixel_data);

  cdk_sci_send (sci, SCI_RGBAIMAGESETWIDTH, cdk_marker_error.width, 0);
  cdk_sci_send (sci, SCI_RGBAIMAGESETHEIGHT, cdk_marker_error.height, 0);
  cdk_sci_send (sci, SCI_MARKERDEFINERGBAIMAGE, CDK_DIAGNOSTICS_MARKER_ERROR, cdk_marker_error.pixel_data);

  self->priv->sci_notify_hnd =
    g_signal_connect_swapped (sci, "sci-notify", G_CALLBACK (cdk_diagnostics_sci_notify), self);
}

static void
cdk_diagnostics_deinitialize_document (CdkDiagnostics *self,
                                       GeanyDocument *document)
{
  ScintillaObject *sci = document->editor->sci;

  if (self->priv->sci_notify_hnd > 0)
    {
      g_signal_handler_disconnect (sci, self->priv->sci_notify_hnd);
      self->priv->sci_notify_hnd = 0;
    }

  cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_DIAGNOSTICS_INDIC_WARNING, self->priv->prev_w_indic_style);
  cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_WARNING, self->priv->prev_w_indic_fore);
  cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_DIAGNOSTICS_INDIC_ERROR, self->priv->prev_e_indic_style);
  cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_ERROR, self->priv->prev_e_indic_fore);
}

static inline void
cdk_diagnostics_clear_indicator (G_GNUC_UNUSED CdkDiagnostics *self,
                                 GeanyDocument *document,
                                 gint indic)
{
  ScintillaObject *sci = document->editor->sci;
  cdk_sci_send (sci, SCI_SETINDICATORCURRENT, indic, 0);
  cdk_sci_send (sci, SCI_INDICATORCLEARRANGE, 0, cdk_document_get_length (document));
}

static void
cdk_diagnostics_clear_indicators (CdkDiagnostics *self,
                                  GeanyDocument  *document)
{
  cdk_diagnostics_clear_indicator (self, document, CDK_DIAGNOSTICS_INDIC_WARNING);
  cdk_diagnostics_clear_indicator (self, document, CDK_DIAGNOSTICS_INDIC_ERROR);
}

static void
cdk_diagnostics_clear_markers (G_GNUC_UNUSED CdkDiagnostics *self,
                               GeanyDocument *document)
{
  ScintillaObject *sci = document->editor->sci;
  cdk_sci_send (sci, SCI_MARKERDELETEALL, CDK_DIAGNOSTICS_MARKER_WARNING, 0);
  cdk_sci_send (sci, SCI_MARKERDELETEALL, CDK_DIAGNOSTICS_MARKER_ERROR, 0);
}

static void
cdk_diagnostics_clear_compiler_messages (G_GNUC_UNUSED CdkDiagnostics *self)
{
  msgwin_clear_tab (MSG_COMPILER);
}

static void
cdk_diagnostics_set_compiler_message (CdkDiagnostics *self,
                                      CXDiagnostic diag)
{
  CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
  GeanyDocument *document = cdk_document_helper_get_document (helper);
  CXString text = clang_getDiagnosticSpelling (diag);
  CXString option = clang_getDiagnosticOption (diag, NULL);
  CXSourceLocation locn = clang_getDiagnosticLocation (diag);
  guint line = 0, column = 0;
  clang_getSpellingLocation (locn, NULL, &line, &column, NULL);

  // FIXME: In order to get Geany compiler tab working with mouse-click
  // we have to fake it out by putting Make-like entering/leaving directory
  // messages so Geany can find the file (it doesn't support absolute
  // paths, and msgwin_set_messages_dir() doesn't work).
  gchar *dir = g_path_get_dirname (document->real_path);
  msgwin_compiler_add (COLOR_BLACK, "make[1]: Entering directory `%s'", dir);

  gchar *docname = g_path_get_basename (document->real_path);

  if (strlen (clang_getCString (option)) == 0)
    {
      msgwin_compiler_add (COLOR_RED,
                           "%s:%u:%u: %s",
                           docname,
                           line, column,
                           clang_getCString (text));
    }
  else
    {
      msgwin_compiler_add (COLOR_RED,
                           "%s:%u:%u: %s [%s]",
                           docname,
                           line, column,
                           clang_getCString (text),
                           clang_getCString (option));
    }

  msgwin_compiler_add (COLOR_BLACK, "make[1]: Leaving directory `%s'", dir);

  g_free (dir);
  g_free (docname);

  clang_disposeString (text);
  clang_disposeString (option);

}

static gboolean
cdk_diagnostics_apply_each_compiler_message (CdkDiagnostics *self,
                                             CXDiagnostic diag,
                                             G_GNUC_UNUSED guint position,
                                             G_GNUC_UNUSED gpointer user_data)
{
  cdk_diagnostics_set_compiler_message (self, diag);
  return TRUE;
}

static gint
cdk_diagnostics_set_marker (CdkDiagnostics *self,
                            GeanyDocument *document,
                            gint line,
                            gint marker)
{
  ScintillaObject *sci = document->editor->sci;
  if (! self->priv->markers_enabled || line <= 0)
    return -1;
  line--; // libclang uses 1-based line, scintilla 0-based
  sptr_t marker_handle = cdk_sci_send (sci, SCI_MARKERADD, line, marker);
  if (marker_handle == -1)
    {
      g_warning ("failed to add marker '%d' to line '%d'", line, marker);
      return -1;
    }
  return marker_handle;
}

static void
cdk_diagnostics_trim_range (ScintillaObject *sci,
                            gint *start_ptr,
                            gint *end_ptr)
{
  gint length = cdk_sci_send (sci, SCI_GETLENGTH, 0, 0);
  gint start = *start_ptr;
  gint end = *end_ptr;
  while (start < length && isspace (cdk_sci_send (sci, SCI_GETCHARAT, start, 0)))
    start++;
  while (end > start && isspace (cdk_sci_send (sci, SCI_GETCHARAT, end, 0)))
    end--;
  *start_ptr = start;
  *end_ptr = end;
}

static void
cdk_diagnostics_set_indicator (CdkDiagnostics *self,
                               GeanyDocument *document,
                               gint indic,
                               gint start,
                               gint end)
{
  ScintillaObject *sci = document->editor->sci;
  if (! self->priv->indicators_enabled)
    return;
  if (start == end)
    {
      start = cdk_sci_send (sci, SCI_WORDSTARTPOSITION, start, TRUE);
      end = cdk_sci_send (sci, SCI_WORDENDPOSITION, start, TRUE);
    }
  cdk_diagnostics_trim_range (sci, &start, &end);
  cdk_sci_send (sci, SCI_SETINDICATORCURRENT, indic, 0);
  cdk_sci_send (sci, SCI_INDICATORFILLRANGE, start, (end - start) + 1);
}

gint
cdk_diagnostics_foreach (CdkDiagnostics *self,
                         CdkDiagnosticFunc func,
                         gpointer user_data)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), -1);
  g_return_val_if_fail (func, -1);

  CdkDocumentHelper *helper = CDK_DOCUMENT_HELPER (self);
  CdkPlugin *plugin = cdk_document_helper_get_plugin (helper);
  GeanyDocument *document = cdk_document_helper_get_document (helper);
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (plugin, document);
  if (tu == NULL)
    return -1;

  gint cnt = 0;
  guint n_diags = clang_getNumDiagnostics (tu);
  if (n_diags == 0)
    return 0;

  for (guint i = 0; i < n_diags; i++)
    {
      cnt++;

      CXDiagnostic diag = clang_getDiagnostic (tu, i);
      CXSourceLocation locn = clang_getDiagnosticLocation (diag);
      guint position = 0;
      clang_getSpellingLocation (locn, NULL, NULL, NULL, &position);

      if (! func (self, diag, position, user_data))
        break;
    }

  return cnt;
}

static gboolean
cdk_diagnostics_range_iter (CdkDiagnostics *self,
                            gpointer diag,
                            G_GNUC_UNUSED guint position,
                            gpointer user_data)
{
  struct CdkDiagnosticsRangeData *data = user_data;

  guint n_ranges = clang_getDiagnosticNumRanges (diag);
  for (guint i = 0; i < n_ranges; i++)
    {
      data->counter++;

      CXSourceRange range = clang_getDiagnosticRange (diag, i);
      CXSourceLocation start_locn = clang_getRangeStart (range);
      CXSourceLocation end_locn = clang_getRangeEnd (range);
      guint start_pos = 0, end_pos = 0;
      clang_getSpellingLocation (start_locn, NULL, NULL, NULL, &start_pos);
      clang_getSpellingLocation (end_locn, NULL, NULL, NULL, &end_pos);

      if (! data->func (self, diag, i, start_pos, end_pos, data->user_data))
        break;
    }

  return TRUE;
}

gint
cdk_diagnostics_foreach_range (CdkDiagnostics *self,
                               CdkDiagnosticRangeFunc func,
                               gpointer user_data)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), -1);
  g_return_val_if_fail (func, -1);

  struct CdkDiagnosticsRangeData data;
  data.func = func;
  data.user_data = user_data;
  data.counter = 0;

  cdk_diagnostics_foreach (self, cdk_diagnostics_range_iter, &data);

  return data.counter;
}

static gboolean
cdk_diagnostics_apply_each_indicator (CdkDiagnostics *self,
                                      CXDiagnostic diag,
                                      G_GNUC_UNUSED guint index,
                                      guint start,
                                      guint end,
                                      gpointer document)
{
  enum CXDiagnosticSeverity severity = clang_getDiagnosticSeverity (diag);
  gint indic = 0;

  switch (severity)
    {
    case CXDiagnostic_Warning:
      indic = CDK_DIAGNOSTICS_INDIC_WARNING;
      break;
    case CXDiagnostic_Error:
    case CXDiagnostic_Fatal:
      indic = CDK_DIAGNOSTICS_INDIC_ERROR;
      break;
    default:
      return TRUE;
    }

  cdk_diagnostics_set_indicator (self, document, indic, start, end);

  return TRUE;
}

static gboolean
cdk_diagnostics_apply_each_marker (CdkDiagnostics *self,
                                   CXDiagnostic diag,
                                   guint position,
                                   gpointer document)
{
  enum CXDiagnosticSeverity severity = clang_getDiagnosticSeverity (diag);
  gint marker = 0;

  switch (severity)
    {
    case CXDiagnostic_Warning:
      marker = CDK_DIAGNOSTICS_MARKER_WARNING;
      break;
    case CXDiagnostic_Error:
    case CXDiagnostic_Fatal:
      marker = CDK_DIAGNOSTICS_MARKER_ERROR;
      break;
    default:
      return TRUE;
    }

  GeanyDocument *doc = document;
  gint line = cdk_sci_send (doc->editor->sci, SCI_LINEFROMPOSITION, position, 0);
  cdk_diagnostics_set_marker (self, doc, line + 1, marker);

  return TRUE;
}

static void
cdk_diagnostics_updated (CdkDocumentHelper *object,
                         GeanyDocument *document)
{
  CdkDiagnostics *self = CDK_DIAGNOSTICS (object);

  if (self->priv->indicators_enabled)
    {
      cdk_diagnostics_clear_indicators (self, document);
      cdk_diagnostics_foreach_range (self, cdk_diagnostics_apply_each_indicator, document);
    }

  if (self->priv->markers_enabled)
    {
      cdk_diagnostics_clear_markers (self, document);
      cdk_diagnostics_clear_annotations (self, document);
      cdk_diagnostics_foreach (self, cdk_diagnostics_apply_each_marker, document);
    }

  if (self->priv->compiler_messages_enabled)
    {
      cdk_diagnostics_clear_compiler_messages (self);
      cdk_diagnostics_foreach (self, cdk_diagnostics_apply_each_compiler_message, NULL);
      // FIXME: need to clear Geany's indicators since it draws them over
      // ours when user clicks on message in Compiler tab. This only clears
      // them after the document is updated again
      editor_indicator_clear (document->editor, GEANY_INDICATOR_ERROR);
    }
}
