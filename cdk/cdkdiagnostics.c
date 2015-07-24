/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#include <cdk/cdkdiagnostics.h>
#include <cdk/cdkplugin.h>
#include <cdk/cdkutils.h>
#include <geanyplugin.h>
#include <clang-c/Index.h>
#include <ctype.h>

#include "error.xpm"
#include "warning.xpm"

#define CDK_DIAGNOSTICS_INDIC_WARNING INDIC_CONTAINER
#define CDK_DIAGNOSTICS_INDIC_ERROR   INDIC_CONTAINER+1
#define CDK_DIAGNOSTICS_MARKER_WARNING 23
#define CDK_DIAGNOSTICS_MARKER_ERROR   24

struct CdkDiagnosticsPrivate_
{
  gboolean indicators_enabled;
  gboolean markers_enabled;
  CdkStyleScheme *scheme;
  sptr_t prev_w_indic_style;
  sptr_t prev_w_indic_fore;
  sptr_t prev_e_indic_style;
  sptr_t prev_e_indic_fore;
};

enum
{
  PROP_0,
  PROP_SCHEME,
  PROP_INDICATORS_ENABLED,
  PROP_MARKERS_ENABLED,
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

  cdk_diagnostics_properties[PROP_SCHEME] =
    g_param_spec_object ("style-scheme",
                         "StyleScheme",
                         "The CdkStyleScheme to use for diagnostics styles",
                         CDK_TYPE_STYLE_SCHEME,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_diagnostics_properties[PROP_INDICATORS_ENABLED] =
    g_param_spec_boolean ("indicators-enabled",
                          "IndicatorsEnabled",
                          "Whether to draw squiggly lines in the editor",
                          TRUE,
                          G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  cdk_diagnostics_properties[PROP_MARKERS_ENABLED] =
    g_param_spec_boolean ("markers-enabled",
                          "MarkersEnabled",
                          "Whether to draw icons in the editor margin",
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

CdkDiagnostics *
cdk_diagnostics_new (struct CdkPlugin_ *plugin,
                     struct GeanyDocument *doc)
{
  return g_object_new (CDK_TYPE_DIAGNOSTICS, "plugin", plugin, "document", doc, NULL);
}

CdkStyleScheme *
cdk_diagnostics_get_style_scheme (CdkDiagnostics *self)
{
  g_return_val_if_fail (CDK_IS_DIAGNOSTICS (self), NULL);
  return self->priv->scheme;
}

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

  cdk_sci_send (sci, SCI_MARKERDEFINEPIXMAP, CDK_DIAGNOSTICS_MARKER_WARNING, warning_xpm);
  cdk_sci_send (sci, SCI_MARKERDEFINEPIXMAP, CDK_DIAGNOSTICS_MARKER_ERROR, error_xpm);
}

static void
cdk_diagnostics_deinitialize_document (CdkDiagnostics *self,
                                       GeanyDocument *document)
{
  ScintillaObject *sci = document->editor->sci;

  cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_DIAGNOSTICS_INDIC_WARNING, self->priv->prev_w_indic_style);
  cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_WARNING, self->priv->prev_w_indic_fore);
  cdk_sci_send (sci, SCI_INDICSETSTYLE, CDK_DIAGNOSTICS_INDIC_ERROR, self->priv->prev_e_indic_style);
  cdk_sci_send (sci, SCI_INDICSETFORE, CDK_DIAGNOSTICS_INDIC_ERROR, self->priv->prev_e_indic_fore);
}

static inline void
cdk_diagnostics_clear_indicator (CdkDiagnostics *self,
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
cdk_diagnostics_clear_markers (CdkDiagnostics *self,
                               GeanyDocument *document)
{
  ScintillaObject *sci = document->editor->sci;
  cdk_sci_send (sci, SCI_MARKERDELETEALL, CDK_DIAGNOSTICS_MARKER_WARNING, 0);
  cdk_sci_send (sci, SCI_MARKERDELETEALL, CDK_DIAGNOSTICS_MARKER_ERROR, 0);
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
  cdk_diagnostics_trim_range (sci, &start, &end);
  cdk_sci_send (sci, SCI_SETINDICATORCURRENT, indic, 0);
  cdk_sci_send (sci, SCI_INDICATORFILLRANGE, start, (end - start) + 1);
}

static void
cdk_diagnostics_updated (CdkDocumentHelper *object,
                         GeanyDocument *document)
{
  CdkDiagnostics *self = CDK_DIAGNOSTICS (object);
  ScintillaObject *sci = document->editor->sci;

  if (! self->priv->indicators_enabled &&
      ! self->priv->markers_enabled)
    {
      return;
    }

  cdk_diagnostics_clear_indicators (self, document);
  cdk_diagnostics_clear_markers (self, document);

  CdkPlugin *plugin = cdk_document_helper_get_plugin (object);
  CXTranslationUnit tu = cdk_plugin_get_translation_unit (plugin, document);
  if (tu == NULL)
    return;

  guint n_diag = clang_getNumDiagnostics (tu);
  for (guint i = 0; i < n_diag; i++)
    {
      CXDiagnostic diag = clang_getDiagnostic (tu, i);
      enum CXDiagnosticSeverity severity = clang_getDiagnosticSeverity (diag);
      gint indic = 0;
      gint marker = 0;
      switch (severity)
        {
          case CXDiagnostic_Warning:
            indic = CDK_DIAGNOSTICS_INDIC_WARNING;
            marker = CDK_DIAGNOSTICS_MARKER_WARNING;
            break;
          case CXDiagnostic_Error:
          case CXDiagnostic_Fatal:
            indic = CDK_DIAGNOSTICS_INDIC_ERROR;
            marker = CDK_DIAGNOSTICS_MARKER_ERROR;
            break;
          default:
            continue;
        }
      guint n_ranges = clang_getDiagnosticNumRanges (diag);

      if (n_ranges == 0)
        {
          CXSourceLocation locn = clang_getDiagnosticLocation (diag);
          guint offset = 0;
          guint line = 0;
          clang_getSpellingLocation (locn, NULL, &line, NULL, &offset);
          gint word_start = cdk_sci_send (sci, SCI_WORDSTARTPOSITION, offset, FALSE);
          gint word_end = cdk_sci_send (sci, SCI_WORDENDPOSITION, offset, FALSE);
          cdk_diagnostics_set_indicator (self, document, indic,
                                         word_start, word_end);
          cdk_diagnostics_set_marker (self, document, line, marker);
        }

      for (guint j = 0; j < n_ranges; j++)
        {
          CXSourceRange range = clang_getDiagnosticRange (diag, j);
          CXSourceLocation start_locn = clang_getRangeStart (range);
          CXSourceLocation end_locn = clang_getRangeEnd (range);
          guint start_offset = 0, end_offset = 0;
          guint line = 0;
          clang_getSpellingLocation (start_locn, NULL, &line, NULL, &start_offset);
          clang_getSpellingLocation (end_locn, NULL, NULL, NULL, &end_offset);
          cdk_diagnostics_set_indicator (self, document, indic,
                                         start_offset, end_offset);
          cdk_diagnostics_set_marker (self, document, line, marker);
        }
    }
}
