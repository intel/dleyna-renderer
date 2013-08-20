/*
 * dLeyna
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
#include <libdleyna/core/task-processor.h>

#include "async.h"
#include "server.h"

dlr_task_t *dlr_task_rescan_new(dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task = g_new0(dlr_task_t, 1);

	task->type = DLR_TASK_RESCAN;
	task->invocation = invocation;
	task->synchronous = TRUE;

	return task;
}

dlr_task_t *dlr_task_get_version_new(dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task = g_new0(dlr_task_t, 1);

	task->type = DLR_TASK_GET_VERSION;
	task->invocation = invocation;
	task->result_format = "(@s)";
	task->result = g_variant_ref_sink(g_variant_new_string(VERSION));
	task->synchronous = TRUE;

	return task;
}

dlr_task_t *dlr_task_get_servers_new(dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task = g_new0(dlr_task_t, 1);

	task->type = DLR_TASK_GET_SERVERS;
	task->invocation = invocation;
	task->result_format = "(@ao)";
	task->synchronous = TRUE;

	return task;
}

dlr_task_t *dlr_task_raise_new(dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task = g_new0(dlr_task_t, 1);

	task->type = DLR_TASK_RAISE;
	task->invocation = invocation;
	task->synchronous = TRUE;

	return task;
}

dlr_task_t *dlr_task_quit_new(dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task = g_new0(dlr_task_t, 1);

	task->type = DLR_TASK_QUIT;
	task->invocation = invocation;
	task->synchronous = TRUE;

	return task;
}

static void prv_dlr_task_delete(dlr_task_t *task)
{
	if (!task->synchronous)
		dlr_async_task_delete((dlr_async_task_t *)task);

	switch (task->type) {
	case DLR_TASK_GET_ALL_PROPS:
		g_free(task->ut.get_props.interface_name);
		break;
	case DLR_TASK_GET_PROP:
		g_free(task->ut.get_prop.interface_name);
		g_free(task->ut.get_prop.prop_name);
		break;
	case DLR_TASK_SET_PROP:
		g_free(task->ut.set_prop.interface_name);
		g_free(task->ut.set_prop.prop_name);
		g_variant_unref(task->ut.set_prop.params);
		break;
	case DLR_TASK_OPEN_URI:
		g_free(task->ut.open_uri.uri);
		g_free(task->ut.open_uri.metadata);
		break;
	case DLR_TASK_HOST_URI:
	case DLR_TASK_REMOVE_URI:
		g_free(task->ut.host_uri.uri);
		g_free(task->ut.host_uri.client);
		break;
	case DLR_TASK_GET_ICON:
		g_free(task->ut.get_icon.mime_type);
		g_free(task->ut.get_icon.resolution);
		break;
	default:
		break;
	}

	g_free(task->path);
	if (task->result)
		g_variant_unref(task->result);

	g_free(task);
}

static dlr_task_t *prv_device_task_new(dlr_task_type_t type,
				       dleyna_connector_msg_id_t invocation,
				       const gchar *path,
				       const gchar *result_format)
{
	dlr_task_t *task = (dlr_task_t *)g_new0(dlr_async_task_t, 1);

	task->type = type;
	task->invocation = invocation;
	task->result_format = result_format;

	task->path = g_strdup(path);
	g_strstrip(task->path);

	return task;
}

dlr_task_t *dlr_task_get_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_GET_PROP, invocation, path, "(v)");

	g_variant_get(parameters, "(ss)", &task->ut.get_prop.interface_name,
		      &task->ut.get_prop.prop_name);

	g_strstrip(task->ut.get_prop.interface_name);
	g_strstrip(task->ut.get_prop.prop_name);

	return task;
}

dlr_task_t *dlr_task_get_props_new(dleyna_connector_msg_id_t invocation,
				   const gchar *path, GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_GET_ALL_PROPS, invocation, path,
				   "(@a{sv})");

	g_variant_get(parameters, "(s)", &task->ut.get_props.interface_name);
	g_strstrip(task->ut.get_props.interface_name);

	return task;
}

dlr_task_t *dlr_task_set_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_SET_PROP, invocation, path, NULL);

	g_variant_get(parameters, "(ssv)", &task->ut.set_prop.interface_name,
		      &task->ut.set_prop.prop_name, &task->ut.set_prop.params);

	g_strstrip(task->ut.set_prop.interface_name);
	g_strstrip(task->ut.set_prop.prop_name);

	return task;
}

dlr_task_t *dlr_task_play_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path)
{
	return prv_device_task_new(DLR_TASK_PLAY, invocation, path, NULL);
}

dlr_task_t *dlr_task_pause_new(dleyna_connector_msg_id_t invocation,
			       const gchar *path)
{
	return prv_device_task_new(DLR_TASK_PAUSE, invocation, path, NULL);
}

dlr_task_t *dlr_task_play_pause_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path)
{
	return prv_device_task_new(DLR_TASK_PLAY_PAUSE, invocation, path, NULL);
}

dlr_task_t *dlr_task_stop_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path)
{
	return prv_device_task_new(DLR_TASK_STOP, invocation, path, NULL);
}

dlr_task_t *dlr_task_next_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path)
{
	return prv_device_task_new(DLR_TASK_NEXT, invocation, path, NULL);
}

dlr_task_t *dlr_task_previous_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path)
{
	return prv_device_task_new(DLR_TASK_PREVIOUS, invocation, path, NULL);
}

dlr_task_t *dlr_task_seek_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path, GVariant *parameters)
{
	dlr_task_t *task = prv_device_task_new(DLR_TASK_SEEK, invocation,
					       path, NULL);

	g_variant_get(parameters, "(x)", &task->ut.seek.position);

	return task;
}

dlr_task_t *dlr_task_set_position_new(dleyna_connector_msg_id_t invocation,
				      const gchar *path, GVariant *parameters)
{
	gchar *track_id;

	dlr_task_t *task = prv_device_task_new(DLR_TASK_SET_POSITION,
					       invocation, path, NULL);

	g_variant_get(parameters, "(&ox)", &track_id, &task->ut.seek.position);

	return task;
}

dlr_task_t *dlr_task_goto_track_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path, GVariant *parameters)
{
	dlr_task_t *task = prv_device_task_new(DLR_TASK_GOTO_TRACK,
					       invocation, path, NULL);

	g_variant_get(parameters, "(u)", &task->ut.seek.track_number);

	return task;
}

dlr_task_t *dlr_task_open_uri_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_OPEN_URI, invocation, path,
				   NULL);

	g_variant_get(parameters, "(s)", &task->ut.open_uri.uri);
	g_strstrip(task->ut.open_uri.uri);

	task->ut.open_uri.metadata = NULL;

	return task;
}

dlr_task_t *dlr_task_open_uri_ex_new(dleyna_connector_msg_id_t invocation,
				     const gchar *path, GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_OPEN_URI, invocation, path,
				   NULL);

	g_variant_get(parameters, "(ss)",
		      &task->ut.open_uri.uri, &task->ut.open_uri.metadata);
	g_strstrip(task->ut.open_uri.uri);
	g_strstrip(task->ut.open_uri.metadata);

	return task;
}

dlr_task_t *dlr_task_host_uri_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path,
				  const gchar *sender,
				  GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_HOST_URI, invocation, path,
				   "(@s)");

	g_variant_get(parameters, "(s)", &task->ut.host_uri.uri);
	g_strstrip(task->ut.host_uri.uri);
	task->ut.host_uri.client = g_strdup(sender);

	return task;
}

dlr_task_t *dlr_task_remove_uri_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path,
				    const gchar *sender,
				    GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_REMOVE_URI, invocation, path, NULL);

	g_variant_get(parameters, "(s)", &task->ut.host_uri.uri);
	g_strstrip(task->ut.host_uri.uri);
	task->ut.host_uri.client = g_strdup(sender);

	return task;
}

dlr_task_t *dlr_task_get_icon_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dlr_task_t *task;

	task = prv_device_task_new(DLR_TASK_GET_ICON, invocation, path,
				   "(@ays)");
	task->multiple_retvals = TRUE;

	g_variant_get(parameters, "(ss)", &task->ut.get_icon.mime_type,
		      &task->ut.get_icon.resolution);

	return task;
}

void dlr_task_complete(dlr_task_t *task)
{
	GVariant *result;

	if (!task)
		goto finished;

	if (task->invocation) {
		if (task->result_format && task->result) {
			if (task->multiple_retvals)
				result = task->result;
			else
				result = g_variant_new(task->result_format,
						       task->result);
			dlr_renderer_get_connector()->return_response(
						task->invocation, result);
		} else {
			dlr_renderer_get_connector()->return_response(
							task->invocation,
							NULL);
		}

		task->invocation = NULL;
	}

finished:

	return;
}

void dlr_task_fail(dlr_task_t *task, GError *error)
{
	if (!task)
		goto finished;

	if (task->invocation) {
		dlr_renderer_get_connector()->return_error(task->invocation,
							   error);
		task->invocation = NULL;
	}

finished:

	return;
}

void dlr_task_cancel(dlr_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(DLEYNA_SERVER_ERROR, DLEYNA_ERROR_CANCELLED,
				    "Operation cancelled.");
		dlr_renderer_get_connector()->return_error(task->invocation,
							   error);
		task->invocation = NULL;
		g_error_free(error);
	}

	if (!task->synchronous)
		dlr_async_task_cancel((dlr_async_task_t *)task);

finished:

	return;
}

void dlr_task_delete(dlr_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(DLEYNA_SERVER_ERROR, DLEYNA_ERROR_DIED,
				    "Unable to complete command.");
		dlr_renderer_get_connector()->return_error(task->invocation,
							   error);
		g_error_free(error);
	}

	prv_dlr_task_delete(task);

finished:

	return;
}
