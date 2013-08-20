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

#include <string.h>

#include <libgssdp/gssdp-resource-browser.h>
#include <libgupnp/gupnp-context-manager.h>
#include <libgupnp/gupnp-error.h>

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/service-task.h>

#include "async.h"
#include "device.h"
#include "host-service.h"
#include "prop-defs.h"
#include "upnp.h"

struct dlr_upnp_t_ {
	dleyna_connector_id_t connection;
	const dleyna_connector_dispatch_cb_t *interface_info;
	dlr_upnp_callback_t found_server;
	dlr_upnp_callback_t lost_server;
	GUPnPContextManager *context_manager;
	void *user_data;
	GHashTable *server_udn_map;
	GHashTable *server_uc_map;
	guint counter;
	dlr_host_service_t *host_service;
};

/* Private structure used in service task */
typedef struct prv_device_new_ct_t_ prv_device_new_ct_t;
struct prv_device_new_ct_t_ {
	dlr_upnp_t *upnp;
	char *udn;
	gchar *ip_address;
	dlr_device_t *device;
	const dleyna_task_queue_key_t *queue_id;
};

static void prv_device_new_free(prv_device_new_ct_t *priv_t)
{
	if (priv_t) {
		g_free(priv_t->udn);
		g_free(priv_t->ip_address);
		g_free(priv_t);
	}
}

static void prv_device_chain_end(gboolean cancelled, gpointer data)
{
	dlr_device_t *device;
	prv_device_new_ct_t *priv_t = (prv_device_new_ct_t *)data;

	DLEYNA_LOG_DEBUG("Enter");

	device = priv_t->device;

	if (cancelled)
		goto on_clear;

	DLEYNA_LOG_DEBUG("Notify new server available: %s", device->path);
	g_hash_table_insert(priv_t->upnp->server_udn_map, g_strdup(priv_t->udn),
			    device);
	priv_t->upnp->found_server(device->path);

on_clear:

	g_hash_table_remove(priv_t->upnp->server_uc_map, priv_t->udn);
	prv_device_new_free(priv_t);

	if (cancelled)
		dlr_device_delete(device);

	DLEYNA_LOG_DEBUG("Exit");
	DLEYNA_LOG_DEBUG_NL();
}

static void prv_device_context_switch_end(gboolean cancelled, gpointer data)
{
	prv_device_new_ct_t *priv_t = (prv_device_new_ct_t *)data;

	DLEYNA_LOG_DEBUG("Enter");

	prv_device_new_free(priv_t);

	DLEYNA_LOG_DEBUG("Exit");
}

static const dleyna_task_queue_key_t *prv_create_device_queue(
						prv_device_new_ct_t **priv_t)
{
	const dleyna_task_queue_key_t *queue_id;

	*priv_t = g_new0(prv_device_new_ct_t, 1);

	queue_id = dleyna_task_processor_add_queue(
			dlr_renderer_service_get_task_processor(),
			dleyna_service_task_create_source(),
			DLR_RENDERER_SINK,
			DLEYNA_TASK_QUEUE_FLAG_AUTO_REMOVE,
			dleyna_service_task_process_cb,
			dleyna_service_task_cancel_cb,
			dleyna_service_task_delete_cb);
	dleyna_task_queue_set_finally(queue_id, prv_device_chain_end);
	dleyna_task_queue_set_user_data(queue_id, *priv_t);


	return queue_id;
}

static void prv_update_device_context(prv_device_new_ct_t *priv_t,
				      dlr_upnp_t *upnp, const char *udn,
				      dlr_device_t *device,
				      const gchar *ip_address,
				      const dleyna_task_queue_key_t *queue_id)
{
	priv_t->upnp = upnp;
	priv_t->udn = g_strdup(udn);
	priv_t->ip_address = g_strdup(ip_address);
	priv_t->queue_id = queue_id;
	priv_t->device = device;

	g_hash_table_insert(upnp->server_uc_map, g_strdup(udn), priv_t);
}

static void prv_server_available_cb(GUPnPControlPoint *cp,
				    GUPnPDeviceProxy *proxy,
				    gpointer user_data)
{
	dlr_upnp_t *upnp = user_data;
	const char *udn;
	dlr_device_t *device;
	const gchar *ip_address;
	dlr_device_context_t *context;
	const dleyna_task_queue_key_t *queue_id;
	unsigned int i;
	prv_device_new_ct_t *priv_t;

	DLEYNA_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *)proxy);

	if (!udn)
		goto on_error;

	ip_address = gupnp_context_get_host_ip(
		gupnp_control_point_get_context(cp));

	DLEYNA_LOG_DEBUG("UDN %s", udn);
	DLEYNA_LOG_DEBUG("IP Address %s", ip_address);

	device = g_hash_table_lookup(upnp->server_udn_map, udn);

	if (!device) {
		priv_t = g_hash_table_lookup(upnp->server_uc_map, udn);

		if (priv_t)
			device = priv_t->device;
	}

	if (!device) {
		DLEYNA_LOG_DEBUG("Device not found. Adding");

		queue_id = prv_create_device_queue(&priv_t);

		device = dlr_device_new(upnp->connection, proxy, ip_address,
					upnp->counter,
					upnp->interface_info,
					queue_id);

		prv_update_device_context(priv_t, upnp, udn, device, ip_address,
					  queue_id);

		upnp->counter++;
	} else {
		DLEYNA_LOG_DEBUG("Device Found");

		for (i = 0; i < device->contexts->len; ++i) {
			context = g_ptr_array_index(device->contexts, i);
			if (!strcmp(context->ip_address, ip_address))
				break;
		}

		if (i == device->contexts->len) {
			DLEYNA_LOG_DEBUG("Adding Context");
			dlr_device_append_new_context(device, ip_address,
						      proxy);
		}
	}

on_error:

	DLEYNA_LOG_DEBUG("Exit");
	DLEYNA_LOG_DEBUG_NL();

	return;
}

static gboolean prv_subscribe_to_service_changes(gpointer user_data)
{
	dlr_device_t *device = user_data;

	device->timeout_id = 0;
	dlr_device_subscribe_to_service_changes(device);

	return FALSE;
}

static void prv_server_unavailable_cb(GUPnPControlPoint *cp,
				      GUPnPDeviceProxy *proxy,
				      gpointer user_data)
{
	dlr_upnp_t *upnp = user_data;
	const char *udn;
	dlr_device_t *device;
	const gchar *ip_address;
	unsigned int i;
	dlr_device_context_t *context;
	gboolean subscribed;
	gboolean under_construction = FALSE;
	prv_device_new_ct_t *priv_t;
	gboolean construction_ctx = FALSE;
	const dleyna_task_queue_key_t *queue_id;

	DLEYNA_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *)proxy);

	if (!udn)
		goto on_error;

	ip_address = gupnp_context_get_host_ip(
		gupnp_control_point_get_context(cp));

	DLEYNA_LOG_DEBUG("UDN %s", udn);
	DLEYNA_LOG_DEBUG("IP Address %s", ip_address);

	device = g_hash_table_lookup(upnp->server_udn_map, udn);

	if (!device) {
		priv_t = g_hash_table_lookup(upnp->server_uc_map, udn);

		if (priv_t) {
			device = priv_t->device;
			under_construction = TRUE;
		}
	}

	if (!device) {
		DLEYNA_LOG_WARNING("Device not found. Ignoring");
		goto on_error;
	}

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (!strcmp(context->ip_address, ip_address))
			break;
	}

	if (i < device->contexts->len) {
		subscribed = (context->subscribed_av || context->subscribed_cm);

		if (under_construction)
			construction_ctx = !strcmp(context->ip_address,
						   priv_t->ip_address);

		(void) g_ptr_array_remove_index(device->contexts, i);

		if (device->contexts->len == 0) {
			if (!under_construction) {
				DLEYNA_LOG_DEBUG(
					"Last Context lost. Delete device");

				upnp->lost_server(device->path);
				g_hash_table_remove(upnp->server_udn_map, udn);
			} else {
				DLEYNA_LOG_WARNING(
				       "Device under construction. Cancelling");

				dleyna_task_processor_cancel_queue(
							priv_t->queue_id);
			}
		} else if (under_construction && construction_ctx) {
			DLEYNA_LOG_WARNING(
				"Device under construction. Switching context");

			/* Cancel previous contruction task chain */
			g_hash_table_remove(priv_t->upnp->server_uc_map,
					    priv_t->udn);
			dleyna_task_queue_set_finally(
						priv_t->queue_id,
						prv_device_context_switch_end);
			dleyna_task_processor_cancel_queue(priv_t->queue_id);

			/* Create a new construction task chain */
			context = dlr_device_get_context(device);
			queue_id = prv_create_device_queue(&priv_t);
			prv_update_device_context(priv_t, upnp, udn, device,
						  context->ip_address,
						  queue_id);

			/* Start tasks from current construction step */
			dlr_device_construct(device, context, upnp->connection,
					     upnp->interface_info, queue_id);
		} else if (subscribed && !device->timeout_id) {
			DLEYNA_LOG_DEBUG("Subscribe on new context");

			device->timeout_id = g_timeout_add_seconds(1,
					prv_subscribe_to_service_changes,
					device);
		}
	}

on_error:

	return;
}

static void prv_on_context_available(GUPnPContextManager *context_manager,
				     GUPnPContext *context,
				     gpointer user_data)
{
	dlr_upnp_t *upnp = user_data;
	GUPnPControlPoint *cp;

	cp = gupnp_control_point_new(
		context,
		"urn:schemas-upnp-org:device:MediaRenderer:1");

	g_signal_connect(cp, "device-proxy-available",
			 G_CALLBACK(prv_server_available_cb), upnp);

	g_signal_connect(cp, "device-proxy-unavailable",
			 G_CALLBACK(prv_server_unavailable_cb), upnp);

	gssdp_resource_browser_set_active(GSSDP_RESOURCE_BROWSER(cp), TRUE);
	gupnp_context_manager_manage_control_point(upnp->context_manager, cp);
	g_object_unref(cp);
}

dlr_upnp_t *dlr_upnp_new(dleyna_connector_id_t connection,
			 const dleyna_connector_dispatch_cb_t *dispatch_table,
			 dlr_upnp_callback_t found_server,
			 dlr_upnp_callback_t lost_server)
{
	dlr_upnp_t *upnp = g_new0(dlr_upnp_t, 1);

	upnp->connection = connection;
	upnp->interface_info = dispatch_table;
	upnp->found_server = found_server;
	upnp->lost_server = lost_server;

	upnp->server_udn_map = g_hash_table_new_full(g_str_hash, g_str_equal,
						     g_free,
						     dlr_device_delete);

	upnp->server_uc_map = g_hash_table_new_full(g_str_hash, g_str_equal,
						    g_free, NULL);

	upnp->context_manager = gupnp_context_manager_create(0);

	g_signal_connect(upnp->context_manager, "context-available",
			 G_CALLBACK(prv_on_context_available),
			 upnp);

	dlr_host_service_new(&upnp->host_service);

	return upnp;
}

void dlr_upnp_delete(dlr_upnp_t *upnp)
{
	if (upnp) {
		dlr_host_service_delete(upnp->host_service);
		g_object_unref(upnp->context_manager);
		g_hash_table_unref(upnp->server_udn_map);
		g_hash_table_unref(upnp->server_uc_map);

		g_free(upnp);
	}
}

GVariant *dlr_upnp_get_server_ids(dlr_upnp_t *upnp)
{
	GVariantBuilder vb;
	GHashTableIter iter;
	gpointer value;
	dlr_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	g_variant_builder_init(&vb, G_VARIANT_TYPE("ao"));
	g_hash_table_iter_init(&iter, upnp->server_udn_map);

	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		device = value;
		g_variant_builder_add(&vb, "o", device->path);
	}

	DLEYNA_LOG_DEBUG("Exit");

	return g_variant_ref_sink(g_variant_builder_end(&vb));
}

GHashTable *dlr_upnp_get_server_udn_map(dlr_upnp_t *upnp)
{
	return upnp->server_udn_map;
}


void dlr_upnp_set_prop(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_set_prop(device, task, cb);
	}
}

void dlr_upnp_get_prop(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);
	DLEYNA_LOG_DEBUG("Prop.%s", task->ut.get_prop.prop_name);

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		DLEYNA_LOG_WARNING("Cannot locate device");

		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_get_prop(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_get_all_props(dlr_upnp_t *upnp, dlr_task_t *task,
			    dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_get_all_props(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_play(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_play(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_pause(dlr_upnp_t *upnp, dlr_task_t *task,
		    dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_pause(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_play_pause(dlr_upnp_t *upnp, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_play_pause(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_stop(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_stop(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_next(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_next(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_previous(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_previous(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_open_uri(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_open_uri(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_seek(dlr_upnp_t *upnp, dlr_task_t *task,
		   dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_seek(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_set_position(dlr_upnp_t *upnp, dlr_task_t *task,
			   dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_set_position(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_goto_track(dlr_upnp_t *upnp, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_goto_track(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_host_uri(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_host_uri(device, task, upnp->host_service, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_remove_uri(dlr_upnp_t *upnp, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_remove_uri(device, task, upnp->host_service, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_get_icon(dlr_upnp_t *upnp, dlr_task_t *task,
		       dlr_upnp_task_complete_t cb)
{
	dlr_device_t *device;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	device = dlr_device_from_path(task->path, upnp->server_udn_map);

	if (!device) {
		DLEYNA_LOG_WARNING("Cannot locate device");

		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the specified object");

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else {
		dlr_device_get_icon(device, task, cb);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_lost_client(dlr_upnp_t *upnp, const gchar *client_name)
{
	dlr_host_service_lost_client(upnp->host_service, client_name);
}

void dlr_upnp_unsubscribe(dlr_upnp_t *upnp)
{
	GHashTableIter iter;
	dlr_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	g_hash_table_iter_init(&iter, upnp->server_udn_map);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&device))
		dlr_device_unsubscribe(device);

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_upnp_rescan(dlr_upnp_t *upnp)
{
	DLEYNA_LOG_DEBUG("re-scanning control points");

	gupnp_context_manager_rescan_control_points(upnp->context_manager);
}
