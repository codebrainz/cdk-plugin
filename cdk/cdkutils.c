/*
 * Copyright (c) 2015, Matthew Brush <mbrush@codebrainz.ca>
 * All rights reserved. See the COPYING file for full license.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cdk/cdkutils.h>
#include <cdk/cdkstyle.h>
#include <geanyplugin.h>
#include <stdlib.h>
#include <errno.h>
#include <pwd.h>

const gchar *
cdk_document_get_contents (struct GeanyDocument *doc)
{
  return (const gchar *)
    scintilla_send_message (doc->editor->sci, SCI_GETCHARACTERPOINTER, 0, 0);
}

gsize
cdk_document_get_length (struct GeanyDocument *doc)
{
  return scintilla_send_message (doc->editor->sci, SCI_GETLENGTH, 0, 0);
}

void
cdk_scintilla_set_style (struct _ScintillaObject *sci, guint id, const struct CdkStyle *style)
{
  g_return_if_fail (IS_SCINTILLA (sci));
  g_return_if_fail (style != NULL);

  cdk_sci_send (sci, SCI_STYLESETBACK, id, style->back);
  cdk_sci_send (sci, SCI_STYLESETFORE, id, style->fore);
  cdk_sci_send (sci, SCI_STYLESETBOLD, id, style->bold);
  cdk_sci_send (sci, SCI_STYLESETITALIC, id, style->italic);
  if (style->font != NULL)
    cdk_sci_send (sci, SCI_STYLESETFONT, id, style->font);
  if (style->size > 0)
    cdk_sci_send (sci, SCI_STYLESETSIZE, id, style->size);
}

gchar *
cdk_sci_get_current_word (struct _ScintillaObject *sci)
{
  g_return_val_if_fail (IS_SCINTILLA (sci), NULL);

  gint cur_pos = cdk_sci_send (sci, SCI_GETCURRENTPOS, 0, 0);
  gint word_start = cdk_sci_send (sci, SCI_WORDSTARTPOSITION, cur_pos, TRUE);
  gint word_end = cdk_sci_send (sci, SCI_WORDENDPOSITION, cur_pos, TRUE);

  struct Sci_TextRange tr;
  tr.chrg.cpMin = word_start;
  tr.chrg.cpMax = word_end;
  tr.lpstrText = g_malloc0 ((word_end - word_start) + 1);

  cdk_sci_send (sci, SCI_GETTEXTRANGE, 0, &tr);

  return tr.lpstrText;
}

static gchar *
cdk_expand_tilde (const gchar *path)
{
  if (path == NULL)
    return NULL;

  if (path[0] == '\0')
    return g_strdup (path);

  if (path[0] == '~')
    {
      if (path[1] == G_DIR_SEPARATOR || path[1] == '\0') // ~/foo or ~
        return g_strconcat (g_get_home_dir (), path+1, NULL);
      else // ~username
        {
          const gchar *first_slash = strchr (path+1, G_DIR_SEPARATOR);
          if (first_slash != NULL)
            {
              const gchar *user_start = path + 1;
              guint user_len = first_slash - user_start;
              gchar *user = g_malloc0 (user_len + 1);
              strncpy (user, user_start, user_len);
              struct passwd *pw = getpwnam(user);
              g_free (user);
              errno = 0;
              if (pw != NULL && pw->pw_dir != NULL)
                return g_strconcat (pw->pw_dir, first_slash, NULL);
              if (errno != 0)
                g_critical ("can't find user info (%d): %s", errno, strerror (errno));
            }
        }
    }

  return g_strdup (path);
}

gchar *
cdk_abspath (const gchar *path)
{
  g_return_val_if_fail (path != NULL, NULL);
  gchar *exp_path = cdk_expand_tilde (path);
  if (exp_path == NULL)
    return NULL;
  gchar buffer[PATH_MAX+1] = {0};
  gchar *result = NULL;
  errno = 0;
  if (realpath (exp_path, buffer) != NULL)
    result = g_strdup (buffer);
  if (errno != 0)
    g_critical ("realpath failed for '%s' (%d): %s", exp_path, errno, strerror (errno));
  g_free (exp_path);
  return result;
}

gchar *
cdk_relpath (const gchar *path, const gchar *rel_dir)
{
  g_return_val_if_fail (path != NULL, NULL);

  gchar *abs_path = cdk_abspath (path);
  if (abs_path == NULL)
    return NULL;

  gchar *abs_dir = NULL;
  if (rel_dir != NULL)
    abs_dir = cdk_abspath (rel_dir);
  else
    {
      gchar *cwd = g_get_current_dir ();
      abs_dir = cdk_abspath (cwd);
      g_free (cwd);
    }
  if (abs_dir == NULL)
    {
      g_free (abs_path);
      return NULL;
    }

  gchar **path_parts = g_strsplit (abs_path, G_DIR_SEPARATOR_S, 0);
  gchar **dir_parts = g_strsplit (abs_dir, G_DIR_SEPARATOR_S, 0);

  guint n_common = 0;
  for (gchar **it_path = path_parts, **it_dir = dir_parts;
       *it_path != NULL && *it_dir != NULL;
       it_path++, it_dir++)
    {
      if (g_strcmp0 (*it_path, *it_dir) == 0)
        n_common++;
      else
        break;
    }

  GPtrArray *res_arr = g_ptr_array_new ();
  guint n_dirs = g_strv_length (dir_parts);
  guint n_dots = n_dirs - n_common;
  for (guint i=0; i < n_dots; i++)
      g_ptr_array_add (res_arr, "..");

  guint n_paths = g_strv_length (path_parts);
  for (guint i=n_common; i < n_paths; i++)
      g_ptr_array_add (res_arr, path_parts[i]);

  g_ptr_array_add (res_arr, NULL);

  gchar *result = g_strjoinv (G_DIR_SEPARATOR_S, (gchar**) res_arr->pdata);

  g_strfreev (path_parts);
  g_strfreev (dir_parts);
  g_ptr_array_free (res_arr, TRUE);

  return result;
}

gchar **
cdk_relpaths (const gchar *const *paths, const gchar *rel_dir)
{
  gchar **cpaths = (gchar**) paths;
  guint count = g_strv_length (cpaths);
  gchar **rpaths = g_malloc0 ((count + 1) * sizeof (gchar*));
  for (guint i=0; i < count; i++)
    {
      gchar *pth = cdk_relpath (cpaths[i], rel_dir);
      if (pth != NULL)
        rpaths[i] = pth;
      else
        rpaths[i] = g_strdup (cpaths[i]);
    }
  rpaths[count] = NULL;
  return rpaths;
}
