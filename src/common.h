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

#ifndef COMMON_H_
#define COMMON_H_

#include <config.h>
#include <gettext.h>
#define _(String) gettext (String)
#define N_(String) String
#include <locale.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixmounts.h>

#endif /* COMMON_H_ */
