/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#include <cdk/cdkdocumenthelper.h>
#include <cdk/cdkplugin.h>
#include <geanyplugin.h>
#include <clang-c/Index.h>

struct CdkDocumentHelperPrivate_
{
  CdkPlugin     *plugin;
  GeanyDocument *document;
};

enum
{
  PROP_0,
  PROP_PLUGIN,
  PROP_DOCUMENT,
  NUM_PROPERTIES,
};

enum
{
  SIG_UPDATED,
  NUM_SIGNALS,
};

static GParamSpec *cdk_document_helper_properties[NUM_PROPERTIES] = { NULL };
static gulong cdk_document_helper_signals[NUM_SIGNALS] = { 0 };

static void cdk_document_helper_finalize (GObject *object);
static void cdk_document_helper_set_property (GObject *object,
                                              guint prop_id,
                                              const GValue *value,
                                              GParamSpec *pspec);
static void cdk_document_helper_get_property (GObject *object,
                                              guint prop_id,
                                              GValue *value,
                                              GParamSpec *pspec);

G_DEFINE_TYPE (CdkDocumentHelper, cdk_document_helper, G_TYPE_OBJECT)

static void
cdk_document_helper_constructed (GObject *object)
{
  G_OBJECT_CLASS (cdk_document_helper_parent_class)->constructed (object);
  CdkDocumentHelperClass *klass = CDK_DOCUMENT_HELPER_GET_CLASS (object);
  if (klass->initialize)
    {
      CdkDocumentHelper *self = CDK_DOCUMENT_HELPER (object);
      klass->initialize (self, self->priv->document);
    }
}

static void
cdk_document_helper_class_init (CdkDocumentHelperClass *klass)
{
  GObjectClass *g_object_class = G_OBJECT_CLASS (klass);

  g_object_class->constructed = cdk_document_helper_constructed;
  g_object_class->finalize = cdk_document_helper_finalize;
  g_object_class->set_property = cdk_document_helper_set_property;
  g_object_class->get_property = cdk_document_helper_get_property;

  cdk_document_helper_properties[PROP_PLUGIN] =
    g_param_spec_object ("plugin",
                         "Plugin",
                         "The owning CdkPlugin",
                         CDK_TYPE_PLUGIN,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  cdk_document_helper_properties[PROP_DOCUMENT] =
    g_param_spec_pointer ("document",
                          "Document",
                          "The related GeanyDocument",
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  g_object_class_install_properties (g_object_class,
                                     NUM_PROPERTIES,
                                     cdk_document_helper_properties);

  cdk_document_helper_signals[SIG_UPDATED] =
    g_signal_new ("updated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  g_type_class_add_private ((gpointer)klass, sizeof (CdkDocumentHelperPrivate));
}

static void
cdk_document_helper_finalize (GObject *object)
{
  CdkDocumentHelper *self;
  g_return_if_fail (CDK_IS_DOCUMENT_HELPER (object));
  self = CDK_DOCUMENT_HELPER (object);
  G_OBJECT_CLASS (cdk_document_helper_parent_class)->finalize (object);
}

static void
cdk_document_helper_init (CdkDocumentHelper *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CDK_TYPE_DOCUMENT_HELPER, CdkDocumentHelperPrivate);
}

static void
cdk_document_helper_set_property (GObject *object,
                                  guint prop_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  CdkDocumentHelper *self = CDK_DOCUMENT_HELPER (object);

  switch (prop_id)
    {
    case PROP_PLUGIN:
      {
        CdkPlugin *plugin = g_value_get_object (value);
        if (plugin != self->priv->plugin)
          {
            self->priv->plugin = plugin;
            g_object_notify (G_OBJECT (object), "plugin");
          }
      }
      break;
    case PROP_DOCUMENT:
      {
        GeanyDocument *doc = g_value_get_pointer (value);
        g_return_if_fail (doc == NULL || DOC_VALID (doc));
        if (doc != self->priv->document)
          {
            self->priv->document = doc;
            g_object_notify (G_OBJECT (object), "document");
          }
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cdk_document_helper_get_property (GObject *object,
                                  guint prop_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  CdkDocumentHelper *self = CDK_DOCUMENT_HELPER (object);

  switch (prop_id)
    {
    case PROP_PLUGIN:
      g_value_set_object (value, self->priv->plugin);
      break;
    case PROP_DOCUMENT:
      g_value_set_pointer (value, self->priv->document);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

struct CdkPlugin_ *
cdk_document_helper_get_plugin (CdkDocumentHelper *self)
{
  g_return_val_if_fail (CDK_IS_DOCUMENT_HELPER (self), NULL);
  return self->priv->plugin;
}

struct GeanyDocument *
cdk_document_helper_get_document (CdkDocumentHelper *self)
{
  g_return_val_if_fail (CDK_IS_DOCUMENT_HELPER (self), NULL);
  return self->priv->document;
}

void
cdk_document_helper_updated (CdkDocumentHelper *self)
{
  g_return_if_fail (CDK_IS_DOCUMENT_HELPER (self));
  CdkDocumentHelperClass *klass = CDK_DOCUMENT_HELPER_GET_CLASS (self);
  if (klass->updated)
    klass->updated (self, self->priv->document);
  g_signal_emit_by_name (self, "updated", self->priv->document);
}
