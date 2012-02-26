/*
 * fmond - file monitoring daemon
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

#include "fmond.h"
#include "daemon.h"
#include "log.h"
#include "log_console.h"
#include "log_file.h"
#include "log_syslog.h"

#include <stdlib.h>
#include <string.h>

#define LOG_ERROR(_fmt, ...)    log_message(app->logger, LOG_LEVEL_ERROR, _fmt, __VA_ARGS__)
#define LOG_INFO(_fmt, ...)     log_message(app->logger, LOG_LEVEL_INFO, _fmt, __VA_ARGS__)
#ifdef DEBUG
#define LOG_DEBUG(_fmt, ...)    log_message(app->logger, LOG_LEVEL_DEBUG, _fmt, __VA_ARGS__)
#else
#define LOG_DEBUG(_fmt, ...)
#endif

typedef struct _watcher_t
{
  gchar *name;
  gchar *path;
  gchar **events;
  gchar *command;
  GFileMonitor *monitor;
} watcher_t;

application_t *app = NULL;

gchar *
get_default_config_file();
gboolean
load_config();
gboolean
reload_config();
GSList *
init_watchers();
logger_t *
init_logger();
gboolean
start_watchers();
void
watcher_event(GFileMonitor *monitor, GFile *file, GFile *other_file,
    GFileMonitorEvent event_type, gpointer user_data);
void
watcher_event_fired(watcher_t *watcher, gchar *event, gchar *file);
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
cleanup(void);

gchar*
get_default_config_file()
{
  const gchar *homedir;
  gchar* config_file;

  homedir = g_getenv("HOME");
  if (!homedir)
    homedir = g_get_home_dir();

  config_file = g_build_path(G_DIR_SEPARATOR_S, homedir, FMOND_HOMEDIR,
      FMOND_CONFIGFILE, NULL);

  if (!g_file_test(config_file, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
    {
      g_free(config_file);

      config_file = g_build_path(G_DIR_SEPARATOR_S, SYSCONFDIR,
          FMOND_CONFIGFILE, NULL);
    }

  return config_file;
}

gboolean
load_config()
{
  GError *error = NULL;
  gchar *group;

  app->settings = g_key_file_new();

  if (!g_key_file_load_from_file(app->settings, app->config_file,
      G_KEY_FILE_NONE, &error))
    {
      g_printerr("%s: %s (%s)\n", app->config_file, N_("error in config file"),
          error->message);

      return FALSE;
    }

  g_key_file_set_list_separator(app->settings, ',');

  group = g_key_file_get_start_group(app->settings);
  if (!group)
    {
      g_printerr("%s: %s (%s)\n", app->config_file, N_("error in config file"),
          N_("no group 'main'"));

      return FALSE;
    }

  if (g_strcmp0(group, CONFIG_GROUP_MAIN) != 0)
    {
      g_printerr("%s: %s (%s)\n", app->config_file, N_("error in config file"),
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
  if (!g_key_file_load_from_file(settings, app->config_file, G_KEY_FILE_NONE,
      &error))
    {
      LOG_ERROR(
          "%s: %s (%s)\n",
          app->config_file, N_("error in config file, aborting reload"), error->message);

      g_key_file_free(settings);

      return FALSE;
    }

  g_key_file_set_list_separator(settings, ',');

  group = g_key_file_get_start_group(settings);
  if (!group)
    {
      LOG_ERROR("%s: %s (%s)\n",
          app->config_file, N_("error in config file"), N_("no group 'main'"));

      g_key_file_free(settings);

      return FALSE;
    }

  if (g_strcmp0(group, CONFIG_GROUP_MAIN) != 0)
    {
      LOG_ERROR(
          "%s: %s (%s)\n",
          app->config_file, N_("error in config file"), N_("the first group is not 'main'"));

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
  gchar **groups;
  gsize len;
  gint i;

  groups = g_key_file_get_groups(app->settings, &len);

  if (len < 2)
    {
      g_printerr("%s: %s (%s)\n", app->config_file, N_("error in config file"),
          N_("no watch group found"));

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
          CONFIG_KEY_WATCHER_PATH, NULL);
      if (!watcher->path)
        {
          g_printerr("%s: %s\n", watcher->name, N_("invalid path"));

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

      watcher->events = g_key_file_get_string_list(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_EVENTS, &len, NULL);
      if (len <= 0)
        {
          g_printerr("%s: %s\n", watcher->name, N_("no events found"));

          g_free(watcher->path);
          g_free(watcher->name);
          g_free(watcher);
          g_strfreev(groups);

          return NULL;
        }

      for (i = 0; watcher->events[i] != NULL; i++)
        {
          if ((g_strcmp0(watcher->events[i], CONFIG_KEY_WATCHER_EVENT_CHANGED)
              != 0)
              && (g_strcmp0(watcher->events[i],
                  CONFIG_KEY_WATCHER_EVENT_CHANGED) != 0)
              && (g_strcmp0(watcher->events[i],
                  CONFIG_KEY_WATCHER_EVENT_CREATED) != 0)
              && (g_strcmp0(watcher->events[i],
                  CONFIG_KEY_WATCHER_EVENT_DELETED) != 0)
              && (g_strcmp0(watcher->events[i],
                  CONFIG_KEY_WATCHER_EVENT_ATTRIBUTECHANGED) != 0))
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

      watcher->command = g_key_file_get_string(app->settings, watcher->name,
          CONFIG_KEY_WATCHER_COMMAND, NULL);
      if (!watcher->command)
        {
          g_printerr("%s: %s\n", watcher->name, N_("invalid command"));

          g_strfreev(watcher->events);
          g_free(watcher->path);
          g_free(watcher->name);
          g_free(watcher);
          g_strfreev(groups);

          return NULL;
        }

      list = g_slist_append(list, watcher);
    }

  g_strfreev(groups);

  return list;
}

logger_t *
init_logger()
{
  logger_t *logger = NULL;
  gboolean daemon;
  GError *error = NULL;

  daemon = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
      CONFIG_KEY_MAIN_DAEMONIZE, &error);
  if (error)
    daemon = CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT;

  if (daemon)
    {
      gboolean use_syslog;
      gint log_level;
      LoggerLevel level = LOGGER_LEVEL_NONE;

      use_syslog = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_USESYSLOG, &error);
      if (error)
        use_syslog = CONFIG_KEY_MAIN_USESYSLOG_DEFAULT;

      log_level = g_key_file_get_integer(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_LOGLEVEL, &error);
      if (error)
        log_level = CONFIG_KEY_MAIN_LOGLEVEL_DEFAULT;

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

      if (use_syslog)
        {
          gchar *syslog_facility;

          syslog_facility = g_key_file_get_string(app->settings,
              CONFIG_KEY_MAIN_GROUP, CONFIG_KEY_MAIN_SYSLOGFACILITY, &error);
          if (error)
            syslog_facility = g_strdup(CONFIG_KEY_MAIN_SYSLOGFACILITY);

          handler_t *handler = log_handler_syslog_create();
          if (!handler)
            return NULL;
          log_handler_set_option(handler, LOG_HANDLER_SYSLOG_OPTION_FACILITY,
              syslog_facility);

          logger = log_create_logger(handler, level);
        }
      else
        {
          gchar *log_file;

          log_file = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
              CONFIG_KEY_MAIN_LOGFILE, NULL);
          if (!log_file)
            log_file = g_strdup(CONFIG_KEY_MAIN_LOGFILE_DEFAULT);

          handler_t *handler = log_handler_file_create();
          if (!handler)
            return NULL;
          log_handler_set_option(handler, LOG_HANDLER_FILE_OPTION_LOGFILE,
              log_file);

          logger = log_create_logger(handler, level);
        }
    }
  else
    {
      handler_t *handler = log_handler_console_create();
      if (!handler)
        return NULL;

#ifdef DEBUG
      logger = log_create_logger(handler, LOGGER_LEVEL_ALL);
#else
      logger = log_create_logger(handler, LOGGER_LEVEL_INFO);
#endif
    }

  return logger;
}

gboolean
start_watchers()
{
  GError *error = NULL;
  GFile *file;
  GSList *item;
  watcher_t *watcher;

  for (item = app->watchers; item; item = item->next)
    {
      watcher = (watcher_t *) item->data;

      file = g_file_new_for_path(watcher->path);

      watcher->monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL,
          &error);
      g_object_unref(file);
      if (error != NULL)
        {
          LOG_DEBUG(
              "%s: %s (%s)",
              watcher->name, N_("failed to create file monitor"), error->message);

          LOG_ERROR("%s: %s", watcher->name, N_("failed to start watcher"));

          continue;
        }

      g_signal_connect(watcher->monitor, "changed", G_CALLBACK(watcher_event),
          watcher);

      LOG_INFO("%s: %s", watcher->name, N_("watcher enabled"));
    }

  return TRUE;
}

void
watcher_event(GFileMonitor *monitor, GFile *file, GFile *other_file,
    GFileMonitorEvent event_type, gpointer user_data)
{
  watcher_t *watcher;
  gchar *file_path;
  gint i;

  if (!user_data)
    return;

  watcher = (watcher_t *) user_data;
  if (file)
    file_path = g_file_get_path(file);
  else
    file_path = g_strdup("");

  LOG_DEBUG("%s: %s (event_type=%d, file:%s)",
      watcher->name, N_("watcher event received"), event_type, file_path);

  switch (event_type)
    {
  case G_FILE_MONITOR_EVENT_CHANGED:
    {
      for (i = 0; watcher->events[i] != NULL; i++)
        {
          if (g_strcmp0(watcher->events[i], CONFIG_KEY_WATCHER_EVENT_CHANGED)
              == 0)
            {
              watcher_event_fired(watcher, CONFIG_KEY_WATCHER_EVENT_CHANGED,
                  file_path);

              break;
            }
        }

      break;
    }

  case G_FILE_MONITOR_EVENT_CREATED:
    {
      for (i = 0; watcher->events[i] != NULL; i++)
        {
          if (g_strcmp0(watcher->events[i], CONFIG_KEY_WATCHER_EVENT_CREATED)
              == 0)
            {
              watcher_event_fired(watcher, CONFIG_KEY_WATCHER_EVENT_CREATED,
                  file_path);

              break;
            }
        }

      break;
    }

  case G_FILE_MONITOR_EVENT_DELETED:
    {
      for (i = 0; watcher->events[i] != NULL; i++)
        {
          if (g_strcmp0(watcher->events[i], CONFIG_KEY_WATCHER_EVENT_DELETED)
              == 0)
            {
              watcher_event_fired(watcher, CONFIG_KEY_WATCHER_EVENT_DELETED,
                  file_path);

              break;
            }
        }

      break;
    }

  case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
    {
      for (i = 0; watcher->events[i] != NULL; i++)
        {
          if (g_strcmp0(watcher->events[i],
              CONFIG_KEY_WATCHER_EVENT_ATTRIBUTECHANGED) == 0)
            {
              watcher_event_fired(watcher,
                  CONFIG_KEY_WATCHER_EVENT_ATTRIBUTECHANGED, file_path);

              break;
            }
        }

      break;
    }

  default:
    {
      break;
    }
    }

  g_free(file_path);
}

void
watcher_event_fired(watcher_t *watcher, gchar *event, gchar *file)
{
  GError *error = NULL;
  GRegex *regex;
  gchar *cmd, *tmp;

  LOG_INFO( "%s: %s (event=%s, file:%s)",
      watcher->name, N_("watcher event fired"), event, file);

  regex = g_regex_new("\\" CONFIG_KEY_WATCHER_COMMAND_KEY_NAME, 0, 0, &error);
  cmd = g_regex_replace_literal(regex, watcher->command, -1, 0, watcher->name,
      0, &error);
  g_regex_unref(regex);

  tmp = cmd;
  regex = g_regex_new("\\" CONFIG_KEY_WATCHER_COMMAND_KEY_PATH, 0, 0, &error);
  cmd = g_regex_replace_literal(regex, tmp, -1, 0, watcher->path, 0, &error);
  g_regex_unref(regex);
  g_free(tmp);

  tmp = cmd;
  regex = g_regex_new("\\" CONFIG_KEY_WATCHER_COMMAND_KEY_EVENT, 0, 0, &error);
  cmd = g_regex_replace_literal(regex, tmp, -1, 0, event, 0, &error);
  g_regex_unref(regex);
  g_free(tmp);

  tmp = cmd;
  regex = g_regex_new("\\" CONFIG_KEY_WATCHER_COMMAND_KEY_FILE, 0, 0, &error);
  cmd = g_regex_replace_literal(regex, tmp, -1, 0, file, 0, &error);
  g_regex_unref(regex);
  g_free(tmp);

  LOG_INFO("%s: %s '%s'", watcher->name, N_("executing command"), cmd);

  g_spawn_command_line_async(cmd, &error);
  g_free(cmd);
  if (error)
    {
      LOG_ERROR("%s: %s", watcher->name, N_("failed to execute command"));

      return;
    }
}

void
version()
{
  g_print("%s\n", PACKAGE_STRING);
  g_print("%s\n", FMOND_COPYRIGHT);
  g_print("\n");
  g_print("%s\n", FMOND_LICENCE);
  g_print("\n");
}

void
parse_command_line(gint argc, gchar *argv[])
{
  GOptionContext *context;
  gchar *help;
  gchar *config_file = NULL;
  gboolean verbose = FALSE;
  gint show_version = 0;
  GError *error = NULL;
  gboolean ret;

  GOptionEntry entries[] =
    {
      { "file", 'f', 0, G_OPTION_ARG_FILENAME, &config_file,
          N_("read configuration from file"), N_("file") },
          { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
              N_("set verbose output") },
          { "version", 0, 0, G_OPTION_ARG_NONE, &show_version,
              N_("show version information"), NULL },
          { NULL } };

  context = g_option_context_new(NULL);
  g_option_context_add_main_entries(context, entries, PACKAGE);

  ret = g_option_context_parse(context, &argc, &argv, &error);

  help = g_option_context_get_help(context, TRUE, NULL);

  g_option_context_free(context);

  if (!ret)
    {
      g_print("%s", help);

      g_free(help);
      exit(1);
    }

  g_free(help);

  if (show_version == 1)
    {
      version();

      exit(0);
    }

  if (config_file)
    app->config_file = g_strdup(config_file);
  else
    app->config_file = get_default_config_file();

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
cleanup(void)
{
  GError *error = NULL;
  gboolean daemon;

  g_main_loop_quit(app->loop);

  if (app->settings && app->daemon)
    {
      daemon = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_DAEMONIZE, &error);
      if (error)
        daemon = CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT;
      if (daemon)
        {
          gchar *pid_file;

          pid_file = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
              CONFIG_KEY_MAIN_PIDFILE, NULL);
          if (pid_file)
            {
              g_unlink(pid_file);
              g_free(pid_file);
            }
        }

      LOG_INFO("%s %s", PACKAGE, N_("daemon stopped"));
    }

  if (app->logger)
    log_destroy_logger(app->logger);

  if (app->watchers)
    {
      GSList *item;
      watcher_t *watcher;

      for (item = app->watchers; item; item = item->next)
        {
          watcher = (watcher_t *) item->data;
          if (!watcher)
            continue;

          if (watcher->monitor)
            {
              if (!g_file_monitor_is_cancelled(watcher->monitor))
                g_file_monitor_cancel(watcher->monitor);

              g_object_unref(watcher->monitor);
            }

          g_strfreev(watcher->events);
          g_free(watcher->command);
          g_free(watcher->path);
          g_free(watcher->name);
          g_free(watcher);
        }

      g_slist_free(app->watchers);
    }

  if (app->settings)
    g_key_file_free(app->settings);

  g_free(app->config_file);
  g_free(app);
}

int
main(int argc, char *argv[])
{
  GError *error = NULL;
  gboolean daemon;
  gint ret;

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  g_type_init();

  app = g_new0(application_t, 1);
  app->loop = g_main_loop_new(NULL, TRUE);
  atexit(cleanup);

  parse_command_line(argc, argv);

  if (!load_config())
    exit(-1);

  app->watchers = init_watchers();
  if (!app->watchers)
    exit(-3);

  app->logger = init_logger();
  if (!app->logger)
    {
      g_printerr("%s\n", N_("failed to create logger"));

      exit(-2);
    }

  daemon = g_key_file_get_boolean(app->settings, CONFIG_GROUP_MAIN,
      CONFIG_KEY_MAIN_DAEMONIZE, &error);
  if (error)
    daemon = CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT;
  if (daemon)
    {
      gchar *pid_file, *user, *group;

      pid_file = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_PIDFILE, NULL);

      if (!pid_file)
        pid_file = g_strdup(CONFIG_KEY_MAIN_PIDFILE_DEFAULT);

      user = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_USER, NULL);
      if (!user)
        user = g_strdup(CONFIG_KEY_MAIN_USER_DEFAULT);

      group = g_key_file_get_string(app->settings, CONFIG_GROUP_MAIN,
          CONFIG_KEY_MAIN_GROUP, NULL);
      if (!group)
        group = g_strdup(CONFIG_KEY_MAIN_GROUP_DEFAULT);

      ret = daemonize(pid_file, user, group);

      g_free(pid_file);
      g_free(user);
      g_free(group);

      if (ret < 0)
        {
          LOG_ERROR("%s: %d", N_("failed to daemonize, error code"), ret);

          exit(-4);
        }

      app->daemon = TRUE;
      LOG_INFO("%s %s", PACKAGE, N_("daemon started"));
    }

  signal(SIGPIPE, sigpipe);
  signal(SIGHUP, sighup);
  signal(SIGINT, sigint);
  signal(SIGTERM, sigterm);

  if (!start_watchers())
    exit(-5);

  g_main_loop_run(app->loop);

  return 0;
}
