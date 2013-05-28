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

#ifndef DLR_UPNP_H__
#define DLR_UPNP_H__

#include <libdleyna/core/connector.h>

#include "server.h"
#include "task.h"

enum dlr_interface_type_ {
	DLR_INTERFACE_INFO_PROPERTIES,
	DLR_INTERFACE_INFO_ROOT,
	DLR_INTERFACE_INFO_PLAYER,
	DLR_INTERFACE_INFO_PUSH_HOST,
	DLR_INTERFACE_INFO_DEVICE,
	DLR_INTERFACE_INFO_MAX
};

typedef void (*dlr_upnp_callback_t)(const gchar *path);
typedef void (*dlr_upnp_task_complete_t)(dlr_task_t *task, GError *error);

dlr_upnp_t *dlr_upnp_new(dleyna_connector_id_t connection,
			 const dleyna_connector_dispatch_cb_t *dispatch_table,
			 dlr_upnp_callback_t found_server,
			 dlr_upnp_callback_t lost_server);

void dlr_upnp_delete(dlr_upnp_t *upnp);

GVariant *dlr_upnp_get_server_ids(dlr_upnp_t *upnp);

GHashTable *dlr_upnp_get_server_udn_map(dlr_upnp_t *upnp);

void dlr_upnp_set_prop(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb);

void dlr_upnp_get_prop(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb);

void dlr_upnp_get_all_props(dlr_upnp_t *upnp, dlr_task_t *task,
			    dlr_upnp_task_complete_t cb);

void dlr_upnp_play(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb);

void dlr_upnp_pause(dlr_upnp_t *upnp, dlr_task_t *task,
		    dlr_upnp_task_complete_t cb);

void dlr_upnp_play_pause(dlr_upnp_t *upnp, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

void dlr_upnp_stop(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb);

void dlr_upnp_next(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb);

void dlr_upnp_previous(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb);

void dlr_upnp_open_uri(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb);

void dlr_upnp_seek(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb);

void dlr_upnp_set_position(dlr_upnp_t *upnp, dlr_task_t *task,
			   dlr_upnp_task_complete_t cb);

void dlr_upnp_goto_track(dlr_upnp_t *upnp, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

void dlr_upnp_host_uri(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb);

void dlr_upnp_remove_uri(dlr_upnp_t *upnp, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

void dlr_upnp_get_icon(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb);

void dlr_upnp_lost_client(dlr_upnp_t *upnp, const gchar *client_name);

void dlr_upnp_unsubscribe(dlr_upnp_t *upnp);

void dlr_upnp_rescan(dlr_upnp_t *upnp);

#endif /* DLR_UPNP_H__ */
