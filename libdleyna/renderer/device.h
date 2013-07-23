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

#ifndef DLR_DEVICE_H__
#define DLR_DEVICE_H__

#include <gio/gio.h>
#include <glib.h>

#include <libgupnp/gupnp-service-proxy.h>
#include <libgupnp/gupnp-device-proxy.h>

#include <libdleyna/core/connector.h>

#include "host-service.h"
#include "server.h"
#include "upnp.h"

typedef struct dlr_service_proxies_t_ dlr_service_proxies_t;
struct dlr_service_proxies_t_ {
	GUPnPServiceProxy *cm_proxy;
	GUPnPServiceProxy *av_proxy;
	GUPnPServiceProxy *rc_proxy;
};

typedef struct dlr_device_context_t_ dlr_device_context_t;
struct dlr_device_context_t_ {
	gchar *ip_address;
	GUPnPDeviceProxy *device_proxy;
	dlr_service_proxies_t service_proxies;
	dlr_device_t *device;
	gboolean subscribed_av;
	gboolean subscribed_cm;
	gboolean subscribed_rc;
	guint timeout_id_av;
	guint timeout_id_cm;
	guint timeout_id_rc;
};

typedef struct dlr_props_t_ dlr_props_t;
struct dlr_props_t_ {
	GHashTable *root_props;
	GHashTable *player_props;
	GHashTable *device_props;
	gboolean synced;
};

typedef struct dlr_device_icon_t_ dlr_device_icon_t;
struct dlr_device_icon_t_ {
	gchar *mime_type;
	guchar *bytes;
	gsize size;
};

struct dlr_device_t_ {
	dleyna_connector_id_t connection;
	guint ids[DLR_INTERFACE_INFO_MAX];
	gchar *path;
	GPtrArray *contexts;
	dlr_props_t props;
	guint timeout_id;
	guint max_volume;
	GPtrArray *transport_play_speeds;
	GPtrArray *dlna_transport_play_speeds;
	GVariant *mpris_transport_play_speeds;
	gchar *rate;
	double min_rate;
	double max_rate;
	guint construct_step;
	dlr_device_icon_t icon;
	GHashTable *rc_event_handlers;
};

void dlr_device_construct(
			dlr_device_t *dev,
			dlr_device_context_t *context,
			dleyna_connector_id_t connection,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id);

dlr_device_t *dlr_device_new(
			dleyna_connector_id_t connection,
			GUPnPDeviceProxy *proxy,
			const gchar *ip_address,
			guint counter,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id);

void dlr_device_delete(void *device);

void dlr_device_unsubscribe(void *device);

void dlr_device_append_new_context(dlr_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy);

dlr_device_t *dlr_device_from_path(const gchar *path, GHashTable *device_list);

dlr_device_context_t *dlr_device_get_context(dlr_device_t *device);

void dlr_device_subscribe_to_service_changes(dlr_device_t *device);


void dlr_device_set_prop(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

void dlr_device_get_prop(dlr_device_t *device, dlr_task_t *task,
			dlr_upnp_task_complete_t cb);

void dlr_device_get_all_props(dlr_device_t *device, dlr_task_t *task,
			      dlr_upnp_task_complete_t cb);

void dlr_device_play(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb);

void dlr_device_pause(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb);

void dlr_device_play_pause(dlr_device_t *device, dlr_task_t *task,
			   dlr_upnp_task_complete_t cb);

void dlr_device_stop(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb);

void dlr_device_next(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb);

void dlr_device_previous(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

void dlr_device_open_uri(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

void dlr_device_seek(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb);

void dlr_device_set_position(dlr_device_t *device, dlr_task_t *task,
			     dlr_upnp_task_complete_t cb);

void dlr_device_goto_track(dlr_device_t *device, dlr_task_t *task,
			   dlr_upnp_task_complete_t cb);

void dlr_device_host_uri(dlr_device_t *device, dlr_task_t *task,
			 dlr_host_service_t *host_service,
			 dlr_upnp_task_complete_t cb);

void dlr_device_remove_uri(dlr_device_t *device, dlr_task_t *task,
			   dlr_host_service_t *host_service,
			   dlr_upnp_task_complete_t cb);

void dlr_device_get_icon(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb);

#endif /* DLR_DEVICE_H__ */
