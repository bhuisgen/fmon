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
#include "daemon.h"
#include "log.h"
#include "log_console.h"
#include "log_file.h"
#include "log_syslog.h"

#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define LOG_ERROR(_fmt, ...)    if (app->logger) log_message(app->logger, LOG_LEVEL_ERROR, _fmt, __VA_ARGS__)
#define LOG_INFO(_fmt, ...)     if (app->logger) log_message(app->logger, LOG_LEVEL_INFO, _fmt, __VA_ARGS__)
#ifdef DEBUG
#define LOG_DEBUG(_fmt, ...)    if (app->logger) log_message(app->logger, LOG_LEVEL_DEBUG, _fmt, __VA_ARGS__)
#else
#define LOG_DEBUG(_fmt, ...)
#endif

typedef struct _watcher_t
{
  gchar *name;
  gchar *path;
  gboolean recursive;
  gint maxdepth;
  gchar *exec;
  gboolean print;
  gboolean print0;
  gboolean mount;
  gchar *type;
  gchar *user;
  gchar *group;
  gchar **events;
  gchar **includes;
  gchar **excludes;
  GHashTable *monitors;
} watcher_t;

typedef struct _watcher_event_t
{
  struct _watcher_t *watcher;
  gchar *event;
  gchar *file;
  gchar *rfile;
} watcher_event_t;

application_t *app = NULL;

gchar *
get_default_config_file(const gchar *file);
gboolean
load_config();
gboolean
reload_config();
GSList *
init_watchers();
logger_t *
init_logger();
void
start_monitors();
void
stop_monitors();
void
list_monitors();
void
create_mount_monitor();
void
destroy_mount_monitor();
void
mount_event(GUnixMountMonitor *monitor, gpointer user_data);
gboolean
watcher_add_monitor_for_path(const watcher_t *watcher, const gchar *path);
gboolean
watcher_add_monitor_for_recursive_path(const watcher_t *watcher,
    const gchar *path, guint depth);
void
watcher_remove_monitor_for_path(const watcher_t *watcher, const gchar *path);
void
watcher_remove_monitor_for_recursive_path(const watcher_t *watcher,
    const gchar *path);
void
watcher_destroy_monitors(const watcher_t *watcher);
void
watcher_list_monitors(const watcher_t *watcher);
void
watcher_event(GFileMonitor *monitor, GFile *file, GFile *other_file,
    GFileMonitorEvent event_type, gpointer user_data);
gboolean
watcher_event_test(watcher_t *watcher, watcher_event_t *event);
void
watcher_event_fired(watcher_t *watcher, watcher_event_t *event);
void
version();
void
parse_command_line(gint argc, gchar *argv[]);
void
sigpipe(gint sig);
void
sighup(gint sig);
void
sigint(gint sig);
void
sigterm(gint sig);
void
sigusr1(gint sig);
void
sigusr2(gint sig);
void
cleanup(void);

gchar*
get_default_config_file(const gchar *file)
{
  const gchar *homedir;
  gchar* config_file;

  if (file && !g_access(file, R_OK))
    {
      config_file = g_strdup(file);

      return config_file;
    }

  homedir = g_getenv("HOME");
  if (!homedir)
    homedir = g_get_home_dir();

  config_file = g_build_path(G_DIR_SEPARATOR_S, homedir, FMON_HOMEDIR,
      FMON_CONFIGFILE, NULL);

  if (g_access(config_file, R_OK))
    {
      g_free(config_file);

      config_file = g_build_path(G_DIR_SEPARATOR_S, SYSCONFDIR,
          FMON_CONFIGFILE, NULL);
    }

  if (g_access(config_file, R_OK))
    {
      g_free(config_file);

      return NULL;
    }

  return config_file;
}

gboolean
load_config()
{
  GError *error = NULL;
  gchar *group;

  app->settings = g_key_file_new();

  g_key_file_load_from_file(app->settings, app->config_file, G_KEY_FILE_NONE,
      &error);
  if (error)
    {
      g_printerr("%s: %s (%s)\n", app->config_file,
          N_("error in configuration file"), error->message);

      g_error_free(error);
      error = NULL;

      return FALSE;
    }

  g_key_file_set_list_separator(app->settings, ',');

  group = g_key_file_get_start_group(app->settings);
  if (!group)
    {
      g_printerr("%s: %s (%s)\n", app->config_file,
          N_("error in configuration file"), N_("no group 'main'"));

      return FALSE;
    }

  if (g_strcmp0(group, CONFIG_GROUP_MAIN) != 0)
    {
      g_printerr("%s: %s (%s)\n", app->config_file,
          N_("error in configuration file"),
          N_("the first group is not 'main'"));

      g_free(group);

      return FALSE;
    }

  g_free(group);

  return TRUE;
}

gboolean
reload_config()
{
  GKeyFile *settings;
  GError *error = NULL;
  gchar *group;

  settings = g_key_file_new();
  g_key_file_load_from_file(settings, app->config_file, G_KEY_FILE_NONE,
      &error);
  if (error)
    {
      LOG_ERROR("%s: %s (%s)\n",
          app->config_file, N_("error in configuration file, aborting reload"), error->message);

      g_error_free(error);
      error = NULL;
      g_key_file_free(settings);

      return FALSE;
    }

  g_key_file_set_list_separator(settings, ',');

  group = g_key_file_get_start_group(settings);
  if (!group)
    {
      LOG_ERROR("%s: %s (%s)\n",
          app->config_file, N_("error in configuration file"), N_("no group 'main'"));

      g_key_file_free(settings);

      return FALSE;
    }

  if (g_strcmp0(group, CONFIG_GROUP_MAIN) != 0)
    {
      LOG_ERROR("%s: %s (%s)\n",
          app->config_file, N_("error in configuration file"), N_("the first group is not 'main'"));

      g_free(group);
      g_key_file_free(settings);

      return FALSE;
    }

  g_free(group);

  if (app->settings)
    g_key_file_free(app->settings);

  app->settings = settings;

  return TRUE;
}

GSList *
init_watchers()
{
  GSList *list = NULL;
  GError *error = NULL;
  gchar **groups;
  gsize len;
  gint i;

  groups = g_key_file_get_groups(app->settings, &len);
  if (len < 2)
    {
      g_printerr("%s: %s (%s)\n", app->config_file,
          N_("error in configuration file"), N_("no watcher group found"));

      g_strfreev(groups);

      return NULL;
    }

  for (i = 0; i < len; i++)
    {
      if (g_strcmp0(groups[i], CONFIG_GROUP_MAIN) == 0)
        continue;

      watcher_t *watcher;
      watcher = g_new0(watcher_t, 1);

      watcher->name = g_strdup(groups[i]);

      watcher->path = g_key_file_get_string(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_PATH, &error);
      if (error)
        {
          g_printerr("%s: %s\n", watcher->name, N_("invalid path"));

          g_error_free(error);
          error = NULL;
          g_free(watcher->name);
          g_free(watcher);
          g_strfreev(groups);

          return NULL;
        }
      if (!g_file_test(watcher->path, G_FILE_TEST_EXISTS))
        {
          g_printerr("%s: %s\n", watcher->name, N_("file/path doesn't exist"));

          g_free(watcher->path);
          g_free(watcher->name);
          g_free(watcher);
          g_strfreev(groups);

          return NULL;
        }
      if (!g_file_test(watcher->path, G_FILE_TEST_IS_DIR))
        {
          if (g_access(watcher->path, R_OK))
            {
              g_printerr("%s: %s\n", watcher->name,
                  N_("bad permissions on file"));

              g_free(watcher->path);
              g_free(watcher->name);
              g_free(watcher);
              g_strfreev(groups);

              return NULL;
            }
        }
      else
        {
          if (g_access(watcher->path, R_OK | X_OK))
            {
              g_printerr("%s: %s\n", watcher->name,
                  N_("bad permissions on path"));

              g_free(watcher->path);
              g_free(watcher->name);
              g_free(watcher);
              g_strfreev(groups);

              return NULL;
            }
        }

      watcher->recursive = g_key_file_get_boolean(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_RECURSIVE, &error);
      if (error)
        {
          watcher->recursive = CONFIG_KEY_WATCHER_RECURSIVE_DEFAULT;

          g_error_free(error);
          error = NULL;
        }

      if (watcher->recursive)
        {
          if (!g_file_test(watcher->path, G_FILE_TEST_IS_DIR))
            {
              g_printerr("%s: %s\n", watcher->name,
                  N_("recursion is enabled but path is not a directory"));

              g_free(watcher->path);
              g_free(watcher->name);
              g_free(watcher);
              g_strfreev(groups);

              return NULL;
            }

          watcher->maxdepth = g_key_file_get_integer(app->settings,
              watcher->name, CONFIG_KEY_WATCHER_MAXDEPTH, &error);
          if (error)
            {
              watcher->recursive = CONFIG_KEY_WATCHER_MAXDEPTH_DEFAULT;

              g_error_free(error);
              error = NULL;
            }
          if (watcher->maxdepth < 0)
            {
              g_printerr("%s: %s\n", watcher->name,
                  N_("invalid maximum depth of recursion"));

              g_free(watcher->path);
              g_free(watcher->name);
              g_free(watcher);
              g_strfreev(groups);

              return NULL;
            }
        }

      watcher->events = g_key_file_get_string_list(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_EVENTS, &len, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }
      if (watcher->events)
        {
          for (i = 0; watcher->events[i]; i++)
            {
              if ((g_strcmp0(watcher->events[i],
                  CONFIG_KEY_WATCHER_EVENT_CHANGED) != 0)
                  && (g_strcmp0(watcher->events[i],
                      CONFIG_KEY_WATCHER_EVENT_CREATED) != 0)
                  && (g_strcmp0(watcher->events[i],
                      CONFIG_KEY_WATCHER_EVENT_DELETED) != 0)
                  && (g_strcmp0(watcher->events[i],
                      CONFIG_KEY_WATCHER_EVENT_ATTRIBUTECHANGED) != 0)
                  && (g_strcmp0(watcher->events[i],
                      CONFIG_KEY_WATCHER_EVENT_MOUNTED) != 0)
                  && (g_strcmp0(watcher->events[i],
                      CONFIG_KEY_WATCHER_EVENT_UNMOUNTED) != 0))
                {
                  g_printerr("%s: %s\n", watcher->name, N_("invalid event"));

                  g_strfreev(watcher->events);
                  g_free(watcher->path);
                  g_free(watcher->name);
                  g_free(watcher);
                  g_strfreev(groups);

                  return NULL;
                }
            }
        }

      watcher->exec = g_key_file_get_string(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_EXEC, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->print = g_key_file_get_boolean(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_PRINT, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->print0 = g_key_file_get_boolean(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_PRINT0, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->mount = g_key_file_get_boolean(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_MOUNT, &error);
      if (error)
        {
          watcher->mount = CONFIG_KEY_WATCHER_MOUNT_DEFAULT;

          g_error_free(error);
          error = NULL;
        }

      watcher->type = g_key_file_get_string(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_TYPE, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }
      if (watcher->type
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_BLOCK) != 0)
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_CHARACTER) != 0)
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_DIRECTORY) != 0)
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_FIFO) != 0)
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_REGULAR) != 0)
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_SOCKET) != 0)
          && (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_SYMBOLICLINK)
              != 0))
        {
          g_printerr("%s: %s\n", watcher->name, N_("invalid type"));

          g_error_free(error);
          error = NULL;
          g_strfreev(watcher->events);
          g_free(watcher->path);
          g_free(watcher->name);
          g_free(watcher);
          g_strfreev(groups);

          return NULL;
        }

      watcher->user = g_key_file_get_string(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_USER, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->group = g_key_file_get_string(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_GROUP, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->includes = g_key_file_get_string_list(app->settings,
          watcher->name, CONFIG_KEY_WATCHER_INCLUDE, NULL, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->excludes = g_key_file_get_string_list(app->settings,
          watcher->name, CONFIG_KEY_WATCHER_EXCLUDE, NULL, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      watcher->monitors = g_hash_table_new(g_str_hash, g_str_equal);

      list = g_slist_append(list, watcher);
    }

  g_strfreev(groups);

  return list;
}

logger_t *
init_logger()
{
  GError *error = NULL;
  logger_t *logger = NULL;
  gboolean daemon;

  daemon = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
      CONFIG_KEY_MAIN_DAEMONIZE, &error);
  if (error)
    {
      daemon = CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT;

      g_error_free(error);
      error = NULL;
    }
  if (daemon)
    {
      gboolean use_syslog;
      gint log_level;
      LoggerLevel level = LOGGER_LEVEL_NONE;

      log_level = g_key_file_get_integer(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_LOGLEVEL, &error);
      if (error)
        {
          log_level = CONFIG_KEY_MAIN_LOGLEVEL_DEFAULT;

          g_error_free(error);
          error = NULL;
        }

      switch (log_level)
        {
      case CONFIG_KEY_MAIN_LOGLEVEL_NONE:
        level = LOGGER_LEVEL_NONE;
        break;

      case CONFIG_KEY_MAIN_LOGLEVEL_ERROR:
        level = LOGGER_LEVEL_ERROR;
        break;

      case CONFIG_KEY_MAIN_LOGLEVEL_WARNING:
        level = LOGGER_LEVEL_WARNING;
        break;

      case CONFIG_KEY_MAIN_LOGLEVEL_INFO:
        level = LOGGER_LEVEL_INFO;
        break;

      case CONFIG_KEY_MAIN_LOGLEVEL_DEBUG:
        level = LOGGER_LEVEL_DEBUG;
        break;
        }

      use_syslog = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_USESYSLOG, &error);
      if (error)
        {
          use_syslog = CONFIG_KEY_MAIN_USESYSLOG_DEFAULT;

          g_error_free(error);
          error = NULL;
        }

      if (use_syslog)
        {
          gchar *syslog_facility;

          syslog_facility = g_key_file_get_string(app->settings,
              CONFIG_KEY_MAIN_GROUP, CONFIG_KEY_MAIN_SYSLOGFACILITY, &error);
          if (error)
            {
              syslog_facility = g_strdup(CONFIG_KEY_MAIN_SYSLOGFACILITY);

              g_error_free(error);
              error = NULL;
            }

          handler_t *handler = log_handler_create(LOG_HANDLER_TYPE_SYSLOG);
          if (!handler)
            {
              g_free(syslog_facility);

              return NULL;
            }

          if (log_handler_set_option(handler,
              LOG_HANDLER_SYSLOG_OPTION_FACILITY, syslog_facility) != 0)
            {
              log_handler_destroy(handler);
              g_free(syslog_facility);

              return NULL;
            }

          g_free(syslog_facility);

          logger = log_create_logger(handler, level);
          if (!logger)
            {
              log_handler_destroy(handler);

              return NULL;
            }
        }
      else
        {
          gchar *log_file;

          log_file = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
              CONFIG_KEY_MAIN_LOGFILE, &error);
          if (error)
            {
              log_file = g_strdup(CONFIG_KEY_MAIN_LOGFILE_DEFAULT);

              g_error_free(error);
              error = NULL;
            }

          handler_t *handler = log_handler_create(LOG_HANDLER_TYPE_FILE);
          if (!handler)
            {
              g_free(log_file);

              return NULL;
            }

          if (log_handler_set_option(handler, LOG_HANDLER_FILE_OPTION_LOGFILE,
              log_file) != 0)
            {
              log_handler_destroy(handler);
              g_free(log_file);

              return NULL;
            }

          g_free(log_file);

          logger = log_create_logger(handler, level);
          if (!logger)
            {
              log_handler_destroy(handler);

              return NULL;
            }
        }
    }
  else
    {
      handler_t *handler = log_handler_create(LOG_HANDLER_TYPE_CONSOLE);
      if (!handler)
        return NULL;

      if (app->verbose)
        {
#ifdef DEBUG
          logger = log_create_logger(handler, LOGGER_LEVEL_ALL);
#else
          logger = log_create_logger(handler, LOGGER_LEVEL_INFO);
#endif
        }
      else
        {
          logger = log_create_logger(handler, LOGGER_LEVEL_ERROR);
        }
      if (!logger)
        {
          log_handler_destroy(handler);

          return NULL;
        }
    }

  return logger;
}

void
start_monitors()
{
  GSList *item;
  watcher_t *watcher;

  if (app->started)
    {
      LOG_INFO("%s", N_("watchers already started"));

      return;
    }

  LOG_INFO("%s", N_("starting watchers"));

  create_mount_monitor();

  LOG_INFO("%s", N_("mount watcher started"));

  for (item = app->watchers; item; item = item->next)
    {
      watcher = (watcher_t *) item->data;

      if (watcher->recursive)
        {
          watcher_add_monitor_for_recursive_path(watcher, watcher->path, 1);
        }
      else
        {
          watcher_add_monitor_for_path(watcher, watcher->path);
        }

      LOG_INFO("%s: %s", watcher->name, N_("watcher started"));

      watcher_list_monitors(watcher);
    }

  app->started = TRUE;
}

void
stop_monitors()
{
  GSList *item;
  watcher_t *watcher;

  if (!app->started)
    {
      LOG_INFO("%s", N_("watchers already stopped"));

      return;
    }

  LOG_INFO("%s", N_("stopping watchers"));

  destroy_mount_monitor();

  LOG_INFO("%s", N_("mount watcher stopped"));

  for (item = app->watchers; item; item = item->next)
    {
      watcher = (watcher_t *) item->data;

      watcher_destroy_monitors(watcher);

      LOG_INFO("%s: %s", watcher->name, N_("watcher stopped"));
    }

  app->started = FALSE;
}

void
list_monitors()
{
  GSList *item;
  watcher_t *watcher;

  if (!app->started)
    {
      LOG_INFO("%s", N_("watchers stopped"));

      return;
    }

  LOG_INFO("%s", N_("listing monitors"));

  for (item = app->watchers; item; item = item->next)
    {
      watcher = (watcher_t *) item->data;

      watcher_list_monitors(watcher);
    }
}

void
create_mount_monitor()
{
  LOG_DEBUG("%s", "creating UNIX mount monitor");

  app->monitor = g_unix_mount_monitor_new();
  app->mounts = g_unix_mounts_get(NULL);

  g_signal_connect(app->monitor, "mounts-changed", G_CALLBACK(mount_event),
      NULL);
}

void
destroy_mount_monitor()
{
  GUnixMountEntry *entry;
  GList *item;

  LOG_DEBUG("%s", "destroying UNIX mount monitor");

  if (app->monitor)
    {
      g_object_unref(app->monitor);
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
  gint depth;
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
      G_FILE_QUERY_INFO_NONE, NULL, &error);
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
  gboolean depth;
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
      depth = 1;

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
#ifndef GStatBuf
  struct stat st_path, st_file;
#else
  GStatBuf st_path, st_file;
#endif
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

  if (g_stat(event->watcher->path, &st_path) != 0)
    {
      LOG_ERROR("%s '%s'",
          N_("failed to stat the watcher path"), event->watcher->path);

      return FALSE;
    }

  if (g_stat(event->file, &st_file) != 0)
    {
      LOG_ERROR("%s '%s'", N_("failed to stat the watched file"), event->file);

      return FALSE;
    }

  if (watcher->mount)
    {
      if (st_file.st_dev != st_path.st_dev)
        {
          LOG_ERROR("%s", N_("the filesystems are not the same"));

          return FALSE;
        }
    }

  if (watcher->type)
    {
      switch (st_file.st_mode)
        {
      case S_IFBLK:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_BLOCK) != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      case S_IFCHR:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_CHARACTER) != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      case S_IFDIR:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_DIRECTORY) != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      case S_IFREG:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_REGULAR) != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      case S_IFLNK:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_SYMBOLICLINK)
              != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      case S_IFIFO:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_FIFO) != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      case S_IFSOCK:
        {
          if (g_strcmp0(watcher->type, CONFIG_KEY_WATCHER_TYPE_SOCKET) != 0)
            {
              LOG_DEBUG("%s", N_("the file type doesn't match"));

              return FALSE;
            }

          break;
        }

      default:
        {
          LOG_DEBUG("%s", N_("the file type is unknown"));

          return FALSE;

          break;
        }
        }
    }

  if (watcher->user)
    {
      struct passwd *pwd;

      pwd = getpwnam(watcher->user);
      if (!pwd)
        {
          gchar *err;
          uid_t uid;

          LOG_DEBUG("%s", N_("failed to retrieve the user name, trying the user id"));

          uid = (uid_t) strtol(watcher->user, &err, 10);
          if ((err == watcher->user) || (errno == ERANGE) || (errno == EINVAL))
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

          LOG_DEBUG("%s", N_("failed to retrieve the group name, trying the group id"));

          gid = (gid_t) strtol(watcher->group, &err, 10);
          if ((err == watcher->user) || (errno == ERANGE) || (errno == EINVAL))
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

void
version()
{
  g_print("%s\n", PACKAGE_STRING);
  g_print("%s\n", FMON_COPYRIGHT);
  g_print("\n");
  g_print("%s\n", FMON_LICENCE);
  g_print("\n");
}

void
parse_command_line(gint argc, gchar *argv[])
{
  GOptionContext *context;
  GOptionGroup *watcher;
  GError *error = NULL;
  gchar *help;
  gchar *current_dir, *file;
  gchar *config_file = NULL;
  gboolean verbose = FALSE;
  gint show_version = 0;
  gchar *watcher_path = NULL;
  gboolean watcher_recursive = CONFIG_KEY_WATCHER_RECURSIVE_DEFAULT;
  gint watcher_maxdepth = CONFIG_KEY_WATCHER_MAXDEPTH_DEFAULT;
  gchar *watcher_event = NULL;
  gboolean watcher_mount = CONFIG_KEY_WATCHER_MOUNT_DEFAULT;
  gchar *watcher_type = NULL;
  gchar *watcher_user = NULL;
  gchar *watcher_group = NULL;
  gchar *watcher_include = NULL;
  gchar *watcher_exclude = NULL;
  gchar *watcher_exec = NULL;
  gboolean watcher_print = FALSE;
  gboolean watcher_print0 = FALSE;

  GOptionEntry main_entries[] =
    {
      { "file", 'f', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_FILENAME, &config_file,
          N_("Read configuration from file"), N_("FILE") },
      { "verbose", 'v', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &verbose,
          N_("Set verbose output") },
      { "version", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &show_version,
          N_("Show version information"), NULL },
      { NULL } };
  GOptionEntry watcher_entries[] =
    {
      { "path", 0, 0, G_OPTION_ARG_FILENAME, &watcher_path,
          N_("Path to watch for events"), N_("PATH") },
      { "recursive", 0, 0, G_OPTION_ARG_NONE, &watcher_recursive,
          N_("Enable recursive mode"), NULL },
      { "maxdepth", 0, 0, G_OPTION_ARG_INT, &watcher_maxdepth,
          N_("Maximum depth of recursion"), N_("LEVEL") },
      { "event", 0, 0, G_OPTION_ARG_STRING, &watcher_event,
          N_("Event to watch"), N_("EVENT") },
      { "mount", 0, 0, G_OPTION_ARG_NONE, &watcher_mount,
          N_("Ignore directories on other filesystems"), NULL },
      { "type", 0, 0, G_OPTION_ARG_STRING, &watcher_type, N_("Check file type"),
          N_("TYPE") },
      { "user", 0, 0, G_OPTION_ARG_STRING, &watcher_user,
          N_("Check owner user"), N_("NAME") },
      { "group", 0, 0, G_OPTION_ARG_STRING, &watcher_group,
          N_("Check owner group"), N_("NAME") },
      { "include", 0, 0, G_OPTION_ARG_STRING, &watcher_include,
          N_("Include files list"), N_("LIST") },
      { "exclude", 0, 0, G_OPTION_ARG_STRING, &watcher_exclude,
          N_("Exclude files list"), N_("LIST") },
      { "exec", 0, 0, G_OPTION_ARG_STRING, &watcher_exec,
          N_("Execute command on event"), N_("COMMAND") },
      { "print", 0, 0, G_OPTION_ARG_NONE, &watcher_print,
          N_("Print filename on event, followed by a newline") },
      { "print0", 0, 0, G_OPTION_ARG_NONE, &watcher_print0,
          N_("Print filename on event, followed by a null character") },
      { NULL } };

  context = g_option_context_new(N_("[WATCHER]"));

  watcher = g_option_group_new(N_("watcher"), N_("Watcher Options"),
      N_("Show all watcher options"), NULL, NULL);
  g_option_group_add_entries(watcher, watcher_entries);
  g_option_context_add_group(context, watcher);

  g_option_context_add_main_entries(context, main_entries, PACKAGE);

  g_option_context_parse(context, &argc, &argv, &error);
  if (error)
    {
      g_error_free(error);
      error = NULL;

      help = g_option_context_get_help(context, TRUE, NULL);
      g_print("%s", help);

      g_free(help);
      g_option_context_free(context);

      exit(1);
    }

  g_option_context_free(context);

  if (show_version == 1)
    {
      version();

      exit(0);
    }

  if (watcher_path)
    {
      app->settings = g_key_file_new();

      g_key_file_set_boolean(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_DAEMONIZE, CONFIG_KEY_MAIN_DAEMONIZE_NO);

      g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
          CONFIG_KEY_WATCHER_PATH, watcher_path);
      g_key_file_set_boolean(app->settings, CONFIG_GROUP_WATCHER,
          CONFIG_KEY_WATCHER_RECURSIVE, watcher_recursive);
      g_key_file_set_integer(app->settings, CONFIG_GROUP_WATCHER,
          CONFIG_KEY_WATCHER_MAXDEPTH, watcher_maxdepth);

      if (watcher_event)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_EVENTS, watcher_event);

      g_key_file_set_boolean(app->settings, CONFIG_GROUP_WATCHER,
          CONFIG_KEY_WATCHER_MOUNT, watcher_mount);

      if (watcher_type)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_TYPE, watcher_type);

      if (watcher_user)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_USER, watcher_user);

      if (watcher_group)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_GROUP, watcher_group);

      if (watcher_include)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_INCLUDE, watcher_include);

      if (watcher_exclude)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_EXCLUDE, watcher_exclude);

      if (watcher_exec)
        g_key_file_set_string(app->settings, CONFIG_GROUP_WATCHER,
            CONFIG_KEY_WATCHER_EXEC, watcher_exec);

      g_key_file_set_boolean(app->settings, CONFIG_GROUP_WATCHER,
          CONFIG_KEY_WATCHER_PRINT, watcher_print);

      g_key_file_set_boolean(app->settings, CONFIG_GROUP_WATCHER,
          CONFIG_KEY_WATCHER_PRINT0, watcher_print0);
    }
  else
    {
      if (config_file && !g_path_is_absolute(config_file))
        {
          current_dir = g_get_current_dir();
          file = g_build_filename(current_dir, config_file, NULL);

          g_free(current_dir);
          g_free(config_file);

          config_file = file;
        }

      app->config_file = get_default_config_file(config_file);
      if (!app->config_file)
        {
          g_printerr("%s\n",
              N_("The configuration file doesn't exist or cannot be read."));

          exit(1);
        }
    }

  app->verbose = verbose;
}

void
sigpipe(gint sig)
{
  LOG_INFO("%s", N_("SIGPIPE received, continuing execution"));
}

void
sighup(gint sig)
{
  LOG_INFO("%s", N_("SIGHUP received, reloading configuration"));

  reload_config();
}

void
sigint(gint sig)
{
  LOG_INFO("%s", N_("SIGINT received, exiting"));

  exit(0);
}

void
sigterm(gint sig)
{
  LOG_INFO("%s", N_("SIGTERM received, exiting"));

  exit(0);
}

void
sigusr1(gint sig)
{
  LOG_INFO("%s", N_("SIGUSR1 received, starting watchers"));

  start_monitors();
  list_monitors();
}

void
sigusr2(gint sig)
{
  LOG_INFO("%s", N_("SIGUSR2 received, stopping watchers"));

  stop_monitors();
}

void
cleanup(void)
{
  GError *error = NULL;
  gboolean daemon;

  LOG_DEBUG("%s", N_("cleanup"));

  g_main_loop_quit(app->loop);

  stop_monitors();

  if (app->settings && app->daemon)
    {
      daemon = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_DAEMONIZE, &error);
      if (error)
        {
          daemon = CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT;

          g_error_free(error);
          error = NULL;
        }
      if (daemon)
        {
          gchar *pid_file;

          pid_file = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
              CONFIG_KEY_MAIN_PIDFILE, &error);
          if (error)
            {
              pid_file = g_strdup(CONFIG_KEY_MAIN_PIDFILE_DEFAULT);

              g_error_free(error);
              error = NULL;
            }

          g_unlink(pid_file);
          g_free(pid_file);
        }

      LOG_INFO("%s %s", PACKAGE, N_("daemon stopped"));
    }

  if (app->logger)
    {
      log_destroy_logger(app->logger);
    }

  if (app->watchers)
    {
      GSList *item;
      watcher_t *watcher;

      for (item = app->watchers; item; item = item->next)
        {
          watcher = (watcher_t *) item->data;
          if (!watcher)
            continue;

          g_free(watcher->name);
          g_free(watcher->path);
          g_free(watcher->exec);
          g_free(watcher->type);
          g_free(watcher->user);
          g_free(watcher->group);
          g_strfreev(watcher->events);
          g_strfreev(watcher->includes);
          g_strfreev(watcher->excludes);
          g_hash_table_destroy(watcher->monitors);
          g_free(watcher);
        }

      g_slist_free(app->watchers);
    }

  if (app->settings)
    g_key_file_free(app->settings);

  g_free(app->config_file);
  g_free(app);
}

gint
main(gint argc, gchar *argv[])
{
  GError *error = NULL;
  gboolean daemon;
  gint ret;

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  if (glib_check_version(2, 6, 0))
    {
      g_error(N_("GLib version 2.6.0 or above is needed"));
    }

#ifdef DEBUG
  g_type_init_with_debug_flags(G_TYPE_DEBUG_MASK);
#else
  g_type_init();
#endif

  app = g_new0(application_t, 1);
  app->loop = g_main_loop_new(NULL, TRUE);
  atexit(cleanup);

  parse_command_line(argc, argv);

  if (app->config_file && !load_config())
    exit(2);

  app->watchers = init_watchers();
  if (!app->watchers)
    exit(3);

  app->logger = init_logger();
  if (!app->logger)
    {
      g_printerr("%s\n", N_("Failed to create events logger."));

      exit(4);
    }

  daemon = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
      CONFIG_KEY_MAIN_DAEMONIZE, &error);
  if (error)
    {
      daemon = CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT;

      g_error_free(error);
      error = NULL;
    }
  if (daemon)
    {
      gchar *pid_file, *user, *group;

      pid_file = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_PIDFILE, &error);
      if (error)
        {
          pid_file = g_strdup(CONFIG_KEY_MAIN_PIDFILE_DEFAULT);

          g_error_free(error);
          error = NULL;
        }

      user = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_USER, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      group = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_GROUP, &error);
      if (error)
        {
          g_error_free(error);
          error = NULL;
        }

      ret = daemonize(pid_file, user, group);

      g_free(pid_file);
      if (user)
        g_free(user);
      if (group)
        g_free(group);

      if (ret < 0)
        {
          LOG_ERROR("%s: %d", N_("failed to daemonize, error code"), ret);

          exit(5);
        }

      app->daemon = TRUE;
      LOG_INFO("%s %s", PACKAGE, N_("daemon started"));
    }

  signal(SIGPIPE, sigpipe);
  signal(SIGINT, sigint);
  signal(SIGTERM, sigterm);

  if (app->daemon)
    {
      signal(SIGHUP, sighup);
      signal(SIGUSR1, sigusr1);
      signal(SIGUSR2, sigusr2);
    }

  start_monitors();

  g_main_loop_run(app->loop);

  return 0;
}
