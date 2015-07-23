/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifndef CDKHIGHLIGHTER_H_
#define CDKHIGHLIGHTER_H_ 1

#include <cdk/cdkdocumenthelper.h>
#include <cdk/cdkstylescheme.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CDK_TYPE_HIGHLIGHTER            (cdk_highlighter_get_type ())
#define CDK_HIGHLIGHTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CDK_TYPE_HIGHLIGHTER, CdkHighlighter))
#define CDK_HIGHLIGHTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CDK_TYPE_HIGHLIGHTER, CdkHighlighterClass))
#define CDK_IS_HIGHLIGHTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CDK_TYPE_HIGHLIGHTER))
#define CDK_IS_HIGHLIGHTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CDK_TYPE_HIGHLIGHTER))
#define CDK_HIGHLIGHTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CDK_TYPE_HIGHLIGHTER, CdkHighlighterClass))

typedef struct CdkHighlighter_        CdkHighlighter;
typedef struct CdkHighlighterClass_   CdkHighlighterClass;
typedef struct CdkHighlighterPrivate_ CdkHighlighterPrivate;

struct CdkHighlighter_
{
  CdkDocumentHelper parent;
  CdkHighlighterPrivate *priv;
};

struct CdkHighlighterClass_
{
  CdkDocumentHelperClass parent_class;
};

GType cdk_highlighter_get_type (void);
CdkHighlighter *cdk_highlighter_new (struct CdkPlugin_ *plugin, struct GeanyDocument *doc);
gboolean cdk_highlighter_highlight (CdkHighlighter *self, gint start_pos, gint end_pos);
gboolean cdk_highlighter_highlight_all (CdkHighlighter *self);
void cdk_highlighter_queue_highlight (CdkHighlighter *self, gint start_pos, gint end_pos);
CdkStyleScheme *cdk_highlighter_get_style_scheme (CdkHighlighter *self);
void cdk_highlighter_set_style_scheme (CdkHighlighter *self, CdkStyleScheme *scheme);

G_END_DECLS

#endif /* CDKHIGHLIGHTER_H_ */
