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

#ifndef DLR_TASK_H__
#define DLR_TASK_H__

#include <gio/gio.h>
#include <glib.h>

#include <libdleyna/core/connector.h>
#include <libdleyna/core/task-atom.h>

enum dlr_task_type_t_ {
	DLR_TASK_GET_VERSION,
	DLR_TASK_GET_SERVERS,
	DLR_TASK_RESCAN,
	DLR_TASK_RAISE,
	DLR_TASK_QUIT,
	DLR_TASK_SET_PROP,
	DLR_TASK_GET_ALL_PROPS,
	DLR_TASK_GET_PROP,
	DLR_TASK_PAUSE,
	DLR_TASK_PLAY,
	DLR_TASK_PLAY_PAUSE,
	DLR_TASK_STOP,
	DLR_TASK_NEXT,
	DLR_TASK_PREVIOUS,
	DLR_TASK_OPEN_URI,
	DLR_TASK_OPEN_NEXT_URI,
	DLR_TASK_SET_URI,
	DLR_TASK_SEEK,
	DLR_TASK_BYTE_SEEK,
	DLR_TASK_SET_POSITION,
	DLR_TASK_SET_BYTE_POSITION,
	DLR_TASK_GOTO_TRACK,
	DLR_TASK_HOST_URI,
	DLR_TASK_REMOVE_URI,
	DLR_TASK_GET_ICON,
	DLR_TASK_MANAGER_GET_ALL_PROPS,
	DLR_TASK_MANAGER_GET_PROP,
	DLR_TASK_MANAGER_SET_PROP
};
typedef enum dlr_task_type_t_ dlr_task_type_t;

typedef void (*dlr_cancel_task_t)(void *handle);

typedef struct dlr_task_get_props_t_ dlr_task_get_props_t;
struct dlr_task_get_props_t_ {
	gchar *interface_name;
};

typedef struct dlr_task_get_prop_t_ dlr_task_get_prop_t;
struct dlr_task_get_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
};

typedef struct dlr_task_set_prop_t_ dlr_task_set_prop_t;
struct dlr_task_set_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
	GVariant *params;
};

typedef struct dlr_task_open_uri_t_ dlr_task_open_uri_t;
struct dlr_task_open_uri_t_ {
	gchar *uri;
	gchar *metadata;
	const gchar *operation;
	const gchar *uri_type;
	const gchar *metadata_type;
};

typedef struct dlr_task_seek_t_ dlr_task_seek_t;
struct dlr_task_seek_t_ {
	guint64 counter_position;
	gint64 position;
	guint32 track_number;
};

typedef struct dlr_task_host_uri_t_ dlr_task_host_uri_t;
struct dlr_task_host_uri_t_ {
	gchar *uri;
	gchar *client;
};

typedef struct dlr_task_get_icon_t_ dlr_task_get_icon_t;
struct dlr_task_get_icon_t_ {
	gchar *mime_type;
	gchar *resolution;
};

typedef struct dlr_task_t_ dlr_task_t;
struct dlr_task_t_ {
	dleyna_task_atom_t atom; /* pseudo inheritance - MUST be first field */
	dlr_task_type_t type;
	gchar *path;
	const gchar *result_format;
	GVariant *result;
	dleyna_connector_msg_id_t invocation;
	gboolean synchronous;
	gboolean multiple_retvals;
	union {
		dlr_task_get_props_t get_props;
		dlr_task_get_prop_t get_prop;
		dlr_task_set_prop_t set_prop;
		dlr_task_open_uri_t open_uri;
		dlr_task_host_uri_t host_uri;
		dlr_task_seek_t seek;
		dlr_task_get_icon_t get_icon;
	} ut;
};

dlr_task_t *dlr_task_rescan_new(dleyna_connector_msg_id_t invocation);

dlr_task_t *dlr_task_get_version_new(dleyna_connector_msg_id_t invocation);

dlr_task_t *dlr_task_get_servers_new(dleyna_connector_msg_id_t invocation);

dlr_task_t *dlr_task_raise_new(dleyna_connector_msg_id_t invocation);

dlr_task_t *dlr_task_quit_new(dleyna_connector_msg_id_t invocation);

dlr_task_t *dlr_task_set_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_get_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_get_props_new(dleyna_connector_msg_id_t invocation,
				   const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_play_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);

dlr_task_t *dlr_task_pause_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);

dlr_task_t *dlr_task_play_pause_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path);

dlr_task_t *dlr_task_stop_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);

dlr_task_t *dlr_task_next_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);

dlr_task_t *dlr_task_previous_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path);

dlr_task_t *dlr_task_seek_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_byte_seek_new(dleyna_connector_msg_id_t invocation,
				   const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_set_position_new(dleyna_connector_msg_id_t invocation,
				      const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_set_byte_position_new(dleyna_connector_msg_id_t invocation,
					   const gchar *path,
					   GVariant *parameters);

dlr_task_t *dlr_task_goto_track_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_open_uri_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_open_uri_ex_new(dleyna_connector_msg_id_t invocation,
				     const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_open_next_uri_new(dleyna_connector_msg_id_t invocation,
				       const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_set_uri_new(dleyna_connector_msg_id_t invocation,
				 const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_host_uri_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, const gchar *sender,
				  GVariant *parameters);

dlr_task_t *dlr_task_remove_uri_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path, const gchar *sender,
				    GVariant *parameters);

dlr_task_t *dlr_task_get_icon_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dlr_task_t *dlr_task_manager_get_prop_new(dleyna_connector_msg_id_t invocation,
					  const gchar *path,
					  GVariant *parameters,
					  GError **error);

dlr_task_t *dlr_task_manager_get_props_new(dleyna_connector_msg_id_t invocation,
					   const gchar *path,
					   GVariant *parameters,
					   GError **error);

dlr_task_t *dlr_task_manager_set_prop_new(dleyna_connector_msg_id_t invocation,
					  const gchar *path,
					  GVariant *parameters,
					  GError **error);

void dlr_task_complete(dlr_task_t *task);

void dlr_task_fail(dlr_task_t *task, GError *error);

void dlr_task_delete(dlr_task_t *task);

void dlr_task_cancel(dlr_task_t *task);

#endif
