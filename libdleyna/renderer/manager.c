/*
 * dLeyna
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
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
 * Ludovic Ferrandis <ludovic.ferrandis@intel.com>
 *
 */

#include <glib.h>
#include <string.h>

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/service-task.h>
#include <libdleyna/core/white-list.h>

#include "async.h"
#include "manager.h"
#include "prop-defs.h"
#include "server.h"

struct dlr_manager_t_ {
	dleyna_connector_id_t connection;
	GUPnPContextManager *cm;
};

static void prv_add_list_wl_entries(gpointer data, gpointer user_data)
{
	GVariantBuilder *vb = (GVariantBuilder *)user_data;
	gchar *entry = (gchar *)data;

	g_variant_builder_add(vb, "s",  entry);
}

static void prv_add_all_props(GUPnPContextManager *manager, GVariantBuilder *vb)
{
	GUPnPWhiteList *wl;
	GList *list;
	GVariantBuilder vb2;

	wl = gupnp_context_manager_get_white_list(manager);
	list = gupnp_white_list_get_entries(wl);

	g_variant_builder_add(vb, "{sv}", DLR_INTERFACE_PROP_WHITE_LIST_ENABLED,
			      g_variant_new_boolean(
					gupnp_white_list_get_enabled(wl)));

	g_variant_builder_init(&vb2, G_VARIANT_TYPE("as"));
	g_list_foreach(list, prv_add_list_wl_entries, &vb2);

	g_variant_builder_add(vb, "{sv}", DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES,
			      g_variant_builder_end(&vb2));
}

static GVariant *prv_get_prop(GUPnPContextManager *manager, const gchar *prop)
{
	GVariant *retval = NULL;
	GUPnPWhiteList *wl;
	GVariantBuilder vb;
	GList *list;
	gboolean b_value;
#if DLEYNA_LOG_LEVEL & DLEYNA_LOG_LEVEL_DEBUG
	gchar *prop_str;
#endif

	wl = gupnp_context_manager_get_white_list(manager);

	if (!strcmp(prop, DLR_INTERFACE_PROP_WHITE_LIST_ENABLED)) {
		b_value = gupnp_white_list_get_enabled(wl);
		retval = g_variant_ref_sink(g_variant_new_boolean(b_value));

	} else if (!strcmp(prop, DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES)) {
		list = gupnp_white_list_get_entries(wl);

		g_variant_builder_init(&vb, G_VARIANT_TYPE("as"));
		g_list_foreach(list, prv_add_list_wl_entries, &vb);
		retval = g_variant_ref_sink(g_variant_builder_end(&vb));
	}

#if DLEYNA_LOG_LEVEL & DLEYNA_LOG_LEVEL_DEBUG
	if (retval) {
		prop_str = g_variant_print(retval, FALSE);
		DLEYNA_LOG_DEBUG("Prop %s = %s", prop, prop_str);
		g_free(prop_str);
	}
#endif

	return retval;
}

static void prv_wl_notify_prop(dlr_manager_t *manager, const gchar *prop_name)
{
	GVariant *prop_val;
	GVariant *val;
	GVariantBuilder array;

	prop_val = prv_get_prop(manager->cm, prop_name);

	g_variant_builder_init(&array, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&array, "{sv}", prop_name, prop_val);

	val = g_variant_new("(s@a{sv}as)", DLEYNA_SERVER_INTERFACE_MANAGER,
			    g_variant_builder_end(&array),
			    NULL);

	(void) dlr_renderer_get_connector()->notify(
					manager->connection,
					DLEYNA_SERVER_OBJECT,
					DLR_INTERFACE_PROPERTIES,
					DLR_INTERFACE_PROPERTIES_CHANGED,
					val,
					NULL);
	g_variant_unref(prop_val);
}

static void prv_wl_notify_enabled_prop(gpointer user_data)
{
	prv_wl_notify_prop((dlr_manager_t *)user_data,
			   DLR_INTERFACE_PROP_WHITE_LIST_ENABLED);
}

static void prv_wl_notify_entries_prop(gpointer user_data)
{
	prv_wl_notify_prop((dlr_manager_t *)user_data,
			   DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES);
}

dlr_manager_t *dlr_manager_new(dleyna_connector_id_t connection,
			       GUPnPContextManager *connection_manager)
{
	dlr_manager_t *manager = g_new0(dlr_manager_t, 1);
	dleyna_white_list_t wl_info;

	manager->connection = connection;
	manager->cm = connection_manager;

	wl_info.wl = gupnp_context_manager_get_white_list(manager->cm);
	wl_info.cb_enabled = prv_wl_notify_enabled_prop;
	wl_info.cb_entries = prv_wl_notify_entries_prop;
	wl_info.user_data = manager;

	dleyna_white_list_set_info(&wl_info);

	return manager;
}

void dlr_manager_delete(dlr_manager_t *manager)
{
	if (manager != NULL) {
		dleyna_white_list_set_info(NULL);
		g_free(manager);
	}
}

void dlr_manager_wl_enable(dlr_task_t *task)
{
	dleyna_white_list_enable(task->ut.white_list.enabled, TRUE);
}

void dlr_manager_wl_add_entries(dlr_task_t *task)
{
	dleyna_white_list_add_entries(task->ut.white_list.entries, TRUE);
}

void dlr_manager_wl_remove_entries(dlr_task_t *task)
{
	dleyna_white_list_remove_entries(task->ut.white_list.entries, TRUE);
}

void dlr_manager_wl_clear(dlr_task_t *task)
{
	dleyna_white_list_clear(TRUE);
}

void dlr_manager_get_all_props(dlr_manager_t *manager,
			       dlr_task_t *task,
			       dlr_manager_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_get_props_t *task_data = &task->ut.get_props;
	GVariantBuilder vb;

	DLEYNA_LOG_DEBUG("Enter");
	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);

	cb_data->cb = cb;

	g_variant_builder_init(&vb, G_VARIANT_TYPE("a{sv}"));

	if (!strcmp(task_data->interface_name,
		    DLEYNA_SERVER_INTERFACE_MANAGER) ||
	    !strcmp(task_data->interface_name, "")) {
		cb_data->cancel_id = g_cancellable_connect(
					cb_data->cancellable,
					G_CALLBACK(dlr_async_task_cancelled),
					cb_data, NULL);

		prv_add_all_props(manager->cm, &vb);

		cb_data->task.result = g_variant_ref_sink(
						g_variant_builder_end(&vb));
	} else {
		DLEYNA_LOG_WARNING("Interface is unknown.");

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Interface is unknown.");
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_manager_get_prop(dlr_manager_t *manager,
			  dlr_task_t *task,
			  dlr_manager_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_get_prop_t *task_data = &task->ut.get_prop;

	DLEYNA_LOG_DEBUG("Enter");
	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);
	DLEYNA_LOG_DEBUG("Prop.%s", task->ut.get_prop.prop_name);

	cb_data->cb = cb;

	if (!strcmp(task_data->interface_name,
		    DLEYNA_SERVER_INTERFACE_MANAGER) ||
	    !strcmp(task_data->interface_name, "")) {
		cb_data->cancel_id = g_cancellable_connect(
					cb_data->cancellable,
					G_CALLBACK(dlr_async_task_cancelled),
					cb_data, NULL);

		cb_data->task.result = prv_get_prop(manager->cm,
						    task_data->prop_name);

		if (!cb_data->task.result)
			cb_data->error = g_error_new(
						DLEYNA_SERVER_ERROR,
						DLEYNA_ERROR_UNKNOWN_PROPERTY,
						"Unknown property");
	} else {
		DLEYNA_LOG_WARNING("Interface is unknown.");

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Interface is unknown.");
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	DLEYNA_LOG_DEBUG("Exit");
}
