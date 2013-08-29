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
#include <math.h>

#include <libsoup/soup.h>
#include <libgupnp/gupnp-control-point.h>
#include <libgupnp-av/gupnp-av.h>

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/service-task.h>

#include "async.h"
#include "device.h"
#include "prop-defs.h"
#include "server.h"

typedef void (*dlr_device_local_cb_t)(dlr_async_task_t *cb_data);

typedef struct dlr_device_data_t_ dlr_device_data_t;
struct dlr_device_data_t_ {
	dlr_device_local_cb_t local_cb;
};

typedef struct dlr_rc_event_t_ dlr_rc_event_t;
struct dlr_rc_event_t_ {
	dlr_device_t *device;
	guint dev_volume;
	guint mute;
	guint source_id;
};

/* Private structure used in chain task */
typedef struct prv_new_device_ct_t_ prv_new_device_ct_t;
struct prv_new_device_ct_t_ {
	dlr_device_t *dev;
	const dleyna_connector_dispatch_cb_t *dispatch_table;
};

typedef struct prv_download_info_t_ prv_download_info_t;
struct prv_download_info_t_ {
	SoupSession *session;
	SoupMessage *msg;
	dlr_async_task_t *task;
};

static void prv_last_change_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data);

static void prv_sink_change_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data);

static void prv_rc_last_change_cb(GUPnPServiceProxy *proxy,
				  const char *variable,
				  GValue *value,
				  gpointer user_data);

static gboolean prv_props_update(dlr_device_t *device, dlr_task_t *task);

static void prv_get_rates_values(GList *allowed_tp_speeds,
				 GVariant **mpris_tp_speeds,
				 GPtrArray **upnp_tp_speeds,
				 double *min_rate, double *max_rate);

static void prv_add_player_speed_props(GHashTable *player_props,
				       double min_rate, double max_rate,
				       GVariant *mpris_transport_play_speeds,
				       GVariantBuilder *changed_props_vb);

static gint prv_compare_rationals(const gchar *a, const gchar *b);

static void prv_unref_variant(gpointer variant)
{
	GVariant *var = variant;
	if (var)
		g_variant_unref(var);
}

static void prv_props_init(dlr_props_t *props)
{
	props->root_props = g_hash_table_new_full(g_str_hash, g_str_equal,
						  NULL, prv_unref_variant);
	props->player_props = g_hash_table_new_full(g_str_hash, g_str_equal,
						    NULL, prv_unref_variant);
	props->device_props = g_hash_table_new_full(g_str_hash, g_str_equal,
						    NULL, prv_unref_variant);
	props->synced = FALSE;
}

static void prv_props_free(dlr_props_t *props)
{
	g_hash_table_unref(props->root_props);
	g_hash_table_unref(props->player_props);
	g_hash_table_unref(props->device_props);
}

static void prv_service_proxies_free(dlr_service_proxies_t *service_proxies)
{
	if (service_proxies->av_proxy)
		g_object_unref(service_proxies->av_proxy);
	if (service_proxies->rc_proxy)
		g_object_unref(service_proxies->rc_proxy);
	if (service_proxies->cm_proxy)
		g_object_unref(service_proxies->cm_proxy);
}

static void prv_context_unsubscribe(dlr_device_context_t *ctx)
{
	DLEYNA_LOG_DEBUG("Enter");

	if (ctx->timeout_id_cm) {
		(void) g_source_remove(ctx->timeout_id_cm);
		ctx->timeout_id_cm = 0;
	}
	if (ctx->timeout_id_av) {
		(void) g_source_remove(ctx->timeout_id_av);
		ctx->timeout_id_av = 0;
	}
	if (ctx->timeout_id_rc) {
		(void) g_source_remove(ctx->timeout_id_rc);
		ctx->timeout_id_rc = 0;
	}

	if (ctx->subscribed_cm) {
		(void) gupnp_service_proxy_remove_notify(
			ctx->service_proxies.cm_proxy, "SinkProtocolInfo",
			prv_sink_change_cb, ctx->device);

		gupnp_service_proxy_set_subscribed(
				ctx->service_proxies.cm_proxy, FALSE);

		ctx->subscribed_cm = FALSE;
	}
	if (ctx->subscribed_av) {
		(void) gupnp_service_proxy_remove_notify(
			ctx->service_proxies.av_proxy, "LastChange",
			prv_last_change_cb, ctx->device);

		gupnp_service_proxy_set_subscribed(
				ctx->service_proxies.av_proxy, FALSE);

		ctx->subscribed_av = FALSE;
	}
	if (ctx->subscribed_rc) {
		(void) gupnp_service_proxy_remove_notify(
			ctx->service_proxies.rc_proxy, "LastChange",
			prv_rc_last_change_cb, ctx->device);

		gupnp_service_proxy_set_subscribed(
				ctx->service_proxies.rc_proxy, FALSE);

		ctx->subscribed_rc = FALSE;
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_dlr_context_delete(gpointer context)
{
	dlr_device_context_t *ctx = context;

	if (ctx) {
		prv_context_unsubscribe(ctx);

		g_free(ctx->ip_address);
		if (ctx->device_proxy)
			g_object_unref(ctx->device_proxy);
		prv_service_proxies_free(&ctx->service_proxies);
		g_free(ctx);
	}
}

static void prv_change_props(GHashTable *props,
			     const gchar *key,
			     GVariant *value,
			     GVariantBuilder *changed_props_vb)
{
	g_hash_table_insert(props, (gpointer) key, value);
	if (changed_props_vb)
		g_variant_builder_add(changed_props_vb, "{sv}", key, value);
}

static void prv_emit_signal_properties_changed(dlr_device_t *device,
					       const char *interface,
					       GVariant *changed_props)
{
#if DLR_LOG_LEVEL & DLR_LOG_LEVEL_DEBUG
	gchar *params;
#endif
	GVariant *val = g_variant_ref_sink(g_variant_new("(s@a{sv}as)",
					   interface,
					   changed_props,
					   NULL));

	DLEYNA_LOG_DEBUG("Emitted Signal: %s.%s - ObjectPath: %s",
			 DLR_INTERFACE_PROPERTIES,
			 DLR_INTERFACE_PROPERTIES_CHANGED,
			 device->path);

#if DLR_LOG_LEVEL & DLR_LOG_LEVEL_DEBUG
	params = g_variant_print(val, FALSE);
	DLEYNA_LOG_DEBUG("Params: %s", params);
	g_free(params);
#endif

	dlr_renderer_get_connector()->notify(device->connection,
					     device->path,
					     DLR_INTERFACE_PROPERTIES,
					     DLR_INTERFACE_PROPERTIES_CHANGED,
					     val,
					     NULL);

	g_variant_unref(val);
}

static void prv_merge_meta_data(dlr_device_t *device,
				const gchar *key,
				GVariant *value,
				GVariantBuilder *changed_props_vb)
{
	GVariant *current_meta_data;
	GVariantIter viter;
	GVariantBuilder *vb;
	GVariant *val;
	gchar *vkey;
	gboolean replaced = FALSE;
	GVariant *new_val;

	vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	current_meta_data = g_hash_table_lookup(device->props.player_props,
						DLR_INTERFACE_PROP_METADATA);
	if (current_meta_data) {
		g_variant_iter_init(&viter, current_meta_data);
		while (g_variant_iter_next(&viter, "{&sv}", &vkey, &val)) {
			if (!strcmp(key, vkey)) {
				new_val = value;
				replaced = TRUE;
			} else {
				new_val = val;
			}
			g_variant_builder_add(vb, "{sv}", vkey, new_val);
			g_variant_unref(val);
		}
	}

	if (!replaced)
		g_variant_builder_add(vb, "{sv}", key, value);

	val = g_variant_ref_sink(g_variant_builder_end(vb));
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_METADATA,
			 val,
			 changed_props_vb);
	g_variant_builder_unref(vb);
}

static void prv_context_new(const gchar *ip_address,
			    GUPnPDeviceProxy *proxy,
			    dlr_device_t *device,
			    dlr_device_context_t **context)
{
	const gchar *cm_type =
		"urn:schemas-upnp-org:service:ConnectionManager";
	const gchar *av_type =
		"urn:schemas-upnp-org:service:AVTransport";
	const gchar *rc_type =
		"urn:schemas-upnp-org:service:RenderingControl";
	dlr_device_context_t *ctx = g_new(dlr_device_context_t, 1);
	dlr_service_proxies_t *service_proxies = &ctx->service_proxies;

	ctx->ip_address = g_strdup(ip_address);
	ctx->device_proxy = proxy;
	ctx->device = device;
	ctx->subscribed_av = FALSE;
	ctx->subscribed_cm = FALSE;
	ctx->subscribed_rc = FALSE;
	ctx->timeout_id_av = 0;
	ctx->timeout_id_cm = 0;
	ctx->timeout_id_rc = 0;

	g_object_ref(proxy);

	service_proxies->cm_proxy = (GUPnPServiceProxy *)
		gupnp_device_info_get_service((GUPnPDeviceInfo *)proxy,
					      cm_type);
	service_proxies->av_proxy = (GUPnPServiceProxy *)
		gupnp_device_info_get_service((GUPnPDeviceInfo *)proxy,
					      av_type);
	service_proxies->rc_proxy = (GUPnPServiceProxy *)
		gupnp_device_info_get_service((GUPnPDeviceInfo *)proxy,
					      rc_type);

	*context = ctx;
}

static dlr_device_context_t *prv_device_get_subscribed_context(
						const dlr_device_t *device)
{
	dlr_device_context_t *context;
	unsigned int i;

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (context->subscribed_av || context->subscribed_cm ||
		    context->subscribed_rc)
			goto on_found;
	}

	return NULL;

on_found:

	return context;
}

static void prv_device_append_new_context(dlr_device_t *device,
					  const gchar *ip_address,
					  GUPnPDeviceProxy *proxy)
{
	dlr_device_context_t *new_context;

	prv_context_new(ip_address, proxy, device, &new_context);
	g_ptr_array_add(device->contexts, new_context);
}

static void prv_device_subscribe_context(dlr_device_t *device)
{
	dlr_device_context_t *subscribed_context;
	dlr_device_context_t *preferred_context;

	subscribed_context = prv_device_get_subscribed_context(device);
	preferred_context = dlr_device_get_context(device);

	if (subscribed_context != preferred_context) {
		if (subscribed_context) {
			DLEYNA_LOG_DEBUG(
					"Subscription switch from <%s> to <%s>",
					subscribed_context->ip_address,
					preferred_context->ip_address);
			prv_context_unsubscribe(subscribed_context);
		}
		dlr_device_subscribe_to_service_changes(device);
	}
}

void dlr_device_append_new_context(dlr_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy)
{
	prv_device_append_new_context(device, ip_address, proxy);
	prv_device_subscribe_context(device);
}

void dlr_device_delete(void *device)
{
	unsigned int i;
	dlr_device_t *dev = device;

	if (dev) {
		if (dev->timeout_id)
			(void) g_source_remove(dev->timeout_id);

		for (i = 0; i < DLR_INTERFACE_INFO_MAX && dev->ids[i]; ++i)
			(void) dlr_renderer_get_connector()->unpublish_object(
								dev->connection,
								dev->ids[i]);
		g_ptr_array_unref(dev->contexts);
		g_free(dev->path);
		prv_props_free(&dev->props);

		if (dev->transport_play_speeds != NULL)
			g_ptr_array_free(dev->transport_play_speeds, TRUE);
		if (dev->dlna_transport_play_speeds != NULL)
			g_ptr_array_free(dev->dlna_transport_play_speeds, TRUE);
		if (dev->mpris_transport_play_speeds)
			g_variant_unref(dev->mpris_transport_play_speeds);
		g_hash_table_unref(dev->rc_event_handlers);
		g_free(dev->rate);

		g_free(dev->icon.mime_type);
		g_free(dev->icon.bytes);

		g_free(dev);
	}
}

void dlr_device_unsubscribe(void *device)
{
	unsigned int i;
	dlr_device_t *dev = device;
	dlr_device_context_t *context;

	if (dev) {
		for (i = 0; i < dev->contexts->len; ++i) {
			context = g_ptr_array_index(dev->contexts, i);
			prv_context_unsubscribe(context);
		}
	}
}

static gboolean prv_re_enable_cm_subscription(gpointer user_data)
{
	dlr_device_context_t *context = user_data;

	context->timeout_id_cm = 0;

	return FALSE;
}

static void prv_cm_subscription_lost_cb(GUPnPServiceProxy *proxy,
					const GError *reason,
					gpointer user_data)
{
	dlr_device_context_t *context = user_data;
	dlr_service_proxies_t *service_proxies = &context->service_proxies;

	if (!context->timeout_id_cm) {
		gupnp_service_proxy_set_subscribed(service_proxies->cm_proxy,
						   TRUE);
		context->timeout_id_cm = g_timeout_add_seconds(10,
						prv_re_enable_cm_subscription,
						context);
	} else {
		g_source_remove(context->timeout_id_cm);
		(void) gupnp_service_proxy_remove_notify(
				service_proxies->cm_proxy, "SinkProtocolInfo",
				prv_sink_change_cb, context->device);

		context->timeout_id_cm = 0;
		context->subscribed_cm = FALSE;
	}
}

static gboolean prv_re_enable_av_subscription(gpointer user_data)
{
	dlr_device_context_t *context = user_data;

	context->timeout_id_av = 0;

	return FALSE;
}

static void prv_av_subscription_lost_cb(GUPnPServiceProxy *proxy,
					const GError *reason,
					gpointer user_data)
{
	dlr_device_context_t *context = user_data;
	dlr_service_proxies_t *service_proxies = &context->service_proxies;

	if (!context->timeout_id_av) {
		gupnp_service_proxy_set_subscribed(service_proxies->av_proxy,
						   TRUE);
		context->timeout_id_av = g_timeout_add_seconds(10,
						prv_re_enable_av_subscription,
						context);
	} else {
		g_source_remove(context->timeout_id_av);
		(void) gupnp_service_proxy_remove_notify(
				service_proxies->av_proxy, "LastChange",
				prv_last_change_cb, context->device);

		context->timeout_id_av = 0;
		context->subscribed_av = FALSE;
	}
}

static gboolean prv_re_enable_rc_subscription(gpointer user_data)
{
	dlr_device_context_t *context = user_data;

	context->timeout_id_rc = 0;

	return FALSE;
}

static void prv_rc_subscription_lost_cb(GUPnPServiceProxy *proxy,
					const GError *reason,
					gpointer user_data)
{
	dlr_device_context_t *context = user_data;
	dlr_service_proxies_t *service_proxies = &context->service_proxies;

	if (!context->timeout_id_rc) {
		gupnp_service_proxy_set_subscribed(service_proxies->rc_proxy,
						   TRUE);
		context->timeout_id_rc = g_timeout_add_seconds(10,
						prv_re_enable_rc_subscription,
						context);
	} else {
		g_source_remove(context->timeout_id_rc);
		(void) gupnp_service_proxy_remove_notify(
				service_proxies->rc_proxy, "LastChange",
				prv_rc_last_change_cb, context->device);

		context->timeout_id_rc = 0;
		context->subscribed_rc = FALSE;
	}
}

void dlr_device_subscribe_to_service_changes(dlr_device_t *device)
{
	dlr_device_context_t *context;
	dlr_service_proxies_t *service_proxies;

	context = dlr_device_get_context(device);
	service_proxies = &context->service_proxies;

	DLEYNA_LOG_DEBUG("Subscribing through context <%s>",
			 context->ip_address);

	if (service_proxies->cm_proxy) {
		gupnp_service_proxy_set_subscribed(service_proxies->cm_proxy,
						   TRUE);
		(void) gupnp_service_proxy_add_notify(service_proxies->cm_proxy,
						      "SinkProtocolInfo",
						      G_TYPE_STRING,
						      prv_sink_change_cb,
						      device);
		context->subscribed_cm = TRUE;

		g_signal_connect(service_proxies->cm_proxy,
				 "subscription-lost",
				 G_CALLBACK(prv_cm_subscription_lost_cb),
				 context);
	}

	if (service_proxies->av_proxy) {
		gupnp_service_proxy_set_subscribed(service_proxies->av_proxy,
						   TRUE);
		(void) gupnp_service_proxy_add_notify(service_proxies->av_proxy,
						      "LastChange",
						      G_TYPE_STRING,
						      prv_last_change_cb,
						      device);
		context->subscribed_av = TRUE;

		g_signal_connect(service_proxies->av_proxy,
				 "subscription-lost",
				 G_CALLBACK(prv_av_subscription_lost_cb),
				 context);
	}

	if (service_proxies->rc_proxy) {
		gupnp_service_proxy_set_subscribed(service_proxies->rc_proxy,
						   TRUE);
		(void) gupnp_service_proxy_add_notify(service_proxies->rc_proxy,
						      "LastChange",
						      G_TYPE_STRING,
						      prv_rc_last_change_cb,
						      device);
		context->subscribed_rc = TRUE;

		g_signal_connect(service_proxies->av_proxy,
				 "subscription-lost",
				 G_CALLBACK(prv_rc_subscription_lost_cb),
				 context);
	}
}

static void prv_as_prop_from_hash_table(const gchar *prop_name,
					GHashTable *values, GHashTable *props)
{
	GVariantBuilder vb;
	GHashTableIter iter;
	gpointer key;
	GVariant *val;

	g_variant_builder_init(&vb, G_VARIANT_TYPE("as"));
	g_hash_table_iter_init(&iter, values);

	while (g_hash_table_iter_next(&iter, &key, NULL))
		g_variant_builder_add(&vb, "s", key);

	val = g_variant_ref_sink(g_variant_builder_end(&vb));
	g_hash_table_insert(props, (gchar *)prop_name, val);
}

static void prv_process_protocol_info(dlr_device_t *device,
				      const gchar *protocol_info)
{
	GVariant *val;
	gchar **entries;
	gchar **type_info;
	unsigned int i;
	GHashTable *protocols;
	GHashTable *types;
	const char http_prefix[] = "http-";

	DLEYNA_LOG_DEBUG("Enter");
	DLEYNA_LOG_DEBUG("prv_process_protocol_info: %s", protocol_info);

	protocols = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					  NULL);
	types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	val = g_variant_ref_sink(g_variant_new_string(protocol_info));
	g_hash_table_insert(device->props.device_props,
			    DLR_INTERFACE_PROP_PROTOCOL_INFO,
			    val);

	entries = g_strsplit(protocol_info, ",", 0);

	for (i = 0; entries[i]; ++i) {
		type_info = g_strsplit(entries[i], ":", 0);

		if (type_info[0] && type_info[1] && type_info[2]) {
			if (!g_ascii_strncasecmp(http_prefix, type_info[0],
						 sizeof(http_prefix) - 1)) {
				type_info[0][sizeof(http_prefix) - 2] = 0;
			}

			g_hash_table_insert(protocols,
					    g_ascii_strdown(type_info[0], -1),
					    NULL);
			g_hash_table_insert(types,
					    g_ascii_strdown(type_info[2], -1),
					    NULL);
		}

		g_strfreev(type_info);
	}

	g_strfreev(entries);

	prv_as_prop_from_hash_table(DLR_INTERFACE_PROP_SUPPORTED_URIS,
				    protocols,
				    device->props.root_props);

	prv_as_prop_from_hash_table(DLR_INTERFACE_PROP_SUPPORTED_MIME,
				    types,
				    device->props.root_props);

	g_hash_table_unref(types);
	g_hash_table_unref(protocols);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_get_protocol_info_cb(GUPnPServiceProxy *proxy,
				     GUPnPServiceProxyAction *action,
				     gpointer user_data)
{
	gchar *result = NULL;
	gboolean end;
	GError *error = NULL;
	prv_new_device_ct_t *priv_t = (prv_new_device_ct_t *)user_data;

	DLEYNA_LOG_DEBUG("Enter");

	priv_t->dev->construct_step++;

	end = gupnp_service_proxy_end_action(proxy, action, &error, "Sink",
					     G_TYPE_STRING, &result, NULL);
	if (!end || (result == NULL)) {
		DLEYNA_LOG_WARNING("GetProtocolInfo operation failed: %s",
				   ((error != NULL) ? error->message
						    : "Invalid result"));
		goto on_error;
	}

	prv_process_protocol_info(priv_t->dev, result);

on_error:

	if (error)
		g_error_free(error);

	g_free(result);

	DLEYNA_LOG_DEBUG("Exit");
}

static GUPnPServiceProxyAction *prv_get_protocol_info(
						dleyna_service_task_t *task,
						GUPnPServiceProxy *proxy,
						gboolean *failed)
{
	*failed = FALSE;

	return gupnp_service_proxy_begin_action(
					proxy, "GetProtocolInfo",
					dleyna_service_task_begin_action_cb,
					task, NULL);
}

static GUPnPServiceProxyAction *prv_subscribe(dleyna_service_task_t *task,
					      GUPnPServiceProxy *proxy,
					      gboolean *failed)
{
	dlr_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = (dlr_device_t *)dleyna_service_task_get_user_data(task);

	device->construct_step++;
	prv_device_subscribe_context(device);

	*failed = FALSE;

	DLEYNA_LOG_DEBUG("Exit");

	return NULL;
}

static GUPnPServiceProxyAction *prv_declare(dleyna_service_task_t *task,
					    GUPnPServiceProxy *proxy,
					    gboolean *failed)
{
	unsigned int i;
	dlr_device_t *device;
	prv_new_device_ct_t *priv_t;
	const dleyna_connector_dispatch_cb_t *table;

	DLEYNA_LOG_DEBUG("Enter");

	*failed = FALSE;

	priv_t = (prv_new_device_ct_t *)dleyna_service_task_get_user_data(task);
	device = priv_t->dev;
	device->construct_step++;

	table = priv_t->dispatch_table;

	for (i = 0; i < DLR_INTERFACE_INFO_MAX; ++i) {
		device->ids[i] = dlr_renderer_get_connector()->publish_object(
				device->connection,
				device->path,
				FALSE,
				i,
				table + i);

		if (!device->ids[i]) {
			*failed = TRUE;
			goto on_error;
		}
	}

on_error:

DLEYNA_LOG_DEBUG("Exit");

	return NULL;
}

static void prv_free_rc_event(gpointer user_data)
{
	dlr_rc_event_t *event = user_data;

	if (event->source_id)
		g_source_remove(event->source_id);
	g_free(event);
}

void dlr_device_construct(
			dlr_device_t *dev,
			dlr_device_context_t *context,
			dleyna_connector_id_t connection,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id)
{
	prv_new_device_ct_t *priv_t;
	GUPnPServiceProxy *s_proxy;

	DLEYNA_LOG_DEBUG("Current step: %d", dev->construct_step);

	priv_t = g_new0(prv_new_device_ct_t, 1);

	priv_t->dev = dev;
	priv_t->dispatch_table = dispatch_table;

	s_proxy = context->service_proxies.cm_proxy;

	if (dev->construct_step < 1)
		dleyna_service_task_add(queue_id, prv_get_protocol_info,
					s_proxy, prv_get_protocol_info_cb,
					NULL, priv_t);

	/* The following task should always be completed */
	dleyna_service_task_add(queue_id, prv_subscribe, s_proxy,
				NULL, NULL, dev);

	if (dev->construct_step < 3)
		dleyna_service_task_add(queue_id, prv_declare, s_proxy,
					NULL, g_free, priv_t);

	dleyna_task_queue_start(queue_id);

	DLEYNA_LOG_DEBUG("Exit");
}

dlr_device_t *dlr_device_new(
			dleyna_connector_id_t connection,
			GUPnPDeviceProxy *proxy,
			const gchar *ip_address,
			guint counter,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id)
{
	dlr_device_t *dev;
	gchar *new_path;
	dlr_device_context_t *context;

	DLEYNA_LOG_DEBUG("New Device on %s", ip_address);

	new_path = g_strdup_printf("%s/%u", DLEYNA_SERVER_PATH, counter);
	DLEYNA_LOG_DEBUG("Server Path %s", new_path);

	dev = g_new0(dlr_device_t, 1);

	dev->connection = connection;
	dev->contexts = g_ptr_array_new_with_free_func(prv_dlr_context_delete);
	dev->path = new_path;
	dev->rate = g_strdup("1");
	dev->rc_event_handlers = g_hash_table_new_full(g_int_hash, g_int_equal,
						       g_free,
						       prv_free_rc_event);

	prv_props_init(&dev->props);

	prv_device_append_new_context(dev, ip_address, proxy);

	context = dlr_device_get_context(dev);

	dlr_device_construct(dev, context, connection,
			     dispatch_table, queue_id);

	DLEYNA_LOG_DEBUG("Exit");

	return dev;
}

dlr_device_t *dlr_device_from_path(const gchar *path, GHashTable *device_list)
{
	GHashTableIter iter;
	gpointer value;
	dlr_device_t *device;
	dlr_device_t *retval = NULL;

	g_hash_table_iter_init(&iter, device_list);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		device = value;
		if (!strcmp(device->path, path)) {
			retval = device;
			break;
		}
	}

	return retval;
}

dlr_device_context_t *dlr_device_get_context(dlr_device_t *device)
{
	dlr_device_context_t *context;
	unsigned int i;
	const char ip4_local_prefix[] = "127.0.0.";

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (!strncmp(context->ip_address, ip4_local_prefix,
			     sizeof(ip4_local_prefix) - 1) ||
		    !strcmp(context->ip_address, "::1") ||
		    !strcmp(context->ip_address, "0:0:0:0:0:0:0:1"))
			break;
	}

	if (i == device->contexts->len)
		context = g_ptr_array_index(device->contexts, 0);

	return context;
}

static void prv_get_prop(dlr_async_task_t *cb_data)
{
	dlr_task_get_prop_t *get_prop = &cb_data->task.ut.get_prop;
	GVariant *res = NULL;

	DLEYNA_LOG_DEBUG("Enter");

	if (!strcmp(get_prop->interface_name,
		    DLEYNA_SERVER_INTERFACE_RENDERER_DEVICE)) {
		res = g_hash_table_lookup(cb_data->device->props.device_props,
					  get_prop->prop_name);
	} else if (!strcmp(get_prop->interface_name, DLR_INTERFACE_SERVER)) {
		res = g_hash_table_lookup(cb_data->device->props.root_props,
					  get_prop->prop_name);
	} else if (!strcmp(get_prop->interface_name, DLR_INTERFACE_PLAYER)) {
		res = g_hash_table_lookup(cb_data->device->props.player_props,
					  get_prop->prop_name);
	} else if (!strcmp(get_prop->interface_name, "")) {
		res = g_hash_table_lookup(cb_data->device->props.root_props,
					  get_prop->prop_name);
		if (!res)
			res = g_hash_table_lookup(
				cb_data->device->props.player_props,
				get_prop->prop_name);

		if (!res)
			res = g_hash_table_lookup(
				cb_data->device->props.device_props,
				get_prop->prop_name);
	} else {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Unknown Interface");
	}

	if (!res) {
		if (!cb_data->error)
			cb_data->error =
				g_error_new(DLEYNA_SERVER_ERROR,
					    DLEYNA_ERROR_UNKNOWN_PROPERTY,
					    "Property not defined for object");
	} else {
		cb_data->task.result = g_variant_ref(res);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_add_props(GHashTable *props, GVariantBuilder *vb)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_hash_table_iter_init(&iter, props);

	while (g_hash_table_iter_next(&iter, &key, &value))
		g_variant_builder_add(vb, "{sv}", (gchar *)key,
				      (GVariant *)value);
}

static void prv_get_props(dlr_async_task_t *cb_data)
{
	dlr_task_get_props_t *get_props = &cb_data->task.ut.get_props;
	GVariantBuilder *vb;

	DLEYNA_LOG_DEBUG("Enter");

	vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (!strcmp(get_props->interface_name,
		    DLEYNA_SERVER_INTERFACE_RENDERER_DEVICE)) {
		prv_add_props(cb_data->device->props.device_props, vb);
	} else if (!strcmp(get_props->interface_name, DLR_INTERFACE_SERVER)) {
		prv_add_props(cb_data->device->props.root_props, vb);
		prv_add_props(cb_data->device->props.device_props, vb);
	} else if (!strcmp(get_props->interface_name, DLR_INTERFACE_PLAYER)) {
		prv_add_props(cb_data->device->props.player_props, vb);
	} else if (!strcmp(get_props->interface_name, "")) {
		prv_add_props(cb_data->device->props.root_props, vb);
		prv_add_props(cb_data->device->props.player_props, vb);
		prv_add_props(cb_data->device->props.device_props, vb);
	} else {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Unknown Interface");
		goto on_error;
	}

	cb_data->task.result = g_variant_ref_sink(g_variant_builder_end(vb));

on_error:

	g_variant_builder_unref(vb);

	DLEYNA_LOG_DEBUG("Exit");
}

static const gchar *prv_map_transport_state(const gchar *upnp_state)
{
	const gchar *retval;

	if (!strcmp(upnp_state, "PLAYING"))
		retval = "Playing";
	else if (!strcmp(upnp_state, "PAUSED_PLAYBACK") ||
		 !strcmp(upnp_state, "PAUSED_RECORDING"))
		retval = "Paused";
	else
		retval = "Stopped";

	return retval;
}

static gdouble prv_map_transport_speed(const gchar *upnp_speed)
{
	gdouble retval = 1;
	gchar **parts = NULL;
	gint num;
	gint dom;

	if (upnp_speed[0]) {
		parts = g_strsplit(upnp_speed, "/", 0);
		if (!parts[0])
			goto on_error;

		g_strstrip(parts[0]);
		num = atoi(parts[0]);

		if (parts[1]) {
			if (parts[2])
				goto on_error;

			g_strstrip(parts[1]);
			dom = atoi(parts[1]);
			if (dom == 0)
				goto on_error;

			retval = num / (gdouble) dom;
		} else {
			retval = num;
		}
	}

on_error:

	if (parts)
		g_strfreev(parts);

	return retval;
}

static gint compare_speeds(gconstpointer a, gconstpointer b)
{
	return prv_compare_rationals((const gchar *)a, (const gchar *)b);
}

static void prv_add_dlna_speeds(dlr_device_t *device,
				gchar **dlna_speeds,
				GVariantBuilder *changed_props_vb)
{
	GList *allowed_tp_speeds = NULL;
	double min_rate = 0;
	double max_rate = 0;
	GVariant *mpris_tp_speeds = NULL;
	unsigned int i = 0;
	gchar *speed;

	if (dlna_speeds == NULL)
		goto exit;

	allowed_tp_speeds = g_list_prepend(allowed_tp_speeds, g_strdup("1"));

	while (dlna_speeds[i]) {
		speed = g_strstrip(g_strdup(dlna_speeds[i]));
		allowed_tp_speeds = g_list_prepend(allowed_tp_speeds, speed);
		++i;
	}

	allowed_tp_speeds = g_list_sort(allowed_tp_speeds, compare_speeds);

	prv_get_rates_values(allowed_tp_speeds,
			     &mpris_tp_speeds,
			     &device->dlna_transport_play_speeds,
			     &min_rate, &max_rate);

	prv_add_player_speed_props(device->props.player_props,
				   min_rate, max_rate,
				   mpris_tp_speeds,
				   changed_props_vb);

exit:
	if (allowed_tp_speeds != NULL)
		g_list_free_full(allowed_tp_speeds, g_free);

	return;
}

static void prv_add_actions(dlr_device_t *device,
			    const gchar *actions,
			    GVariantBuilder *changed_props_vb)
{
	gchar **parts;
	unsigned int i = 0;
	GVariant *true_val;
	GVariant *false_val;
	gboolean play = FALSE;
	gboolean ppause = FALSE;
	gboolean seek = FALSE;
	gboolean next = FALSE;
	gboolean previous = FALSE;
	GVariant *val;
	GRegex *regex;
	gchar *tmp_str;
	gchar **speeds;

	regex = g_regex_new("\\\\,", 0, 0, NULL);
	tmp_str = g_regex_replace_literal(regex, actions, -1, 0, "*", 0, NULL);
	parts = g_strsplit(tmp_str, ",", 0);
	g_free(tmp_str);
	g_regex_unref(regex);

	true_val = g_variant_ref_sink(g_variant_new_boolean(TRUE));
	false_val = g_variant_ref_sink(g_variant_new_boolean(FALSE));

	while (parts[i]) {
		g_strstrip(parts[i]);

		if (!strcmp(parts[i], "Play")) {
			play = TRUE;
		} else if (!strcmp(parts[i], "Pause")) {
			ppause = TRUE;
		} else if (!strcmp(parts[i], "Seek")) {
			seek = TRUE;
		} else if (!strcmp(parts[i], "Next")) {
			next = TRUE;
		} else if (!strcmp(parts[i], "Previous")) {
			previous = TRUE;
		} else if (!strncmp(parts[i], "X_DLNA_PS=",
			 strlen("X_DLNA_PS="))) {
			speeds = g_strsplit(parts[i] + strlen("X_DLNA_PS="),
					    "*", 0);
			prv_add_dlna_speeds(device, speeds, changed_props_vb);
			g_strfreev(speeds);
		}
		++i;
	}

	g_variant_ref(false_val);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_CONTROL, false_val,
			 changed_props_vb);

	val = play ? true_val : false_val;
	g_variant_ref(val);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_PLAY, val,
			 changed_props_vb);

	val = ppause ? true_val : false_val;
	g_variant_ref(val);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_PAUSE, val,
			 changed_props_vb);

	val = seek ? true_val : false_val;
	g_variant_ref(val);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_SEEK, val,
			 changed_props_vb);

	val = next ? true_val : false_val;
	g_variant_ref(val);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_NEXT, val,
			 changed_props_vb);

	val = previous ? true_val : false_val;
	g_variant_ref(val);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_PREVIOUS, val,
			 changed_props_vb);

	g_variant_unref(true_val);
	g_variant_unref(false_val);
	g_strfreev(parts);
}

static void prv_add_all_actions(dlr_device_t *device,
				GVariantBuilder *changed_props_vb)
{
	GVariant *val;

	val = g_variant_ref_sink(g_variant_new_boolean(TRUE));
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_PLAY, val,
			 changed_props_vb);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_PAUSE, g_variant_ref(val),
			 changed_props_vb);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_SEEK, g_variant_ref(val),
			 changed_props_vb);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_NEXT, g_variant_ref(val),
			 changed_props_vb);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_PREVIOUS, g_variant_ref(val),
			 changed_props_vb);
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_CAN_CONTROL, g_variant_ref(val),
			 changed_props_vb);
}

static gint64 prv_duration_to_int64(const gchar *duration)
{
	gchar **parts;
	unsigned int i = 0;
	unsigned int count;
	gint64 pos = 0;

	parts = g_strsplit(duration, ":", 0);
	for (count = 0; parts[count]; ++count)
		;

	if (count != 3)
		goto on_error;

	/* TODO: This does not handle fractional seconds */

	i = 1;
	do {
		--count;
		g_strstrip(parts[count]);
		pos += atoi(parts[count]) * i;
		i *= 60;
	} while (count > 0);

	pos *= 1000000;

on_error:

	g_strfreev(parts);

	return pos;
}

static gchar *prv_int64_to_duration(gint64 micro_seconds)
{
	GString *retval;
	unsigned int seconds;

	if (micro_seconds < 0) {
		retval = g_string_new("-");
		micro_seconds = -micro_seconds;
	} else {
		retval = g_string_new("");
	}

	/* TODO: This does not handle fractional seconds */

	seconds = micro_seconds / 1000000;
	g_string_append_printf(retval, "%02u:%02u:%02u",
			       seconds / 3600,
			       (seconds / 60) % 60,
			       seconds % 60);

	return g_string_free(retval, FALSE);
}

static void prv_add_reltime(dlr_device_t *device,
			    const gchar *reltime,
			    GVariantBuilder *changed_props_vb)
{
	GVariant *val;
	gint64 pos = prv_duration_to_int64(reltime);

	val = g_variant_ref_sink(g_variant_new_int64(pos));
	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_POSITION, val,
			 changed_props_vb);
}

static void prv_found_item(GUPnPDIDLLiteParser *parser,
			   GUPnPDIDLLiteObject *object,
			   gpointer user_data)
{
	GVariantBuilder *vb = user_data;
	gchar *track_id;
	int track_number = gupnp_didl_lite_object_get_track_number(object);
	GVariant *value;
	const gchar *str_value;
	GVariantBuilder *artists_vb;
	GVariantBuilder *album_artists_vb;
	GList *artists;
	GList *head;
	const gchar *artist_name;
	const gchar *artist_role;

	track_id = g_strdup_printf(DLEYNA_SERVER_OBJECT"/track/%u",
				   track_number != -1 ? track_number : 0);

	value = g_variant_new_string(track_id);
	g_variant_builder_add(vb, "{sv}", "mpris:trackid", value);
	g_free(track_id);

	if (track_number != -1) {
		value = g_variant_new_int32(track_number);
		g_variant_builder_add(vb, "{sv}", "mpris:trackNumber", value);
	}

	str_value = gupnp_didl_lite_object_get_title(object);
	if (str_value) {
		value = g_variant_new_string(str_value);
		g_variant_builder_add(vb, "{sv}", "xesam:title", value);
	}

	str_value = gupnp_didl_lite_object_get_album_art(object);
	if (str_value) {
		value = g_variant_new_string(str_value);
		g_variant_builder_add(vb, "{sv}", "mpris:artUrl", value);
	}

	str_value = gupnp_didl_lite_object_get_album(object);
	if (str_value) {
		value = g_variant_new_string(str_value);
		g_variant_builder_add(vb, "{sv}", "xesam:album", value);
	}

	str_value = gupnp_didl_lite_object_get_genre(object);
	if (str_value) {
		value = g_variant_new_string(str_value);
		g_variant_builder_add(vb, "{sv}", "xesam:genre", value);
	}

	artists = gupnp_didl_lite_object_get_artists(object);
	head = artists;

	if (artists) {
		artists_vb = g_variant_builder_new(G_VARIANT_TYPE("as"));
		album_artists_vb = g_variant_builder_new(G_VARIANT_TYPE("as"));
		do {
			artist_name =
				gupnp_didl_lite_contributor_get_name(
					artists->data);
			artist_role = gupnp_didl_lite_contributor_get_role(
				artists->data);
			if (!artist_role)
				g_variant_builder_add(artists_vb, "s",
						      artist_name);
			else if (!strcmp(artist_role, "AlbumArtist"))
				g_variant_builder_add(album_artists_vb, "s",
						      artist_name);
			g_object_unref(artists->data);
			artists = g_list_next(artists);
		} while (artists);
		g_list_free(head);
		value = g_variant_builder_end(artists_vb);
		g_variant_builder_add(vb, "{sv}", "xesam:artist", value);
		value = g_variant_builder_end(album_artists_vb);
		g_variant_builder_add(vb, "{sv}", "xesam:albumArtist", value);
		g_variant_builder_unref(artists_vb);
		g_variant_builder_unref(album_artists_vb);
	}
}

static void prv_add_track_meta_data(dlr_device_t *device,
				    const gchar *metadata,
				    const gchar *duration,
				    const gchar *uri,
				    GVariantBuilder *changed_props_vb)
{
	gchar *didl = g_strdup_printf("<DIDL-Lite>%s</DIDL-Lite>", metadata);
	GUPnPDIDLLiteParser *parser = NULL;
	GVariantBuilder *vb;
	GError *upnp_error = NULL;
	GVariant *val;
	gint error_code;

	parser = gupnp_didl_lite_parser_new();

	vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (duration) {
		val = g_variant_new_int64(prv_duration_to_int64(duration));
		g_variant_builder_add(vb, "{sv}", "mpris:length", val);
	}

	if (uri) {
		val = g_variant_new_string(uri);
		g_variant_builder_add(vb, "{sv}", "xesam:url", val);
	}

	g_signal_connect(parser, "object-available" ,
			 G_CALLBACK(prv_found_item), vb);

	if (!gupnp_didl_lite_parser_parse_didl(parser, didl, &upnp_error)) {
		error_code = upnp_error->code;
		g_error_free(upnp_error);
		if (error_code != GUPNP_XML_ERROR_EMPTY_NODE)
			goto on_error;
	}

	prv_change_props(device->props.player_props,
			 DLR_INTERFACE_PROP_METADATA,
			 g_variant_ref_sink(g_variant_builder_end(vb)),
			 changed_props_vb);

on_error:

	if (parser)
		g_object_unref(parser);

	g_variant_builder_unref(vb);
	g_free(didl);
}

static void prv_last_change_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data)
{
	GUPnPLastChangeParser *parser;
	dlr_device_t *device = user_data;
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	gchar *meta_data = NULL;
	gchar *actions = NULL;
	gchar *play_speed = NULL;
	gchar *state = NULL;
	gchar *duration = NULL;
	gchar *uri = NULL;
	guint tracks_number = G_MAXUINT;
	guint current_track = G_MAXUINT;
	GVariant *val;

	parser = gupnp_last_change_parser_new();

	if (!gupnp_last_change_parser_parse_last_change(
			parser, 0,
			g_value_get_string(value),
			NULL,
			"CurrentTrackMetaData", G_TYPE_STRING, &meta_data,
			"CurrentTransportActions", G_TYPE_STRING, &actions,
			"TransportPlaySpeed", G_TYPE_STRING, &play_speed,
			"TransportState", G_TYPE_STRING, &state,
			"CurrentTrackDuration", G_TYPE_STRING, &duration,
			"CurrentTrackURI", G_TYPE_STRING, &uri,
			"NumberOfTracks", G_TYPE_UINT, &tracks_number,
			"CurrentTrack", G_TYPE_UINT, &current_track,
			NULL))
		goto on_error;

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (meta_data) {
		prv_add_track_meta_data(device,
					meta_data,
					duration,
					uri,
					changed_props_vb);
		g_free(meta_data);
	} else {
		if (duration) {
			val = g_variant_new_int64(prv_duration_to_int64(
							  duration));
			val = g_variant_ref_sink(val);
			prv_merge_meta_data(device,
					    "mpris:length",
					    val,
					    changed_props_vb);
			g_variant_unref(val);
		}

		if (uri) {
			val = g_variant_ref_sink(g_variant_new_string(uri));
			prv_merge_meta_data(device,
					    "xesam:url",
					    val,
					    changed_props_vb);
			g_variant_unref(val);
		}
	}

	g_free(duration);
	g_free(uri);

	if (actions) {
		prv_add_actions(device, actions, changed_props_vb);
		g_free(actions);
	}

	if (play_speed) {
		val = g_variant_ref_sink(
			g_variant_new_double(
				prv_map_transport_speed(play_speed)));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_RATE, val,
				 changed_props_vb);

		g_free(device->rate);
		device->rate = play_speed;
	}

	if (state) {
		val = g_variant_ref_sink(
			g_variant_new_string(
				prv_map_transport_state(state)));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_PLAYBACK_STATUS, val,
				 changed_props_vb);
		g_free(state);
	}

	if (tracks_number != G_MAXUINT) {
		val = g_variant_ref_sink(g_variant_new_uint32(tracks_number));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_NUMBER_OF_TRACKS, val,
				 changed_props_vb);
	}

	if (current_track != G_MAXUINT) {
		val = g_variant_ref_sink(g_variant_new_uint32(current_track));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_CURRENT_TRACK, val,
				 changed_props_vb);
	}

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));
	prv_emit_signal_properties_changed(device,
					   DLR_INTERFACE_PLAYER,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);

on_error:

	g_object_unref(parser);
}

static gboolean prv_process_rc_last_change(gpointer user_data)
{
	dlr_rc_event_t *event = user_data;
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	GVariant *val;
	double mpris_volume;
	dlr_device_t *device = event->device;
	gint source_id;

	if (!device->props.synced && !prv_props_update(device, NULL))
		goto on_lost_device;

	if (device->max_volume == 0)
		goto on_error;

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (event->dev_volume != G_MAXUINT) {
		mpris_volume = (double) event->dev_volume /
			(double) device->max_volume;
		val = g_variant_ref_sink(g_variant_new_double(mpris_volume));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_VOLUME, val,
				 changed_props_vb);
	}

	if (event->mute != G_MAXUINT) {
		val = g_variant_ref_sink(
				g_variant_new_boolean(event->mute ? TRUE
						      : FALSE));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_MUTE, val,
				 changed_props_vb);
	}

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));
	prv_emit_signal_properties_changed(device,
					   DLR_INTERFACE_PLAYER,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);

on_error:
	source_id = (gint) event->source_id;
	event->source_id = 0;
	g_hash_table_remove(device->rc_event_handlers, &source_id);

on_lost_device:

	return FALSE;
}

static void prv_rc_last_change_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data)
{
	GUPnPLastChangeParser *parser;
	dlr_rc_event_t *event;
	guint dev_volume = G_MAXUINT;
	guint mute = G_MAXUINT;
	gint *key;

	parser = gupnp_last_change_parser_new();

	if (!gupnp_last_change_parser_parse_last_change(
		    parser, 0,
		    g_value_get_string(value),
		    NULL,
		    "Volume", G_TYPE_UINT, &dev_volume,
		    "Mute", G_TYPE_UINT, &mute,
		    NULL))
		goto on_error;

	event = g_new0(dlr_rc_event_t, 1);
	event->dev_volume = dev_volume;
	event->mute = mute;
	event->device = user_data;

	/* We cannot execute the code in prv_process_rc_last_change directly
	   in this function as it can cause the main loop to be iterated, which
	   may cause a crash when we return back to GUPnP.  This code will be
	   re-written once we can cancel calls to retrieve the service
	   introspection data. */

	event->source_id = g_idle_add(prv_process_rc_last_change, event);
	key = g_new(gint, 1);
	*key = (gint) event->source_id;
	g_hash_table_insert(event->device->rc_event_handlers, key, event);

on_error:

	g_object_unref(parser);
}

static void prv_sink_change_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data)
{
	dlr_device_t *device = user_data;
	const gchar *sink;

	sink = g_value_get_string(value);

	if (sink)
		prv_process_protocol_info(device, sink);
}

static void prv_get_position_info_cb(GUPnPServiceProxy *proxy,
				     GUPnPServiceProxyAction *action,
				     gpointer user_data)
{
	gchar *rel_pos = NULL;
	const gchar *message;
	gboolean end;
	dlr_async_task_t *cb_data = user_data;
	GError *error = NULL;
	dlr_device_data_t *device_data = cb_data->private;
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;

	end = gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					     &error, "RelTime",
					     G_TYPE_STRING, &rel_pos, NULL);
	if (!end || (rel_pos == NULL)) {
		message = (error != NULL) ? error->message : "Invalid result";
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "GetPositionInfo operation failed: %s",
					     message);
		goto on_error;
	}

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	g_strstrip(rel_pos);
	prv_add_reltime(cb_data->device, rel_pos, changed_props_vb);
	g_free(rel_pos);

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));
	prv_emit_signal_properties_changed(cb_data->device,
					   DLR_INTERFACE_PLAYER,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);

on_error:

	if (error != NULL)
		g_error_free(error);

	device_data->local_cb(cb_data);
}

static void prv_get_position_info(dlr_async_task_t *cb_data)
{
	dlr_device_context_t *context;

	context = dlr_device_get_context(cb_data->device);

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dlr_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->service_proxies.av_proxy;

	g_object_add_weak_pointer((G_OBJECT(context->service_proxies.av_proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy,
						 "GetPositionInfo",
						 prv_get_position_info_cb,
						 cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 NULL);
}

/***********************************************************************/
/*  Rational numbers parameters of the following functions are formed  */
/* like this : «2» «5/6». A decimal notation like «2.6» is not allowed */
/***********************************************************************/
static inline long prv_rational_get_numerator(const char *r)
{
	return strtol(r, NULL, 10);
}

static long prv_rational_get_denominator(const char *r)
{
	char *div_pos = strstr(r, "/");
	if (div_pos == NULL)
		goto exit;

	return strtol(div_pos + 1, NULL, 10);

exit:
	return 1;
}

static double prv_rational_to_double(const char *r, double precision)
{
	long p;
	long q;
	double result;

	p = prv_rational_get_numerator(r);
	if (p == 0)
		goto error;

	q = prv_rational_get_denominator(r);
	if (q == 0)
		goto error;

	result = (double)p/(double)q;

	if (precision != 0)
		result = round(result/precision) * precision;

	return result;

error:
	return 0.0;
}

static inline gboolean prv_rational_is_invalid(const char *val)
{
	return (prv_rational_get_numerator(val) == 0) ||
		(prv_rational_get_denominator(val) == 0);
}

static gint prv_compare_rationals(const gchar *a, const gchar *b)
{
	long a_numerator = prv_rational_get_numerator(a);
	long b_numerator = prv_rational_get_numerator(b);
	long a_denominator = prv_rational_get_denominator(a);
	long b_denominator = prv_rational_get_denominator(b);

	return (a_numerator * b_denominator) - (b_numerator * a_denominator);
}

static void prv_get_rates_values(GList *allowed_tp_speeds,
				 GVariant **mpris_tp_speeds,
				 GPtrArray **upnp_tp_speeds,
				 double *min_rate, double *max_rate)
{
	char *rate;
	char *min_rate_str;
	char *max_rate_str;
	GList *list;
	GVariantBuilder vb;
	const double precision = 0.01;

	if (allowed_tp_speeds == NULL)
		goto exit;

	g_variant_builder_init(&vb, G_VARIANT_TYPE("ad"));

	list = allowed_tp_speeds;

	min_rate_str = list->data;
	max_rate_str = min_rate_str;

	if (*upnp_tp_speeds != NULL)
		g_ptr_array_free(*upnp_tp_speeds, TRUE);

	*upnp_tp_speeds = g_ptr_array_new_with_free_func(g_free);

	for (; list != NULL; list = list->next) {
		rate = (char *)list->data;

		if (prv_rational_is_invalid(rate))
			continue;

		g_ptr_array_add(*upnp_tp_speeds, g_strdup(rate));

		g_variant_builder_add(&vb, "d",
				      prv_rational_to_double(rate, precision));

		if (prv_compare_rationals(min_rate_str, rate) > 0)
			min_rate_str = rate;
		else if (prv_compare_rationals(max_rate_str, rate) < 0)
			max_rate_str = rate;
	}

	*mpris_tp_speeds = g_variant_builder_end(&vb);

	*min_rate = prv_rational_to_double(min_rate_str, precision);
	*max_rate = prv_rational_to_double(max_rate_str, precision);

exit:
	return;
}

static gboolean prv_get_av_service_states_values(GUPnPServiceProxy *av_proxy,
						 GVariant **mpris_tp_speeds,
						 GPtrArray **upnp_tp_speeds,
						 double *min_rate,
						 double *max_rate)
{
	const GUPnPServiceStateVariableInfo *svi;
	GUPnPServiceIntrospection *introspection;
	GError *error = NULL;
	GVariant *speeds = NULL;
	GList *allowed_values;
	gpointer weak_ref = NULL;
	gboolean  device_alive = TRUE;

	/* TODO: this weak_ref hack is needed as
	   gupnp_service_info_get_introspection iterates the main loop.
	   This can result in our device getting deleted before this
	   function returns.  Ultimately, this code needs to be re-written
	   to use gupnp_service_info_get_introspection_async but this cannot
	   really be done until GUPnP provides a way to cancel this function. */

	weak_ref = av_proxy;
	g_object_add_weak_pointer(G_OBJECT(av_proxy), &weak_ref);

	introspection = gupnp_service_info_get_introspection(
		GUPNP_SERVICE_INFO(av_proxy),
		&error);

	if (!weak_ref) {
		DLEYNA_LOG_WARNING("Lost device during introspection call");
		device_alive = FALSE;
		goto exit;
	}

	g_object_remove_weak_pointer(G_OBJECT(av_proxy), &weak_ref);

	if (error != NULL) {
		DLEYNA_LOG_DEBUG(
			"failed to fetch AV service introspection file");

		g_error_free(error);

		goto exit;
	}

	svi = gupnp_service_introspection_get_state_variable(
							introspection,
							"TransportPlaySpeed");

	if (svi && svi->allowed_values) {
		allowed_values = svi->allowed_values;

		allowed_values = g_list_sort(allowed_values, compare_speeds);

		prv_get_rates_values(allowed_values, &speeds, upnp_tp_speeds,
				     min_rate, max_rate);

		*mpris_tp_speeds = g_variant_ref_sink(speeds);
	}

	g_object_unref(introspection);

exit:

	return device_alive;
}

static gboolean prv_get_rc_service_states_values(GUPnPServiceProxy *rc_proxy,
						 guint *max_volume)
{
	const GUPnPServiceStateVariableInfo *svi;
	GUPnPServiceIntrospection *introspection;
	GError *error = NULL;
	gpointer weak_ref = NULL;
	gboolean device_alive = TRUE;

	/* TODO: this weak_ref hack is needed as
	   gupnp_service_info_get_introspection iterates the main loop.
	   This can result in our device getting deleted before this
	   function returns.  Ultimately, this code needs to be re-written
	   to use gupnp_service_info_get_introspection_async but this cannot
	   really be done until GUPnP provides a way to cancel this function. */

	weak_ref = rc_proxy;
	g_object_add_weak_pointer(G_OBJECT(rc_proxy), &weak_ref);

	introspection = gupnp_service_info_get_introspection(
		GUPNP_SERVICE_INFO(rc_proxy),
		&error);

	if (!weak_ref) {
		DLEYNA_LOG_WARNING("Lost device during introspection call");
		device_alive = FALSE;
		goto exit;
	}

	g_object_remove_weak_pointer(G_OBJECT(rc_proxy), &weak_ref);

	if (error != NULL) {
		DLEYNA_LOG_DEBUG(
			"failed to fetch RC service introspection file");

		g_error_free(error);

		goto exit;
	}

	svi = gupnp_service_introspection_get_state_variable(introspection,
							     "Volume");
	if (svi != NULL)
		*max_volume = g_value_get_uint(&svi->maximum);

	g_object_unref(introspection);

exit:

	return device_alive;
}


static void prv_update_device_props(GUPnPDeviceInfo *proxy, GHashTable *props)
{
	GVariant *val;
	gchar *str;

	val = g_variant_ref_sink(g_variant_new_string(
				gupnp_device_info_get_device_type(proxy)));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_DEVICE_TYPE, val);

	val = g_variant_ref_sink(g_variant_new_string(
					gupnp_device_info_get_udn(proxy)));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_UDN, val);

	str = gupnp_device_info_get_friendly_name(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_FRIENDLY_NAME, val);
	g_free(str);

	str = gupnp_device_info_get_icon_url(proxy, NULL, -1, -1, -1, FALSE,
					     NULL, NULL, NULL, NULL);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_ICON_URL, val);
	g_free(str);

	str = gupnp_device_info_get_manufacturer(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_MANUFACTURER, val);
	g_free(str);

	str = gupnp_device_info_get_manufacturer_url(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_MANUFACTURER_URL, val);
	g_free(str);

	str = gupnp_device_info_get_model_description(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_MODEL_DESCRIPTION, val);
	g_free(str);

	str = gupnp_device_info_get_model_name(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_MODEL_NAME, val);
	g_free(str);

	str = gupnp_device_info_get_model_number(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_MODEL_NUMBER, val);
	g_free(str);

	str = gupnp_device_info_get_serial_number(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_SERIAL_NUMBER, val);
	g_free(str);

	str = gupnp_device_info_get_presentation_url(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLR_INTERFACE_PROP_PRESENTATION_URL, val);
	g_free(str);

}

static void prv_add_player_speed_props(GHashTable *player_props,
				       double min_rate, double max_rate,
				       GVariant *mpris_transport_play_speeds,
				       GVariantBuilder *changed_props_vb)
{
	GVariant *val;

	if (min_rate != 0) {
		val = g_variant_ref_sink(g_variant_new_double(min_rate));
		prv_change_props(player_props,
				 DLR_INTERFACE_PROP_MINIMUM_RATE,
				 val, changed_props_vb);
	}

	if (max_rate != 0) {
		val = g_variant_ref_sink(g_variant_new_double(max_rate));
		prv_change_props(player_props,
				 DLR_INTERFACE_PROP_MAXIMUM_RATE,
				 val, changed_props_vb);
	}

	if (mpris_transport_play_speeds != NULL) {
		val = g_variant_ref_sink(mpris_transport_play_speeds);
		prv_change_props(player_props,
				 DLR_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS,
				 val, changed_props_vb);
	}
}

static gboolean prv_props_update(dlr_device_t *device, dlr_task_t *task)
{
	GVariant *val;
	GUPnPDeviceInfo *info;
	dlr_device_context_t *context;
	dlr_service_proxies_t *service_proxies;
	dlr_props_t *props = &device->props;
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	gboolean device_alive = TRUE;

	context = dlr_device_get_context(device);

	val = g_variant_ref_sink(g_variant_new_boolean(FALSE));
	g_hash_table_insert(props->root_props, DLR_INTERFACE_PROP_CAN_QUIT,
			    val);

	g_hash_table_insert(props->root_props, DLR_INTERFACE_PROP_CAN_RAISE,
			    g_variant_ref(val));

	g_hash_table_insert(props->root_props,
			    DLR_INTERFACE_PROP_CAN_SET_FULLSCREEN,
			    g_variant_ref(val));

	g_hash_table_insert(props->root_props,
			    DLR_INTERFACE_PROP_HAS_TRACK_LIST,
			    g_variant_ref(val));

	info = (GUPnPDeviceInfo *)context->device_proxy;

	prv_update_device_props(info, props->device_props);

	val = g_hash_table_lookup(props->device_props,
			    DLR_INTERFACE_PROP_FRIENDLY_NAME);
	g_hash_table_insert(props->root_props, DLR_INTERFACE_PROP_IDENTITY,
			    g_variant_ref(val));

	service_proxies = &context->service_proxies;

	/* TODO: We should not retrieve these values here.  They should be
	   retrieved during device construction. */

	if (service_proxies->av_proxy)
		if (!prv_get_av_service_states_values(
			    service_proxies->av_proxy,
			    &device->mpris_transport_play_speeds,
			    &device->transport_play_speeds,
			    &device->min_rate,
			    &device->max_rate)) {
			DLEYNA_LOG_DEBUG("Lost Device AV");

			device_alive = FALSE;
			goto on_lost_device;
		}

	/* TODO: We should not retrieve these values here.  They should be
	   retrieved during device construction. */

	if (service_proxies->rc_proxy)
		if (!prv_get_rc_service_states_values(service_proxies->rc_proxy,
						      &device->max_volume)) {
			DLEYNA_LOG_DEBUG("Lost Device RC");
			device_alive = FALSE;
			goto on_lost_device;
		}

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	prv_add_player_speed_props(device->props.player_props,
				   device->min_rate, device->max_rate,
				   device->mpris_transport_play_speeds,
				   changed_props_vb);

	prv_add_all_actions(device, changed_props_vb);
	device->props.synced = TRUE;

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));
	prv_emit_signal_properties_changed(device,
					   DLR_INTERFACE_PLAYER,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);

on_lost_device:

	return device_alive;
}

static void prv_complete_get_prop(dlr_async_task_t *cb_data)
{
	prv_get_prop(cb_data);
	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);
}

static void prv_complete_get_props(dlr_async_task_t *cb_data)
{
	prv_get_props(cb_data);
	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);
}

static void prv_simple_call_cb(GUPnPServiceProxy *proxy,
			       GUPnPServiceProxyAction *action,
			       gpointer user_data)
{
	dlr_async_task_t *cb_data = user_data;
	GError *upnp_error = NULL;

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error, NULL)) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "Operation failed: %s",
					     upnp_error->message);
		g_error_free(upnp_error);
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);
}

static void prv_set_volume(dlr_async_task_t *cb_data, GVariant *params)
{
	double volume;

	volume = g_variant_get_double(params) * cb_data->device->max_volume;

	DLEYNA_LOG_INFO("Set device volume to %d/%d", (guint)volume,
			cb_data->device->max_volume);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy, "SetVolume",
						 prv_simple_call_cb, cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 "Channel",
						 G_TYPE_STRING, "Master",
						 "DesiredVolume",
						 G_TYPE_UINT, (guint) volume,
						 NULL);
}

static void prv_set_mute(dlr_async_task_t *cb_data, GVariant *params)
{
	gboolean mute;

	mute = g_variant_get_boolean(params);

	DLEYNA_LOG_INFO("Set device mute state to %s", mute ? "TRUE" : "FALSE");

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy, "SetMute",
						 prv_simple_call_cb, cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 "Channel",
						 G_TYPE_STRING, "Master",
						 "DesiredMute",
						 G_TYPE_BOOLEAN, mute,
						 NULL);
}

static GVariant *prv_get_rate_value_from_double(GVariant *params,
						gchar **upnp_rate,
						dlr_async_task_t *cb_data)
{
	dlr_device_t *dev = cb_data->device;
	GVariant *val = NULL;
	GVariant *tps;
	GVariantIter iter;
	double tps_value;
	double mpris_rate;
	GPtrArray *tp_speeds;
	int i;

	if (dev->dlna_transport_play_speeds != NULL) {
		tps = g_hash_table_lookup(dev->props.player_props,
				DLR_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS);

		tp_speeds = dev->dlna_transport_play_speeds;
	} else {
		tps = dev->mpris_transport_play_speeds;

		tp_speeds = dev->transport_play_speeds;
	}

	if (tps == NULL) {
		cb_data->error =
			g_error_new(DLEYNA_SERVER_ERROR,
				    DLEYNA_ERROR_OPERATION_FAILED,
				    "TransportPlaySpeeds list is empty");
		goto exit;
	}

	mpris_rate = g_variant_get_double(params);

	i = 0;

	g_variant_iter_init(&iter, tps);
	while (g_variant_iter_next(&iter, "d", &tps_value)) {

		if (fabs(mpris_rate - tps_value) <= 0.01) {
			val = g_variant_ref_sink(
				g_variant_new_double(tps_value));

			*upnp_rate = g_ptr_array_index(tp_speeds, i);

			break;
		}

		i++;
	}

	if (val == NULL)
		cb_data->error =
			g_error_new(DLEYNA_SERVER_ERROR, DLEYNA_ERROR_BAD_QUERY,
				    "Value %.2f not in TransportPlaySpeeds",
				    mpris_rate);

exit:

	return val;
}

static void prv_set_rate(GVariant *params, dlr_async_task_t *cb_data)
{
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	GVariant *val;
	gchar *rate;

	if (g_variant_is_of_type(params, G_VARIANT_TYPE_DOUBLE) == FALSE) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_BAD_QUERY,
					     "Parameter is not a double");
		goto exit;
	}

	rate = NULL;

	val = prv_get_rate_value_from_double(params, &rate, cb_data);
	if (val == NULL)
		goto exit;

	DLEYNA_LOG_INFO("Set device rate to %s", rate);

	if (!strcmp(cb_data->device->rate, rate)) {
		g_variant_unref(val);

		goto exit;
	}

	g_free(cb_data->device->rate);
	cb_data->device->rate = g_strdup(rate);

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	prv_change_props(cb_data->device->props.player_props,
			 DLR_INTERFACE_PROP_RATE, val, changed_props_vb);

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));
	prv_emit_signal_properties_changed(cb_data->device,
					   DLR_INTERFACE_PLAYER,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);
exit:

	return;
}

void dlr_device_set_prop(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_device_context_t *context;
	dlr_task_set_prop_t *set_prop = &task->ut.set_prop;

	cb_data->cb = cb;
	cb_data->device = device;

	if (g_strcmp0(set_prop->interface_name, DLR_INTERFACE_PLAYER) != 0 &&
	    g_strcmp0(set_prop->interface_name, "") != 0) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Interface %s not managed for property setting",
					     set_prop->interface_name);
		goto exit;
	}

	if (g_strcmp0(set_prop->prop_name, DLR_INTERFACE_PROP_RATE) == 0) {
		prv_set_rate(set_prop->params, cb_data);
		goto exit;
	}

	if ((g_strcmp0(set_prop->prop_name, DLR_INTERFACE_PROP_VOLUME) != 0) &&
	    (g_strcmp0(set_prop->prop_name, DLR_INTERFACE_PROP_MUTE) != 0)) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_PROPERTY,
					     "Property %s not managed for setting",
					     set_prop->prop_name);
		goto exit;
	}

	context = dlr_device_get_context(device);

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dlr_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->service_proxies.rc_proxy;

	g_object_add_weak_pointer((G_OBJECT(context->service_proxies.rc_proxy)),
				  (gpointer *)&cb_data->proxy);

	if (g_strcmp0(set_prop->prop_name, DLR_INTERFACE_PROP_MUTE) == 0)
		prv_set_mute(cb_data, set_prop->params);
	else
		prv_set_volume(cb_data, set_prop->params);

	return;

exit:

	g_idle_add(dlr_async_task_complete, cb_data);
}

void dlr_device_get_prop(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_get_prop_t *get_prop = &task->ut.get_prop;
	dlr_device_data_t *device_cb_data;

	/* Need to check to see if the property is DLR_INTERFACE_PROP_POSITION.
	   If it is we need to call GetPositionInfo.  This value is not evented.
	   Otherwise we can just update the value straight away. */

	if ((!strcmp(get_prop->interface_name, DLR_INTERFACE_PLAYER) ||
	     !strcmp(get_prop->interface_name, "")) &&
	    (!strcmp(task->ut.get_prop.prop_name,
			DLR_INTERFACE_PROP_POSITION))) {
		/* Need to read the current position.  This property is not
		   evented */

		device_cb_data = g_new(dlr_device_data_t, 1);
		device_cb_data->local_cb = prv_complete_get_prop;

		cb_data->cb = cb;
		cb_data->private = device_cb_data;
		cb_data->free_private = g_free;
		cb_data->device = device;

		prv_get_position_info(cb_data);
	} else {
		cb_data->cb = cb;
		cb_data->device = device;

		if (!device->props.synced && !prv_props_update(device, task)) {
			cb_data->error = g_error_new(
				DLEYNA_SERVER_ERROR,
				DLEYNA_ERROR_OPERATION_FAILED,
				"Lost Device");
		} else {
			prv_get_prop(cb_data);
		}

		(void) g_idle_add(dlr_async_task_complete, cb_data);
	}
}

void dlr_device_get_all_props(dlr_device_t *device, dlr_task_t *task,
			      dlr_upnp_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_get_props_t *get_props = &task->ut.get_props;
	dlr_device_data_t *device_cb_data;

	cb_data->cb = cb;
	cb_data->device = device;

	if (!device->props.synced && !prv_props_update(device, task)) {
		cb_data->error = g_error_new(
			DLEYNA_SERVER_ERROR,
			DLEYNA_ERROR_OPERATION_FAILED,
			"Lost Device");
		(void) g_idle_add(dlr_async_task_complete, cb_data);
	} else if ((!strcmp(get_props->interface_name, DLR_INTERFACE_PLAYER) ||
	     !strcmp(get_props->interface_name, ""))) {
		/* Need to read the current position.  This property is not
		   evented */

		device_cb_data = g_new(dlr_device_data_t, 1);
		device_cb_data->local_cb = prv_complete_get_props;

		cb_data->private = device_cb_data;
		cb_data->free_private = g_free;

		prv_get_position_info(cb_data);
	} else {
		prv_get_props(cb_data);
		(void) g_idle_add(dlr_async_task_complete, cb_data);
	}
}

void dlr_device_play(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb)
{
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_INFO("Play at speed %s", device->rate);

	context = dlr_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dlr_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->service_proxies.av_proxy;

	g_object_add_weak_pointer((G_OBJECT(context->service_proxies.av_proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy,
						 "Play",
						 prv_simple_call_cb,
						 cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 "Speed", G_TYPE_STRING,
						 device->rate, NULL);
}

void dlr_device_play_pause(dlr_device_t *device, dlr_task_t *task,
			   dlr_upnp_task_complete_t cb)
{
	GVariant *state;

	state = g_hash_table_lookup(device->props.player_props,
				    DLR_INTERFACE_PROP_PLAYBACK_STATUS);

	if (state && !strcmp(g_variant_get_string(state, NULL), "Playing"))
		dlr_device_pause(device, task, cb);
	else
		dlr_device_play(device, task, cb);
}

static void prv_simple_command(dlr_device_t *device, dlr_task_t *task,
			       const gchar *command_name,
			       dlr_upnp_task_complete_t cb)
{
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;

	DLEYNA_LOG_INFO("%s", command_name);

	context = dlr_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dlr_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->service_proxies.av_proxy;

	g_object_add_weak_pointer((G_OBJECT(context->service_proxies.av_proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy,
						 command_name,
						 prv_simple_call_cb,
						 cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 NULL);
}

void dlr_device_pause(dlr_device_t *device, dlr_task_t *task,
		      dlr_upnp_task_complete_t cb)
{
	prv_simple_command(device, task, "Pause", cb);
}

void dlr_device_stop(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb)
{
	prv_simple_command(device, task, "Stop", cb);
}

void dlr_device_next(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb)
{
	prv_simple_command(device, task, "Next", cb);
}

void dlr_device_previous(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	prv_simple_command(device, task, "Previous", cb);
}

static void prv_reset_transport_speed_props(dlr_device_t *device)
{
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	GVariant *val;
	double min_rate;
	double max_rate;
	gboolean props_changed = FALSE;

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (device->dlna_transport_play_speeds != NULL) {
		g_ptr_array_free(device->dlna_transport_play_speeds, TRUE);
		device->dlna_transport_play_speeds = NULL;
	}

	val = g_hash_table_lookup(device->props.player_props,
				  DLR_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS);
	if (!val ||
	    !g_variant_equal(val, device->mpris_transport_play_speeds)) {
		min_rate = 0;
		val = g_hash_table_lookup(device->props.player_props,
					  DLR_INTERFACE_PROP_MINIMUM_RATE);
		if (!val || (g_variant_get_double(val) != device->min_rate))
			min_rate = device->min_rate;

		max_rate = 0;
		val = g_hash_table_lookup(device->props.player_props,
					  DLR_INTERFACE_PROP_MAXIMUM_RATE);
		if (!val || (g_variant_get_double(val) != device->max_rate))
			max_rate = device->max_rate;

		prv_add_player_speed_props(device->props.player_props,
					   min_rate, max_rate,
					   device->mpris_transport_play_speeds,
					   changed_props_vb);

		props_changed = TRUE;
	}

	if (!device->rate || g_strcmp0(device->rate, "1") != 0) {
		g_free(device->rate);
		device->rate = g_strdup("1");

		val = g_variant_ref_sink(g_variant_new_double(
					prv_map_transport_speed(device->rate)));
		prv_change_props(device->props.player_props,
				 DLR_INTERFACE_PROP_RATE, val,
				 changed_props_vb);

		props_changed = TRUE;
	}

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));
	if (props_changed)
		prv_emit_signal_properties_changed(device,
						   DLR_INTERFACE_PLAYER,
						   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);
}

static void prv_open_uri_cb(GUPnPServiceProxy *proxy,
			       GUPnPServiceProxyAction *action,
			       gpointer user_data)
{
	dlr_async_task_t *cb_data = user_data;
	GError *upnp_error = NULL;

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &upnp_error, NULL)) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "Operation failed: %s",
					     upnp_error->message);
		g_error_free(upnp_error);

		goto exit;
	}

	prv_reset_transport_speed_props(cb_data->device);

exit:

	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);
}

void dlr_device_open_uri(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_open_uri_t *open_uri_data = &task->ut.open_uri;
	gchar *metadata = open_uri_data->metadata;

	DLEYNA_LOG_INFO("URI: %s", open_uri_data->uri);
	DLEYNA_LOG_INFO("METADATA: %s", metadata ? metadata : "Not provided");

	context = dlr_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dlr_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->service_proxies.av_proxy;

	g_object_add_weak_pointer((G_OBJECT(context->service_proxies.av_proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy,
						 "SetAVTransportURI",
						 prv_open_uri_cb,
						 cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 "CurrentURI", G_TYPE_STRING,
						 open_uri_data->uri,
						 "CurrentURIMetaData",
						 G_TYPE_STRING,
						 metadata ? metadata : "",
						 NULL);
}

static void prv_device_set_position(dlr_device_t *device, dlr_task_t *task,
				    const gchar *pos_type,
				    dlr_upnp_task_complete_t cb)
{
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_seek_t *seek_data = &task->ut.seek;
	gchar *position;

	context = dlr_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	if (!strcmp(pos_type, "TRACK_NR"))
		position = g_strdup_printf("%u", seek_data->track_number);
	else
		position = prv_int64_to_duration(seek_data->position);

	DLEYNA_LOG_INFO("set %s position : %s", pos_type, position);

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dlr_async_task_cancelled),
				      cb_data, NULL);
	cb_data->cancellable = cb_data->cancellable;
	cb_data->proxy = context->service_proxies.av_proxy;

	g_object_add_weak_pointer((G_OBJECT(context->service_proxies.av_proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy,
						 "Seek",
						 prv_simple_call_cb,
						 cb_data,
						 "InstanceID", G_TYPE_INT, 0,
						 "Unit", G_TYPE_STRING,
						 pos_type,
						 "Target",
						 G_TYPE_STRING, position,
						 NULL);

	g_free(position);
}

void dlr_device_seek(dlr_device_t *device, dlr_task_t *task,
		     dlr_upnp_task_complete_t cb)
{
	prv_device_set_position(device, task,  "REL_TIME", cb);
}

void dlr_device_set_position(dlr_device_t *device, dlr_task_t *task,
			     dlr_upnp_task_complete_t cb)
{
	prv_device_set_position(device, task,  "ABS_TIME", cb);
}

void dlr_device_goto_track(dlr_device_t *device, dlr_task_t *task,
			   dlr_upnp_task_complete_t cb)
{
	prv_device_set_position(device, task,  "TRACK_NR", cb);
}

void dlr_device_host_uri(dlr_device_t *device, dlr_task_t *task,
			 dlr_host_service_t *host_service,
			 dlr_upnp_task_complete_t cb)
{
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_host_uri_t *host_uri = &task->ut.host_uri;
	gchar *url;
	GError *error = NULL;

	context = dlr_device_get_context(device);
	url = dlr_host_service_add(host_service, context->ip_address,
				   host_uri->client, host_uri->uri,
				   &error);

	cb_data->cb = cb;
	cb_data->device = device;
	if (url) {
		cb_data->task.result = g_variant_ref_sink(
						g_variant_new_string(url));
		g_free(url);
	} else {
		cb_data->error  = error;
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);
}

void dlr_device_remove_uri(dlr_device_t *device, dlr_task_t *task,
			   dlr_host_service_t *host_service,
			   dlr_upnp_task_complete_t cb)
{
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_host_uri_t *host_uri = &task->ut.host_uri;

	context = dlr_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	if (!dlr_host_service_remove(host_service, context->ip_address,
				     host_uri->client, host_uri->uri)) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "File not hosted for specified device");
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);
}

static void prv_build_icon_result(dlr_device_t *device, dlr_task_t *task)
{
	GVariant *out_p[2];

	out_p[0] = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
					     device->icon.bytes,
					     device->icon.size,
					     1);
	out_p[1] = g_variant_new_string(device->icon.mime_type);
	task->result = g_variant_ref_sink(g_variant_new_tuple(out_p, 2));
}

static void prv_get_icon_cancelled(GCancellable *cancellable,
				   gpointer user_data)
{
	prv_download_info_t *download = (prv_download_info_t *)user_data;

	dlr_async_task_cancelled(cancellable, download->task);

	if (download->msg) {
		soup_session_cancel_message(download->session, download->msg,
					    SOUP_STATUS_CANCELLED);
		DLEYNA_LOG_DEBUG("Cancelling device icon download");
	}
}

static void prv_free_download_info(prv_download_info_t *download)
{
	if (download->msg)
		g_object_unref(download->msg);
	g_object_unref(download->session);
	g_free(download);
}

static void prv_get_icon_session_cb(SoupSession *session,
				    SoupMessage *msg,
				    gpointer user_data)
{
	prv_download_info_t *download = (prv_download_info_t *)user_data;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)download->task;
	dlr_device_t *device = (dlr_device_t *)cb_data->device;

	if (msg->status_code == SOUP_STATUS_CANCELLED)
		goto out;

	if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		device->icon.size = msg->response_body->length;
		device->icon.bytes = g_malloc(device->icon.size);
		memcpy(device->icon.bytes, msg->response_body->data,
		       device->icon.size);

		prv_build_icon_result(device, &cb_data->task);
	} else {
		DLEYNA_LOG_DEBUG("Failed to GET device icon: %s",
				 msg->reason_phrase);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "Failed to GET device icon");
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

out:

	prv_free_download_info(download);
}

void dlr_device_get_icon(dlr_device_t *device, dlr_task_t *task,
			 dlr_upnp_task_complete_t cb)
{
	GUPnPDeviceInfo *info;
	dlr_device_context_t *context;
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	gchar *url;
	prv_download_info_t *download;

	cb_data->cb = cb;
	cb_data->device = device;

	if (device->icon.size != 0) {
		prv_build_icon_result(device, task);
		goto end;
	}

	context = dlr_device_get_context(device);
	info = (GUPnPDeviceInfo *)context->device_proxy;

	url = gupnp_device_info_get_icon_url(info, NULL, -1, -1, -1, FALSE,
					     &device->icon.mime_type, NULL,
					     NULL, NULL);
	if (url == NULL) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_NOT_SUPPORTED,
					     "No icon available");
		goto end;
	}

	download = g_new0(prv_download_info_t, 1);
	download->session = soup_session_async_new();
	download->msg = soup_message_new(SOUP_METHOD_GET, url);
	download->task = cb_data;

	if (!download->msg) {
		DLEYNA_LOG_WARNING("Invalid URL %s", url);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_BAD_RESULT,
					     "Invalid URL %s", url);
		prv_free_download_info(download);
		g_free(url);

		goto end;
	}

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(prv_get_icon_cancelled),
				      download, NULL);

	g_object_ref(download->msg);
	soup_session_queue_message(download->session, download->msg,
				   prv_get_icon_session_cb, download);

	g_free(url);

	return;

end:

	(void) g_idle_add(dlr_async_task_complete, cb_data);
}
