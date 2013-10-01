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
	dleyna_white_list_t *wl;
};

static GVariant *prv_build_wl_entries(dleyna_settings_t *settings)
{
	GVariant *result;

	result = dleyna_settings_white_list_entries(settings);

	if (result == NULL)
		result = g_variant_new("as", NULL);

	return result;
}

static void prv_add_all_props(dleyna_settings_t *settings, GVariantBuilder *vb)
{
	g_variant_builder_add(vb, "{sv}", DLR_INTERFACE_PROP_NEVER_QUIT,
			      g_variant_new_boolean(
					dleyna_settings_is_never_quit(
								settings)));

	g_variant_builder_add(vb, "{sv}", DLR_INTERFACE_PROP_WHITE_LIST_ENABLED,
			      g_variant_new_boolean(
					dleyna_settings_is_white_list_enabled(
								settings)));

	g_variant_builder_add(vb, "{sv}", DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES,
			      prv_build_wl_entries(settings));
}

static GVariant *prv_get_prop(dleyna_settings_t *settings, const gchar *prop)
{
	GVariant *retval = NULL;
#if DLEYNA_LOG_LEVEL & DLEYNA_LOG_LEVEL_DEBUG
	gchar *prop_str;
#endif

	if (!strcmp(prop, DLR_INTERFACE_PROP_NEVER_QUIT))
		retval = g_variant_ref_sink(g_variant_new_boolean(
					dleyna_settings_is_never_quit(
								settings)));
	else if (!strcmp(prop, DLR_INTERFACE_PROP_WHITE_LIST_ENABLED))
		retval = g_variant_ref_sink(g_variant_new_boolean(
					dleyna_settings_is_white_list_enabled(
								settings)));
	else if (!strcmp(prop, DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES))
		retval = g_variant_ref_sink(prv_build_wl_entries(settings));

#if DLEYNA_LOG_LEVEL & DLEYNA_LOG_LEVEL_DEBUG
	if (retval) {
		prop_str = g_variant_print(retval, FALSE);
		DLEYNA_LOG_DEBUG("Prop %s = %s", prop, prop_str);
		g_free(prop_str);
	}
#endif

	return retval;
}

static void prv_wl_notify_prop(dlr_manager_t *manager,
			       const gchar *prop_name,
			       GVariant *prop_val)
{
	GVariant *val;
	GVariantBuilder array;

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
}

dlr_manager_t *dlr_manager_new(dleyna_connector_id_t connection,
			       GUPnPContextManager *connection_manager)
{
	dlr_manager_t *manager = g_new0(dlr_manager_t, 1);
	GUPnPWhiteList *gupnp_wl;

	gupnp_wl = gupnp_context_manager_get_white_list(connection_manager);

	manager->connection = connection;
	manager->cm = connection_manager;
	manager->wl = dleyna_white_list_new(gupnp_wl);

	return manager;
}

void dlr_manager_delete(dlr_manager_t *manager)
{
	if (manager != NULL) {
		dleyna_white_list_delete(manager->wl);
		g_free(manager);
	}
}

dleyna_white_list_t *dlr_manager_get_white_list(dlr_manager_t *manager)
{
	return manager->wl;
}

void dlr_manager_get_all_props(dlr_manager_t *manager,
			       dleyna_settings_t *settings,
			       dlr_task_t *task,
			       dlr_manager_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_get_props_t *task_data = &task->ut.get_props;
	gchar *i_name = task_data->interface_name;
	GVariantBuilder vb;

	DLEYNA_LOG_DEBUG("Enter");
	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", i_name);

	cb_data->cb = cb;

	g_variant_builder_init(&vb, G_VARIANT_TYPE("a{sv}"));

	if (!strcmp(i_name, DLEYNA_SERVER_INTERFACE_MANAGER) ||
	    !strcmp(i_name, "")) {
		prv_add_all_props(settings, &vb);

		cb_data->task.result = g_variant_ref_sink(
						g_variant_builder_end(&vb));
	} else {
		DLEYNA_LOG_WARNING("Interface is unknown.");

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Interface is unknown.");
	}

	(void) g_idle_add(dlr_async_task_complete, cb_data);

	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_manager_get_prop(dlr_manager_t *manager,
			  dleyna_settings_t *settings,
			  dlr_task_t *task,
			  dlr_manager_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_get_prop_t *task_data = &task->ut.get_prop;
	gchar *i_name = task_data->interface_name;
	gchar *name = task_data->prop_name;

	DLEYNA_LOG_DEBUG("Enter");
	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", i_name);
	DLEYNA_LOG_DEBUG("Prop.%s", name);

	cb_data->cb = cb;

	if (!strcmp(i_name, DLEYNA_SERVER_INTERFACE_MANAGER) ||
	    !strcmp(i_name, "")) {
		cb_data->task.result = prv_get_prop(settings, name);

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

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_set_prop_never_quit(dlr_manager_t *manager,
				    dleyna_settings_t *settings,
				    gboolean never_quit,
				    GError **error)
{
	GVariant *prop_val;
	gboolean old_val;

	DLEYNA_LOG_DEBUG("Enter %d", never_quit);

	old_val = dleyna_settings_is_never_quit(settings);

	if (old_val == never_quit)
		goto exit;

	/* If no error, the white list will be updated in the reload callack
	 */
	dleyna_settings_set_never_quit(settings, never_quit, error);

	if (*error == NULL) {
		prop_val = g_variant_new_boolean(never_quit);
		prv_wl_notify_prop(manager,
				   DLR_INTERFACE_PROP_NEVER_QUIT,
				   prop_val);
	}

exit:
	DLEYNA_LOG_DEBUG("Exit");
	return;
}

static void prv_set_prop_wl_enabled(dlr_manager_t *manager,
				    dleyna_settings_t *settings,
				    gboolean enabled,
				    GError **error)
{
	GVariant *prop_val;
	gboolean old_val;

	DLEYNA_LOG_DEBUG("Enter %d", enabled);

	old_val = dleyna_settings_is_white_list_enabled(settings);

	if (old_val == enabled)
		goto exit;

	/* If no error, the white list will be updated in the reload callack
	 */
	dleyna_settings_set_white_list_enabled(settings, enabled, error);

	if (*error == NULL) {
		dleyna_white_list_enable(manager->wl, enabled);
		prop_val = g_variant_new_boolean(enabled);
		prv_wl_notify_prop(manager,
				   DLR_INTERFACE_PROP_WHITE_LIST_ENABLED,
				   prop_val);
	}

exit:
	DLEYNA_LOG_DEBUG("Exit");
	return;
}

static void prv_set_prop_wl_entries(dlr_manager_t *manager,
				    dleyna_settings_t *settings,
				    GVariant *entries,
				    GError **error)
{
	DLEYNA_LOG_DEBUG("Enter");

	if (strcmp(g_variant_get_type_string(entries), "as")) {
		DLEYNA_LOG_WARNING("Invalid parameter type. 'as' expected.");

		*error = g_error_new(DLEYNA_SERVER_ERROR,
				     DLEYNA_ERROR_BAD_QUERY,
				     "Invalid parameter type. 'as' expected.");
		goto exit;
	}

	/* If no error, the white list will be updated in the reload callack
	 * callack
	 */
	dleyna_settings_set_white_list_entries(settings, entries, error);

	if (*error == NULL) {
		dleyna_white_list_clear(manager->wl);
		dleyna_white_list_add_entries(manager->wl, entries);

		prv_wl_notify_prop(manager,
				   DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES,
				   entries);
	}
exit:
	DLEYNA_LOG_DEBUG("Exit");
}

void dlr_manager_set_prop(dlr_manager_t *manager,
			  dleyna_settings_t *settings,
			  dlr_task_t *task,
			  dlr_manager_task_complete_t cb)
{
	dlr_async_task_t *cb_data = (dlr_async_task_t *)task;
	dlr_task_set_prop_t *task_data = &task->ut.set_prop;
	GVariant *param = task_data->params;
	gchar *name = task_data->prop_name;
	gchar *i_name = task_data->interface_name;
	GError *error = NULL;

	DLEYNA_LOG_DEBUG("Enter");
	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", i_name);
	DLEYNA_LOG_DEBUG("Prop.%s", name);

	cb_data->cb = cb;

	if (strcmp(i_name, DLEYNA_SERVER_INTERFACE_MANAGER) &&
	    strcmp(i_name, "")) {
		DLEYNA_LOG_WARNING("Interface is unknown.");

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Interface is unknown.");
		goto exit;
	}

	if (!strcmp(name, DLR_INTERFACE_PROP_NEVER_QUIT))
		prv_set_prop_never_quit(manager, settings,
					g_variant_get_boolean(param),
					&error);
	else if (!strcmp(name, DLR_INTERFACE_PROP_WHITE_LIST_ENABLED))
		prv_set_prop_wl_enabled(manager, settings,
					g_variant_get_boolean(param),
					&error);
	else if (!strcmp(name, DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES))
		prv_set_prop_wl_entries(manager, settings, param, &error);
	else
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_PROPERTY,
					     "Unknown property");

	if (error != NULL)
		cb_data->error = error;

exit:
	(void) g_idle_add(dlr_async_task_complete, cb_data);
	DLEYNA_LOG_DEBUG("Exit");
}
