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
#include "watcher.h"

#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

gboolean
watcher_add_monitor_for_path(const watcher_t *watcher, const gchar *path)
{
  GFile *file;
  GFileMonitor *monitor;
  GError *error = NULL;

  LOG_DEBUG("%s: %s (path=%s)",
      watcher->name, N_("creating file monitor for path"), path);

  file = g_file_new_for_path(path);

  monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, &error);
  g_object_unref(file);
  if (error)
    {
      LOG_ERROR("%s: %s (path=%s)",
          watcher->name, N_("failed to create file monitor"), error->message);

      g_error_free(error);
      error = NULL;

      return FALSE;
    }

  g_hash_table_insert(watcher->monitors, g_strdup(path), monitor);

  g_signal_connect(monitor, "changed", G_CALLBACK(watcher_event),
      (gpointer)watcher);

  return TRUE;
}

gboolean
watcher_add_monitor_for_recursive_path(const watcher_t *watcher,
    const gchar *path, guint depth)
{
  GFile *file;
  GFileEnumerator *enumerator;
  GFileInfo *info;
  gchar *child_path;
  GError *error = NULL;

  if ((watcher->maxdepth > 0) && (depth > watcher->maxdepth))
    {
      LOG_DEBUG("%s: %s (depth=%d, path=%s)",
          watcher->name, N_("maximum depth of recursion reached"), depth, path);

      return TRUE;
    }

  if (!watcher_add_monitor_for_path(watcher, path))
    return FALSE;

  file = g_file_new_for_path(path);

  enumerator = g_file_enumerate_children(file, "standard::type,standard::name",
      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
  g_object_unref(file);
  if (error)
    {
      LOG_ERROR("%s: %s (error=%s)",
          watcher->name, N_("failed to create file enumerator"), error->message);

      g_error_free(error);
      error = NULL;

      return FALSE;
    }

  while ((info = g_file_enumerator_next_file(enumerator, NULL, &error)) != NULL)
    {
      if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY)
        {
          child_path = g_build_path(G_DIR_SEPARATOR_S, path,
              g_file_info_get_name(info), NULL);

          if (!watcher_add_monitor_for_recursive_path(watcher, child_path,
              depth + 1))
            {
              g_free(child_path);
              g_object_unref(info);
              g_object_unref(enumerator);

              return FALSE;
            }

          g_free(child_path);
        }

      g_object_unref(info);
    }

  g_object_unref(enumerator);
  if (error)
    {
      LOG_DEBUG("%s: %s (%s)",
          watcher->name, N_("failed to iterate during file enumeration"), error->message);

      g_error_free(error);
      error = NULL;

      return FALSE;
    }

  return TRUE;
}

void
watcher_remove_monitor_for_path(const watcher_t *watcher, const gchar *path)
{
  GFileMonitor *w_monitor;
  gchar *w_path;
  GHashTableIter iter;
  gpointer key, value;

  LOG_DEBUG("%s: %s (path=%s)",
      watcher->name, N_("removing file monitor for path"), path);

  g_hash_table_iter_init(&iter, watcher->monitors);
  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      w_path = (gchar *) key;
      w_monitor = (GFileMonitor *) value;

      if (g_strcmp0(w_path, path) == 0)
        {
          if (!g_file_monitor_is_cancelled(w_monitor))
            {
              g_file_monitor_cancel(w_monitor);

              LOG_DEBUG("%s: %s (%s)",
                  watcher->name, N_("file monitor cancelled"), w_path);
            }
          else
            {
              LOG_DEBUG("%s: %s (%s)",
                  watcher->name, N_("file monitor already cancelled"), w_path);
            }

          g_hash_table_iter_remove(&iter);

          g_free(w_path);
          g_object_unref(w_path);

          break;
        }
    }
}

void
watcher_remove_monitor_for_recursive_path(const watcher_t *watcher,
    const gchar *path)
{
  GFile *w_file, *file;
  GFileMonitor *w_monitor;
  gchar *w_path;
  GHashTableIter iter;
  gpointer key, value;

  LOG_DEBUG("%s: %s (path=%s)",
      watcher->name, N_("removing file monitors for recursive path"), path);

  file = g_file_new_for_path(path);

  g_hash_table_iter_init(&iter, watcher->monitors);
  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      w_path = (gchar *) key;
      w_monitor = (GFileMonitor *) value;

      if (g_strcmp0(w_path, watcher->path) == 0)
        continue;

      w_file = g_file_new_for_path(w_path);

      if (g_file_equal(w_file, file) || g_file_has_prefix(w_file, file))
        {
          if (!g_file_monitor_is_cancelled(w_monitor))
            {
              g_file_monitor_cancel(w_monitor);

              LOG_DEBUG("%s: %s (%s)",
                  watcher->name, N_("file monitor cancelled"), w_path);
            }
          else
            {
              LOG_DEBUG("%s: %s (%s)",
                  watcher->name, N_("file monitor already cancelled"), w_path);
            }

          g_hash_table_iter_remove(&iter);

          g_object_unref(w_file);
          g_free(w_path);
          g_object_unref(w_monitor);
        }
    }

  g_object_unref(file);
}

void
watcher_destroy_monitors(const watcher_t *watcher)
{
  GFileMonitor *monitor;
  gchar *path;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init(&iter, watcher->monitors);
  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      path = (gchar *) key;
      monitor = (GFileMonitor *) value;

      if (!g_file_monitor_is_cancelled(monitor))
        {
          g_file_monitor_cancel(monitor);

          LOG_DEBUG("%s: %s (%s)",
              watcher->name, N_("file monitor cancelled"), path);
        }
      else
        {
          LOG_DEBUG("%s: %s (%s)",
              watcher->name, N_("file monitor already cancelled"), path);
        }

      g_free(path);
      g_object_unref(monitor);
    }

  g_hash_table_remove_all(watcher->monitors);
}

void
watcher_list_monitors(const watcher_t *watcher)
{
  gchar *path;
  GHashTableIter iter;
  gpointer key, value;

  LOG_INFO("%s: %s", watcher->name, N_("listing monitors"));

  g_hash_table_iter_init(&iter, watcher->monitors);
  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      path = (gchar *) key;

      LOG_INFO("%s: +-- path=%s", watcher->name, path);
    }

  LOG_INFO("%s: %s", watcher->name, N_("end of list"));
}

void
watcher_event(GFileMonitor *monitor, GFile *file, GFile *other_file,
    GFileMonitorEvent event_type, gpointer user_data)
{
  GFile *parent, *top, *tmp;
  gboolean depth = 1;
  watcher_t *watcher;
  watcher_event_t *event;

  if (!user_data)
    return;

  watcher = (watcher_t *) user_data;

  parent = g_file_new_for_path(watcher->path);

  event = (watcher_event_t *) g_new0(watcher_event_t, 1);
  event->watcher = watcher;
  event->file = g_file_get_path(file);
  event->rfile = g_file_get_relative_path(parent, file);
  if (!event->rfile)
    event->rfile = g_strdup("");

  if (watcher->recursive)
    {
      for (top = g_file_dup(file), depth = 1;
          !g_file_equal(top, parent) && !g_file_has_parent(top, parent); top =
              tmp, depth++)
        {
          tmp = g_file_get_parent(top);

          g_object_unref(top);
        }

      g_object_unref(top);

      LOG_DEBUG("%s: file depth to watcher path is '%d'", watcher->name, depth);
    }

  g_object_unref(parent);

  LOG_DEBUG("%s: %s (event_type=%d, file=%s)",
      watcher->name, N_("watcher event received"), event_type, event->file);

  switch (event_type)
  {
  case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    {
      event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_CHANGING);

      break;
    }

  case G_FILE_MONITOR_EVENT_CHANGED:
    {
      event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_CHANGED);

      break;
    }

  case G_FILE_MONITOR_EVENT_CREATED:
    {
      event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_CREATED);

      break;
    }

  case G_FILE_MONITOR_EVENT_DELETED:
    {
      event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_DELETED);

      break;
    }

  case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
    {
      event->event = g_strdup(CONFIG_KEY_WATCHER_EVENT_ATTRIBUTECHANGED);

      break;
    }

  default:
    {
      LOG_DEBUG("%s: %s (event_type=%d)",
          watcher->name, N_("unknown event"), event_type);

      g_free(event->file);
      g_free(event->rfile);
      g_free(event);

      return;

      break;
    }
  }

  if ((event_type == G_FILE_MONITOR_EVENT_DELETED) && watcher->recursive
      && (g_strcmp0(event->file, watcher->path) != 0))
    {
      watcher_remove_monitor_for_recursive_path(watcher, event->file);
    }

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

  if (event_type == G_FILE_MONITOR_EVENT_CREATED && watcher->recursive
      && g_file_test(event->file, G_FILE_TEST_IS_DIR)
  && (g_strcmp0(event->file, watcher->path) != 0))
    {
      watcher_add_monitor_for_recursive_path(watcher, event->file, depth);
    }

  watcher_event_fired(watcher, event);

  g_free(event->event);
  g_free(event->file);
  g_free(event->rfile);
  g_free(event);
}

gboolean
watcher_event_test(watcher_t *watcher, watcher_event_t *event)
{
  gboolean found = FALSE;
  gboolean include = TRUE;
  gint i;

  if (watcher->events)
    {
      for (i = 0; watcher->events[i] != NULL; i++)
        {
          if (g_strcmp0(watcher->events[i], event->event) == 0)
            {
              found = TRUE;

              break;
            }
        }

      if (!found)
        return FALSE;
    }

  if ((g_strcmp0(event->event, CONFIG_KEY_WATCHER_EVENT_DELETED) != 0)
      && (g_strcmp0(event->event, CONFIG_KEY_WATCHER_EVENT_UNMOUNTED) != 0))
    {
#ifndef GStatBuf
      struct stat st_path, st_file;
#else
      GStatBuf st_path, st_file;
#endif

      memset(&st_path, 0, sizeof(st_path));
      memset(&st_file, 0, sizeof(st_file));

      if (g_stat(event->watcher->path, &st_path) != 0)
        {
          LOG_ERROR("%s '%s'",
              N_("failed to stat the watcher path"), event->watcher->path);

          return FALSE;
        }

      if (g_stat(event->file, &st_file) != 0)
        {
          LOG_ERROR("%s '%s'",
              N_("failed to stat the watched file"), event->file);

          return FALSE;
        }

      if (watcher->mount)
        {
          if (st_file.st_dev != st_path.st_dev)
            {
              LOG_DEBUG("%s", N_("the filesystems are not the same"));

              return FALSE;
            }
        }

      if (watcher->readable)
        {
          if (g_access (event->file, R_OK) != 0)
            {
              LOG_DEBUG("%s", N_("the file is not readable"));

              return FALSE;
            }
        }

      if (watcher->writable)
        {
          if (g_access (event->file, W_OK) != 0)
            {
              LOG_DEBUG("%s", N_("the file is not writable"));

              return FALSE;
            }
        }

      if (watcher->executable)
        {
          if (g_access (event->file, X_OK) != 0)
            {
              LOG_DEBUG("%s", N_("the file is not executable"));

              return FALSE;
            }
        }

      if (!S_ISDIR(st_file.st_mode) && (watcher->size > -1))
        {
          guint size;

          switch (watcher->size_unit)
          {
          case WATCHER_SIZE_UNIT_KBYTES:
            {
              size = watcher->size * 1024;

              break;
            }


          case WATCHER_SIZE_UNIT_MBYTES:
            {
              size = watcher->size * 1048576;

              break;
            }


          case WATCHER_SIZE_UNIT_GBYTES:
            {
              size = watcher->size * 1073741824;

              break;
            }

          case WATCHER_SIZE_UNIT_BYTES:
          default:
            {
              size = watcher->size;

              break;
            }
          }

          switch (watcher->size_cmp)
          {
          case WATCHER_SIZE_COMPARE_GREATER:
            {
              if (st_file.st_size <= size)
                {
                  LOG_DEBUG("%s", N_("the file size is not greater"));

                  return FALSE;
                }

              break;
            }

          case WATCHER_SIZE_COMPARE_LESS:
            {
              if (st_file.st_size >= size)
                {
                  LOG_DEBUG("%s", N_("the file size is not less"));

                  return FALSE;
                }

              break;
            }

          case WATCHER_SIZE_COMPARE_EQUAL:
          default:
            {
              if (st_file.st_size != size)
                {
                  LOG_DEBUG("%s", N_("the file size is not equal"));

                  return FALSE;
                }

              break;
            }
          }
        }

      if (watcher->type)
        {
          if (S_ISBLK(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_BLOCK) != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else if (S_ISCHR(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_CHARACTER)
                  != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else if (S_ISDIR(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_DIRECTORY)
                  != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else if (S_ISREG(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_REGULAR)
                  != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else if (S_ISLNK(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_SYMBOLICLINK)
                  != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else if (S_ISFIFO(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_FIFO) != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else if (S_ISSOCK(st_file.st_mode))
            {
              if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_SOCKET) != 0)
                {
                  LOG_DEBUG("%s", N_("the file type doesn't match"));

                  return FALSE;
                }
            }
          else
            {
              LOG_DEBUG("%s (%d)",
                  N_("the file type is unknown"), st_file.st_mode);

              return FALSE;
            }

          if (watcher->user)
            {
              struct passwd *pwd;

              pwd = getpwnam(watcher->user);
              if (!pwd)
                {
                  gchar *err;
                  uid_t uid;

                  LOG_DEBUG("%s",
                      N_("failed to retrieve the user name, trying the user id"));

                  uid = (uid_t) strtol(watcher->user, &err, 10);
                  if ((err == watcher->user) || (errno == ERANGE)
                      || (errno == EINVAL))
                    {
                      LOG_DEBUG("%s", N_("invalid value"));

                      return FALSE;
                    }

                  pwd = getpwuid(uid);
                  if (!pwd)
                    {
                      LOG_DEBUG("%s", N_("failed to retrieve the user id"));

                      return FALSE;
                    }
                }

              if (pwd->pw_uid != st_file.st_uid)
                {
                  LOG_DEBUG("%s", N_("the owner user matches"));

                  return FALSE;
                }
            }

          if (watcher->group)
            {
              struct group *grp;

              grp = getgrnam(watcher->group);
              if (!grp)
                {
                  gchar *err;
                  gid_t gid;

                  LOG_DEBUG("%s",
                      N_("failed to retrieve the group name, trying the group id"));

                  gid = (gid_t) strtol(watcher->group, &err, 10);
                  if ((err == watcher->group) || (errno == ERANGE)
                      || (errno == EINVAL))
                    {
                      LOG_DEBUG("%s", N_("invalid value"));

                      return FALSE;
                    }

                  grp = getgrgid(gid);
                  if (!grp)
                    {
                      LOG_DEBUG("%s", N_("failed to retrieve the group id"));

                      return FALSE;
                    }
                }

              if (grp->gr_gid != st_file.st_gid)
                {
                  LOG_DEBUG("%s", N_("the owner group matches"));

                  return FALSE;
                }
            }
        }
    }

  if (watcher->includes)
    {
      include = FALSE;

      for (i = 0; watcher->includes[i] != NULL; i++)
        {
          if (g_pattern_match_simple(watcher->includes[i], event->rfile))
            {
              LOG_DEBUG("%s", N_("relative filename found in include list"));

              return TRUE;
            }
        }
    }

  if (watcher->excludes)
    {
      for (i = 0; watcher->excludes[i] != NULL; i++)
        {
          if (g_pattern_match_simple(watcher->excludes[i], event->rfile))
            {
              LOG_DEBUG("%s", N_("relative filename found in exclude list"));

              return FALSE;
            }
        }
    }

  return include;
}

void
watcher_event_fired(watcher_t *watcher, watcher_event_t *event)
{
  GError *error = NULL;
  GRegex *regex;
  gchar *exec, *tmp;

  LOG_INFO( "%s: %s (event=%s, file=%s)",
      watcher->name, N_("event fired"), event->event, event->file);

  if (watcher->exec)
    {
      regex = g_regex_new("\\" CONFIG_KEY_WATCHER_EXEC_KEY_NAME, 0, 0, &error);
      exec = g_regex_replace_literal(regex, watcher->exec, -1, 0, watcher->name,
          0, &error);
      g_regex_unref(regex);

      tmp = exec;
      regex = g_regex_new("\\" CONFIG_KEY_WATCHER_EXEC_KEY_PATH, 0, 0, &error);
      exec = g_regex_replace_literal(regex, tmp, -1, 0, watcher->path, 0,
          &error);
      g_regex_unref(regex);
      g_free(tmp);

      tmp = exec;
      regex = g_regex_new("\\" CONFIG_KEY_WATCHER_EXEC_KEY_EVENT, 0, 0, &error);
      exec = g_regex_replace_literal(regex, tmp, -1, 0, event->event, 0,
          &error);
      g_regex_unref(regex);
      g_free(tmp);

      tmp = exec;
      regex = g_regex_new("\\" CONFIG_KEY_WATCHER_EXEC_KEY_FILE, 0, 0, &error);
      exec = g_regex_replace_literal(regex, tmp, -1, 0, event->file, 0, &error);
      g_regex_unref(regex);
      g_free(tmp);

      tmp = exec;
      regex = g_regex_new("\\" CONFIG_KEY_WATCHER_EXEC_KEY_RFILE, 0, 0, &error);
      exec = g_regex_replace_literal(regex, tmp, -1, 0, event->rfile, 0,
          &error);
      g_regex_unref(regex);
      g_free(tmp);

      LOG_INFO("%s: %s '%s'", watcher->name, N_("executing command"), exec);

      g_spawn_command_line_async(exec, &error);
      g_free(exec);
      if (error)
        {
          LOG_ERROR("%s: %s (%s)",
              watcher->name, N_("failed to execute command"), error->message);

          g_error_free(error);
          error = NULL;
        }
    }

  if (!app->daemon)
    {
      if (watcher->print)
        g_print("%s\n", event->file);

      if (watcher->print0)
        g_print("%s", event->file);
    }
}



