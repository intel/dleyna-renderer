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

#ifndef RSU_DEVICE_H__
#define RSU_DEVICE_H__

#include <gio/gio.h>
#include <glib.h>

#include <libgupnp/gupnp-service-proxy.h>
#include <libgupnp/gupnp-device-proxy.h>

#include <libdleyna/core/connector.h>

#include "host-service.h"
#include "upnp.h"

typedef struct rsu_device_t_ rsu_device_t;

typedef struct rsu_service_proxies_t_ rsu_service_proxies_t;
struct rsu_service_proxies_t_ {
	GUPnPServiceProxy *cm_proxy;
	GUPnPServiceProxy *av_proxy;
	GUPnPServiceProxy *rc_proxy;
};

typedef struct rsu_device_context_t_ rsu_device_context_t;
struct rsu_device_context_t_ {
	gchar *ip_address;
	GUPnPDeviceProxy *device_proxy;
	rsu_service_proxies_t service_proxies;
	rsu_device_t *device;
	gboolean subscribed_av;
	gboolean subscribed_cm;
	gboolean subscribed_rc;
	guint timeout_id_av;
	guint timeout_id_cm;
	guint timeout_id_rc;
};

typedef struct rsu_props_t_ rsu_props_t;
struct rsu_props_t_ {
	GHashTable *root_props;
	GHashTable *player_props;
	GHashTable *device_props;
	gboolean synced;
};

struct rsu_device_t_ {
	dleyna_connector_id_t connection;
	guint ids[RSU_INTERFACE_INFO_MAX];
	gchar *path;
	GPtrArray *contexts;
	gpointer current_task;
	rsu_props_t props;
	guint timeout_id;
	guint max_volume;
	GPtrArray *transport_play_speeds;
	gchar *rate;
};

gboolean rsu_device_new(dleyna_connector_id_t connection,
			GUPnPDeviceProxy *proxy,
			const gchar *ip_address,
			guint counter,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			rsu_device_t **device);

void rsu_device_delete(void *device);

void rsu_device_append_new_context(rsu_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy);
rsu_device_t *rsu_device_from_path(const gchar *path, GHashTable *device_list);
rsu_device_context_t *rsu_device_get_context(rsu_device_t *device);
void rsu_device_subscribe_to_service_changes(rsu_device_t *device);

void rsu_device_set_prop(rsu_device_t *device, rsu_task_t *task,
			 rsu_upnp_task_complete_t cb);
void rsu_device_get_prop(rsu_device_t *device, rsu_task_t *task,
			rsu_upnp_task_complete_t cb);
void rsu_device_get_all_props(rsu_device_t *device, rsu_task_t *task,
			      rsu_upnp_task_complete_t cb);
void rsu_device_play(rsu_device_t *device, rsu_task_t *task,
		     rsu_upnp_task_complete_t cb);
void rsu_device_pause(rsu_device_t *device, rsu_task_t *task,
		     rsu_upnp_task_complete_t cb);
void rsu_device_play_pause(rsu_device_t *device, rsu_task_t *task,
			   rsu_upnp_task_complete_t cb);
void rsu_device_stop(rsu_device_t *device, rsu_task_t *task,
		     rsu_upnp_task_complete_t cb);
void rsu_device_next(rsu_device_t *device, rsu_task_t *task,
		     rsu_upnp_task_complete_t cb);
void rsu_device_previous(rsu_device_t *device, rsu_task_t *task,
			 rsu_upnp_task_complete_t cb);
void rsu_device_open_uri(rsu_device_t *device, rsu_task_t *task,
			 rsu_upnp_task_complete_t cb);
void rsu_device_seek(rsu_device_t *device, rsu_task_t *task,
		     rsu_upnp_task_complete_t cb);
void rsu_device_set_position(rsu_device_t *device, rsu_task_t *task,
			     rsu_upnp_task_complete_t cb);
void rsu_device_host_uri(rsu_device_t *device, rsu_task_t *task,
			 rsu_host_service_t *host_service,
			 rsu_upnp_task_complete_t cb);
void rsu_device_remove_uri(rsu_device_t *device, rsu_task_t *task,
			   rsu_host_service_t *host_service,
			   rsu_upnp_task_complete_t cb);

#endif
