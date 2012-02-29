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

#ifndef FMOND_H_
#define FMOND_H_

#include "common.h"
#include "log.h"

#define FMOND_COPYRIGHT                                 "Copyright (C) 2011 Boris HUISGEN <bhuisgen@hbis.fr>"
#define FMOND_LICENCE                                   "This is free software; see the source for copying conditions.  There is NO\n" \
                                                        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."

#define FMOND_HOMEDIR                                   "." PACKAGE
#define FMOND_CONFIGFILE                                PACKAGE ".conf"

#define CONFIG_GROUP_MAIN                               "main"
#define CONFIG_KEY_MAIN_DAEMONIZE                       "Daemonize"
#define CONFIG_KEY_MAIN_DAEMONIZE_NO                    0
#define CONFIG_KEY_MAIN_DAEMONIZE_YES                   1
#define CONFIG_KEY_MAIN_DAEMONIZE_DEFAULT               CONFIG_KEY_MAIN_DAEMONIZE_NO
#define CONFIG_KEY_MAIN_PIDFILE                         "PIDFile"
#define CONFIG_KEY_MAIN_PIDFILE_DEFAULT                 "/var/run/" PACKAGE "/" PACKAGE ".pid"
#define CONFIG_KEY_MAIN_USER                            "User"
#define CONFIG_KEY_MAIN_GROUP                           "Group"
#define CONFIG_KEY_MAIN_LOGLEVEL                        "LogLevel"
#define CONFIG_KEY_MAIN_LOGLEVEL_NONE                   0
#define CONFIG_KEY_MAIN_LOGLEVEL_ERROR                  1
#define CONFIG_KEY_MAIN_LOGLEVEL_WARNING                2
#define CONFIG_KEY_MAIN_LOGLEVEL_INFO                   3
#define CONFIG_KEY_MAIN_LOGLEVEL_DEBUG                  4
#define CONFIG_KEY_MAIN_LOGLEVEL_DEFAULT                CONFIG_KEY_MAIN_LOGLEVEL_INFO
#define CONFIG_KEY_MAIN_LOGFILE                         "LogFile"
#define CONFIG_KEY_MAIN_LOGFILE_DEFAULT                 "/var/log/" PACKAGE "/" PACKAGE ".log"
#define CONFIG_KEY_MAIN_USESYSLOG                       "UseSyslog"
#define CONFIG_KEY_MAIN_USESYSLOG_NO                    0
#define CONFIG_KEY_MAIN_USESYSLOG_YES                   1
#define CONFIG_KEY_MAIN_USESYSLOG_DEFAULT               CONFIG_KEY_MAIN_USESYSLOG_NO
#define CONFIG_KEY_MAIN_SYSLOGFACILITY                  "SyslogFacility"
#define CONFIG_KEY_MAIN_SYSLOGFACILITY_DEFAULT          "DAEMON";
#define CONFIG_KEY_WATCHER_PATH                         "Path"
#define CONFIG_KEY_WATCHER_EVENTS                       "Events"
#define CONFIG_KEY_WATCHER_EVENT_CHANGED                "changed"
#define CONFIG_KEY_WATCHER_EVENT_CREATED                "created"
#define CONFIG_KEY_WATCHER_EVENT_DELETED                "deleted"
#define CONFIG_KEY_WATCHER_EVENT_ATTRIBUTECHANGED       "attribute_changed"
#define CONFIG_KEY_WATCHER_COMMAND                      "Command"
#define CONFIG_KEY_WATCHER_COMMAND_KEY_NAME             "$name"
#define CONFIG_KEY_WATCHER_COMMAND_KEY_PATH             "$path"
#define CONFIG_KEY_WATCHER_COMMAND_KEY_EVENT            "$event"
#define CONFIG_KEY_WATCHER_COMMAND_KEY_FILE             "$file"
#define CONFIG_KEY_WATCHER_COMMAND_KEY_RFILE            "$rfile"
#define CONFIG_KEY_WATCHER_INCLUDE                      "Include"
#define CONFIG_KEY_WATCHER_EXCLUDE                      "Exclude"

typedef struct _application_t
{
  GMainLoop *loop;
  gboolean daemon;
  logger_t *logger;
  GKeyFile *settings;
  GSList *watchers;
  gchar *config_file;
  gboolean verbose;
} application_t;

extern application_t *app;

#endif /* FMOND_H_ */