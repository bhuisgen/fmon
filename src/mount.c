/*
 * fmon - a file monitoring tool
 *
 * Copyright 2011 Boris HUISGEN <bhuisgen@hbis.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "fmon.h"
#include "mount.h"
#include "watcher.h"

void
mount_create()
{
  LOG_DEBUG("%s", "creating UNIX mount monitor");

  app->mount = g_unix_mount_monitor_new();
  app->mounts = g_unix_mounts_get(NULL);

  g_signal_connect(app->mount, "mounts-changed", G_CALLBACK(mount_event),
      NULL);
}

void
mount_destroy()
{
  GUnixMountEntry *entry;
  GList *item;

  LOG_DEBUG("%s", "destroying UNIX mount monitor");

  if (app->mount)
    {
      g_object_unref(app->mount);
    }

  for (item = app->mounts; item; item = item->next)
    {
      entry = (GUnixMountEntry *) item->data;

      g_unix_mount_free(entry);
    }

  g_list_free(app->mounts);
}

void
mount_event(GUnixMountMonitor *monitor, gpointer user_data)
{
  GFile *m_file, *w_file, *parent, *top, *tmp;
  GUnixMountEntry *mount1, *mount2;
  GList *mounts, *item1, *item2;
  GSList *item3;
  GHashTableIter iter;
  gpointer key, value;
  const gchar *mountpath;
  gchar *path;
  gboolean found, matched;
  gint depth = 1;
  watcher_t *watcher;
  watcher_event_t *event;

  LOG_DEBUG("%s: %s", "mount", N_("mount event received"));

  mounts = g_unix_mounts_get(NULL);

  for (item1 = app->mounts, found = FALSE, matched = FALSE; item1;
      item1 = item1->next, found = FALSE, matched = FALSE)
    {
      mount1 = (GUnixMountEntry *) item1->data;

      for (item2 = mounts; item2; item2 = item2->next)
        {
          mount2 = (GUnixMountEntry *) item2->data;

          if (g_unix_mount_compare(mount1, mount2) == 0)
            {
              found = TRUE;

              break;
            }
        }

      if (!found)
        {
          mountpath = g_unix_mount_get_mount_path(mount1);

          LOG_INFO("%s: %s '%s'", "mount", N_("path unmounted"), mountpath);

          m_file = g_file_new_for_path(mountpath);
          if (!g_file_has_parent(m_file, NULL))
            {
              LOG_DEBUG("%s: %s", "mount", N_("path has no parent"));

              g_object_unref(m_file);

              continue;
            }

          for (item3 = app->watchers; item3; item3 = item3->next)
            {
              watcher = (watcher_t *) item3->data;

              w_file = g_file_new_for_path(watcher->path);
              if (g_file_equal(m_file, w_file))
                {
                  LOG_DEBUG("%s: %s (%s)",
                      watcher->name, N_("path matches"), mountpath);

                  matched = TRUE;
                }
              else if (!g_file_has_prefix(m_file, w_file))
                {
                  LOG_DEBUG("%s: %s",
                      watcher->name, N_("path has not the same prefix"));

                  g_object_unref(w_file);

                  continue;
                }

              if (!matched && watcher->recursive)
                {
                  g_hash_table_iter_init(&iter, watcher->monitors);
                  while (g_hash_table_iter_next(&iter, &key, &value))
                    {
                      path = (gchar *) key;

                      w_file = g_file_new_for_path(path);

                      if (g_file_equal(w_file, m_file))
                        {
                          matched = TRUE;

                          LOG_DEBUG("%s: %s (%s)",
                              watcher->name, N_("path matches"), mountpath);

                          break;
                        }
                    }
                }

              if (matched)
                {
                  parent = g_file_new_for_path(watcher->path);

                  event = (watcher_event_t *) g_new0(watcher_event_t, 1);
                  event->watcher = watcher;
                  event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_UNMOUNTED);
                  event->file = g_file_get_path(m_file);
                  event->rfile = g_file_get_relative_path(parent, m_file);

                  if (watcher->recursive)
                    {
                      for (top = g_file_dup(m_file), depth = 1;
                          !g_file_equal(top, parent)
                                  && !g_file_has_parent(top, parent);
                          top = tmp, depth++)
                        {
                          tmp = g_file_get_parent(top);

                          g_object_unref(top);
                        }

                      g_object_unref(top);

                      LOG_DEBUG("%s: file depth to watcher path is '%d'",
                          watcher->name, depth);
                    }

                  g_object_unref(parent);

                  if (watcher->recursive)
                    {
                      watcher_remove_monitor_for_recursive_path(watcher,
                          event->file);
                      watcher_add_monitor_for_recursive_path(watcher,
                          event->file, depth);
                    }
                  else
                    {
                      watcher_remove_monitor_for_path(watcher, watcher->path);
                      watcher_add_monitor_for_path(watcher, watcher->path);
                    }

                  LOG_INFO("%s: %s", watcher->name, N_("watcher updated"));

                  if (!watcher_event_test(watcher, event))
                    {
                      LOG_DEBUG("%s: %s (event=%s, file=%s)",
                          watcher->name, N_("event ignored"), event->event, event->file);

                      g_free(event->event);
                      g_free(event->file);
                      g_free(event->rfile);
                      g_free(event);

                      return;
                    }

                  watcher_event_fired(watcher, event);

                  g_free(event->event);
                  g_free(event->file);
                  g_free(event->rfile);
                  g_free(event);
                }

              g_object_unref(w_file);
            }

          g_object_unref(m_file);
        }
    }

  for (item1 = mounts, found = FALSE, matched = FALSE; item1;
      item1 = item1->next, found = FALSE, matched = FALSE)
    {
      mount1 = (GUnixMountEntry *) item1->data;

      for (item2 = app->mounts; item2; item2 = item2->next)
        {
          mount2 = (GUnixMountEntry *) item2->data;

          if (g_unix_mount_compare(mount1, mount2) == 0)
            {
              found = TRUE;

              break;
            }
        }

      if (!found)
        {
          mountpath = g_unix_mount_get_mount_path(mount1);

          LOG_INFO("%s: %s '%s'", "mount", N_("path mounted"), mountpath);

          m_file = g_file_new_for_path(mountpath);
          if (!g_file_has_parent(m_file, NULL))
            {
              LOG_DEBUG("%s: %s", "mount", N_("path has no parent"));

              g_object_unref(m_file);

              continue;
            }

          for (item3 = app->watchers; item3; item3 = item3->next)
            {
              watcher = (watcher_t *) item3->data;

              w_file = g_file_new_for_path(watcher->path);
              if (g_file_equal(m_file, w_file))
                {
                  LOG_DEBUG("%s: %s (%s)",
                      watcher->name, N_("path matches"), mountpath);

                  matched = TRUE;
                }
              else if (!g_file_has_prefix(m_file, w_file))
                {
                  LOG_DEBUG("%s: %s",
                      watcher->name, N_("path has not the same prefix"));

                  g_object_unref(w_file);

                  continue;
                }

              if (!matched && watcher->recursive)
                {
                  g_hash_table_iter_init(&iter, watcher->monitors);
                  while (g_hash_table_iter_next(&iter, &key, &value))
                    {
                      path = (gchar *) key;

                      w_file = g_file_new_for_path(path);

                      if (g_file_equal(w_file, m_file))
                        {
                          matched = TRUE;

                          LOG_DEBUG("%s: %s (%s)",
                              watcher->name, N_("path matches"), mountpath);

                          break;
                        }
                    }
                }

              if (matched)
                {
                  parent = g_file_new_for_path(watcher->path);

                  event = (watcher_event_t *) g_new0(watcher_event_t, 1);
                  event->watcher = watcher;
                  event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_MOUNTED);
                  event->file = g_file_get_path(m_file);
                  event->rfile = g_file_get_relative_path(parent, m_file);

                  if (watcher->recursive)
                    {
                      depth = 1;

                      for (top = g_file_dup(m_file), depth = 1;
                          !g_file_equal(top, parent)
                                  && !g_file_has_parent(top, parent);
                          top = tmp, depth++)
                        {
                          tmp = g_file_get_parent(top);

                          g_object_unref(top);
                        }

                      g_object_unref(top);

                      LOG_DEBUG("%s: file depth to watcher path is '%d'",
                          watcher->name, depth);
                    }

                  g_object_unref(parent);

                  if (watcher->recursive)
                    {
                      watcher_remove_monitor_for_recursive_path(watcher,
                          event->file);
                      watcher_add_monitor_for_recursive_path(watcher,
                          event->file, depth);
                    }
                  else
                    {
                      watcher_remove_monitor_for_path(watcher, watcher->path);
                      watcher_add_monitor_for_path(watcher, watcher->path);
                    }

                  LOG_INFO("%s: %s", watcher->name, N_("watcher updated"));

                  if (!watcher_event_test(watcher, event))
                    {
                      LOG_DEBUG("%s: %s (event=%s, file=%s)",
                          watcher->name, N_("event ignored"), event->event, event->file);

                      g_free(event->event);
                      g_free(event->file);
                      g_free(event->rfile);
                      g_free(event);

                      return;
                    }

                  watcher_event_fired(watcher, event);

                  g_free(event->event);
                  g_free(event->file);
                  g_free(event->rfile);
                  g_free(event);
                }

              g_object_unref(w_file);
            }

          g_object_unref(m_file);
        }
    }

  for (item1 = app->mounts; item1; item1 = item1->next)
    {
      mount1 = (GUnixMountEntry *) item1->data;

      g_unix_mount_free(mount1);
    }

  app->mounts = mounts;
}

