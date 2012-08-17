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

#ifndef WATCHER_H_
#define WATCHER_H_

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
  gboolean readable;
  gboolean writable;
  gboolean executable;
  gint size;
  guint size_unit;
#define WATCHER_SIZE_UNIT_BYTES         0
#define WATCHER_SIZE_UNIT_KBYTES        1
#define WATCHER_SIZE_UNIT_MBYTES        2
#define WATCHER_SIZE_UNIT_GBYTES        3
  guint size_cmp;
#define WATCHER_SIZE_COMPARE_EQUAL      0
#define WATCHER_SIZE_COMPARE_GREATER    1
#define WATCHER_SIZE_COMPARE_LESS       2
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

#endif /* WATCHER_H_ */
