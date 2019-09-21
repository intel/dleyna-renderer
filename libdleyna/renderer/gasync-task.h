/*
 * dLeyna
 *
 * Copyright (c) 2019 Jens Georg <mail@jensge.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#pragma once

#include <libdleyna/core/task-atom.h>

#include <glib.h>
#include <gio/gio.h>

typedef struct dleyna_gasync_task_t_ dleyna_gasync_task_t;

typedef gboolean (*dleyna_gasync_task_action)
	(dleyna_gasync_task_t *task,
	 GObject *target);

const char *dleyna_gasync_task_create_source(void);

void dleyna_gasync_task_add(const dleyna_task_queue_key_t *queue_id,
		dleyna_gasync_task_action action,
		GObject *target,
		GAsyncReadyCallback callback,
		GCancellable *cancellable,
		GDestroyNotify free_func,
		gpointer cb_user_data);

void dleyna_gasync_task_ready_cb(GObject *source, GAsyncResult *res, gpointer user_data);

void dleyna_gasync_task_process_cb(dleyna_task_atom_t *atom,
		gpointer user_data);

void dleyna_gasync_task_cancel_cb(dleyna_task_atom_t *atom,
		gpointer user_data);

void dleyna_gasync_task_delete_cb(dleyna_task_atom_t *atom,
		gpointer user_data);

gpointer dleyna_gasync_task_get_user_data(dleyna_gasync_task_t *task);

GCancellable *dleyna_gasync_task_get_cancellable(dleyna_gasync_task_t *task);
