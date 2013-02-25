/*
 * dleyna
 *
 * Copyright (C) 2012-2013 Intel Corporation. All rights reserved.
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
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>

#include "async.h"

void rsu_async_task_delete(rsu_async_task_t *task)
{
	if (task->free_private)
		task->free_private(task->private);
	if (task->cancellable)
		g_object_unref(task->cancellable);
}

gboolean rsu_async_task_complete(gpointer user_data)
{
	rsu_async_task_t *cb_data = user_data;

	DLEYNA_LOG_DEBUG("Enter. Error %p", (void *)cb_data->error);
	DLEYNA_LOG_DEBUG_NL();

	cb_data->device->current_task = NULL;

	if (cb_data->proxy != NULL)
		g_object_remove_weak_pointer((G_OBJECT(cb_data->proxy)),
					     (gpointer *)&cb_data->proxy);

	cb_data->cb(&cb_data->task, cb_data->error);

	return FALSE;
}

void rsu_async_task_cancelled(GCancellable *cancellable, gpointer user_data)
{
	rsu_async_task_t *cb_data = user_data;

	cb_data->device->current_task = NULL;
	gupnp_service_proxy_cancel_action(cb_data->proxy, cb_data->action);
	if (!cb_data->error)
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_CANCELLED,
					     "Operation cancelled.");
	(void) g_idle_add(rsu_async_task_complete, cb_data);
}

void rsu_async_task_lost_object(gpointer user_data)
{
	rsu_async_task_t *cb_data = user_data;

	if (!cb_data->error)
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_LOST_OBJECT,
					     "Renderer died before command "
					     "could be completed.");
	(void) g_idle_add(rsu_async_task_complete, cb_data);
}

void rsu_async_task_cancel(rsu_async_task_t *task)
{
	if (task->cancellable)
		g_cancellable_cancel(task->cancellable);
}
