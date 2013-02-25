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

#ifndef RSU_TASK_H__
#define RSU_TASK_H__

#include <gio/gio.h>
#include <glib.h>

#include <libdleyna/core/connector.h>
#include <libdleyna/core/task-atom.h>

enum rsu_task_type_t_ {
	RSU_TASK_GET_VERSION,
	RSU_TASK_GET_SERVERS,
	RSU_TASK_RAISE,
	RSU_TASK_QUIT,
	RSU_TASK_SET_PROP,
	RSU_TASK_GET_ALL_PROPS,
	RSU_TASK_GET_PROP,
	RSU_TASK_PAUSE,
	RSU_TASK_PLAY,
	RSU_TASK_PLAY_PAUSE,
	RSU_TASK_STOP,
	RSU_TASK_NEXT,
	RSU_TASK_PREVIOUS,
	RSU_TASK_OPEN_URI,
	RSU_TASK_SEEK,
	RSU_TASK_SET_POSITION,
	RSU_TASK_HOST_URI,
	RSU_TASK_REMOVE_URI
};
typedef enum rsu_task_type_t_ rsu_task_type_t;

typedef void (*rsu_cancel_task_t)(void *handle);

typedef struct rsu_task_get_props_t_ rsu_task_get_props_t;
struct rsu_task_get_props_t_ {
	gchar *interface_name;
};

typedef struct rsu_task_get_prop_t_ rsu_task_get_prop_t;
struct rsu_task_get_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
};

typedef struct rsu_task_set_prop_t_ rsu_task_set_prop_t;
struct rsu_task_set_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
	GVariant *params;
};

typedef struct rsu_task_open_uri_t_ rsu_task_open_uri_t;
struct rsu_task_open_uri_t_ {
	gchar *uri;
};

typedef struct rsu_task_seek_t_ rsu_task_seek_t;
struct rsu_task_seek_t_ {
	gint64 position;
};

typedef struct rsu_task_host_uri_t_ rsu_task_host_uri_t;
struct rsu_task_host_uri_t_ {
	gchar *uri;
	gchar *client;
};

typedef struct rsu_task_t_ rsu_task_t;
struct rsu_task_t_ {
	dleyna_task_atom_t atom; /* pseudo inheritance - MUST be first field */
	rsu_task_type_t type;
	gchar *path;
	const gchar *result_format;
	GVariant *result;
	dleyna_connector_msg_id_t invocation;
	gboolean synchronous;
	union {
		rsu_task_get_props_t get_props;
		rsu_task_get_prop_t get_prop;
		rsu_task_set_prop_t set_prop;
		rsu_task_open_uri_t open_uri;
		rsu_task_host_uri_t host_uri;
		rsu_task_seek_t seek;
	} ut;
};

rsu_task_t *rsu_task_get_version_new(dleyna_connector_msg_id_t invocation);
rsu_task_t *rsu_task_get_servers_new(dleyna_connector_msg_id_t invocation);
rsu_task_t *rsu_task_raise_new(dleyna_connector_msg_id_t invocation);
rsu_task_t *rsu_task_quit_new(dleyna_connector_msg_id_t invocation);
rsu_task_t *rsu_task_set_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_get_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_get_props_new(dleyna_connector_msg_id_t invocation,
				   const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_play_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);
rsu_task_t *rsu_task_pause_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);
rsu_task_t *rsu_task_play_pause_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path);
rsu_task_t *rsu_task_stop_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);
rsu_task_t *rsu_task_next_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path);
rsu_task_t *rsu_task_previous_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path);
rsu_task_t *rsu_task_seek_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_set_position_new(dleyna_connector_msg_id_t invocation,
				      const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_open_uri_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_host_uri_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);
rsu_task_t *rsu_task_remove_uri_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path, GVariant *parameters);
void rsu_task_complete(rsu_task_t *task);
void rsu_task_fail(rsu_task_t *task, GError *error);
void rsu_task_delete(rsu_task_t *task);
void rsu_task_cancel(rsu_task_t *task);

#endif
