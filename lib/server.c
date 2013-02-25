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


#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/signalfd.h>

#include <libdleyna/core/connector.h>
#include <libdleyna/core/control-point.h>
#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/task-processor.h>

#include "async.h"
#include "control-point-renderer.h"
#include "device.h"
#include "prop-defs.h"
#include "server.h"
#include "upnp.h"

#define RSU_INTERFACE_GET_VERSION "GetVersion"
#define RSU_INTERFACE_GET_SERVERS "GetServers"
#define RSU_INTERFACE_RELEASE "Release"

#define RSU_INTERFACE_FOUND_SERVER "FoundServer"
#define RSU_INTERFACE_LOST_SERVER "LostServer"

#define RSU_INTERFACE_HOST_FILE "HostFile"
#define RSU_INTERFACE_REMOVE_FILE "RemoveFile"

#define RSU_INTERFACE_VERSION "Version"
#define RSU_INTERFACE_SERVERS "Servers"

#define RSU_INTERFACE_PATH "Path"
#define RSU_INTERFACE_URI "Uri"
#define RSU_INTERFACE_ID "Id"

#define RSU_INTERFACE_CHANGED_PROPERTIES "changed_properties"
#define RSU_INTERFACE_INVALIDATED_PROPERTIES "invalidated_properties"
#define RSU_INTERFACE_GET "Get"
#define RSU_INTERFACE_GET_ALL "GetAll"
#define RSU_INTERFACE_SET "Set"
#define RSU_INTERFACE_INTERFACE_NAME "interface_name"
#define RSU_INTERFACE_PROPERTY_NAME "property_name"
#define RSU_INTERFACE_PROPERTIES_VALUE "properties"
#define RSU_INTERFACE_VALUE "value"
#define RSU_INTERFACE_OFFSET "offset"
#define RSU_INTERFACE_POSITION "position"
#define RSU_INTERFACE_TRACKID "trackid"

#define RSU_INTERFACE_RAISE "Raise"
#define RSU_INTERFACE_QUIT "Quit"
#define RSU_INTERFACE_PLAY "Play"
#define RSU_INTERFACE_PLAY_PAUSE "PlayPause"
#define RSU_INTERFACE_NEXT "Next"
#define RSU_INTERFACE_PREVIOUS "Previous"
#define RSU_INTERFACE_PAUSE "Pause"
#define RSU_INTERFACE_STOP "Stop"
#define RSU_INTERFACE_OPEN_URI "OpenUri"
#define RSU_INTERFACE_SEEK "Seek"
#define RSU_INTERFACE_SET_POSITION "SetPosition"

#define RSU_INTERFACE_CANCEL "Cancel"

#define RSU_SINK "renderer-service-upnp"

typedef struct rsu_context_t_ rsu_context_t;
struct rsu_context_t_ {
	guint rsu_id;
	dleyna_connector_id_t connection;
	guint watchers;
	dleyna_task_processor_t *processor;
	const dleyna_connector_t *connector;
	rsu_upnp_t *upnp;
	dleyna_settings_t *settings;
};

static rsu_context_t g_context;

static const gchar g_root_introspection[] =
	"<node>"
	"  <interface name='"DLEYNA_SERVER_INTERFACE_MANAGER"'>"
	"    <method name='"RSU_INTERFACE_GET_VERSION"'>"
	"      <arg type='s' name='"RSU_INTERFACE_VERSION"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_RELEASE"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_GET_SERVERS"'>"
	"      <arg type='as' name='"RSU_INTERFACE_SERVERS"'"
	"           direction='out'/>"
	"    </method>"
	"    <signal name='"RSU_INTERFACE_FOUND_SERVER"'>"
	"      <arg type='s' name='"RSU_INTERFACE_PATH"'/>"
	"    </signal>"
	"    <signal name='"RSU_INTERFACE_LOST_SERVER"'>"
	"      <arg type='s' name='"RSU_INTERFACE_PATH"'/>"
	"    </signal>"
	"  </interface>"
	"</node>";

static const gchar g_server_introspection[] =
	"<node>"
	"  <interface name='"RSU_INTERFACE_PROPERTIES"'>"
	"    <method name='"RSU_INTERFACE_GET"'>"
	"      <arg type='s' name='"RSU_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"RSU_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"RSU_INTERFACE_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_GET_ALL"'>"
	"      <arg type='s' name='"RSU_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='a{sv}' name='"RSU_INTERFACE_PROPERTIES_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_SET"'>"
	"      <arg type='s' name='"RSU_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"RSU_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"RSU_INTERFACE_VALUE"'"
	"           direction='in'/>"
	"    </method>"
	"    <signal name='"RSU_INTERFACE_PROPERTIES_CHANGED"'>"
	"      <arg type='s' name='"RSU_INTERFACE_INTERFACE_NAME"'/>"
	"      <arg type='a{sv}' name='"RSU_INTERFACE_CHANGED_PROPERTIES"'/>"
	"      <arg type='as' name='"RSU_INTERFACE_INVALIDATED_PROPERTIES"'/>"
	"    </signal>"
	"  </interface>"
	"  <interface name='"RSU_INTERFACE_SERVER"'>"
	"    <method name='"RSU_INTERFACE_RAISE"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_QUIT"'>"
	"    </method>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_QUIT"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_RAISE"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_SET_FULLSCREEN"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_HAS_TRACK_LIST"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_IDENTITY"'"
	"       access='read'/>"
	"    <property type='as' name='"RSU_INTERFACE_PROP_SUPPORTED_URIS"'"
	"       access='read'/>"
	"    <property type='as' name='"RSU_INTERFACE_PROP_SUPPORTED_MIME"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"RSU_INTERFACE_PLAYER"'>"
	"    <method name='"RSU_INTERFACE_PLAY"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_PAUSE"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_PLAY_PAUSE"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_STOP"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_NEXT"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_PREVIOUS"'>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_OPEN_URI"'>"
	"      <arg type='s' name='"RSU_INTERFACE_URI"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_SEEK"'>"
	"      <arg type='x' name='"RSU_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_SET_POSITION"'>"
	"      <arg type='o' name='"RSU_INTERFACE_TRACKID"'"
	"           direction='in'/>"
	"      <arg type='x' name='"RSU_INTERFACE_POSITION"'"
	"           direction='in'/>"
	"    </method>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_PLAYBACK_STATUS"'"
	"       access='read'/>"
	"    <property type='d' name='"RSU_INTERFACE_PROP_RATE"'"
	"       access='readwrite'/>"
	"    <property type='d' name='"RSU_INTERFACE_PROP_MINIMUM_RATE"'"
	"       access='read'/>"
	"    <property type='d' name='"RSU_INTERFACE_PROP_MAXIMUM_RATE"'"
	"       access='read'/>"
	"    <property type='ad'"
	"       name='"RSU_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS"'"
	"       access='read'/>"
	"    <property type='d' name='"RSU_INTERFACE_PROP_VOLUME"'"
	"       access='readwrite'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_PLAY"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_SEEK"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_CONTROL"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_PAUSE"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_NEXT"'"
	"       access='read'/>"
	"    <property type='b' name='"RSU_INTERFACE_PROP_CAN_PREVIOUS"'"
	"       access='read'/>"
	"    <property type='x' name='"RSU_INTERFACE_PROP_POSITION"'"
	"       access='read'/>"
	"    <property type='a{sv}' name='"RSU_INTERFACE_PROP_METADATA"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"DLEYNA_INTERFACE_PUSH_HOST"'>"
	"    <method name='"RSU_INTERFACE_HOST_FILE"'>"
	"      <arg type='s' name='"RSU_INTERFACE_PATH"'"
	"           direction='in'/>"
	"      <arg type='s' name='"RSU_INTERFACE_URI"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"RSU_INTERFACE_REMOVE_FILE"'>"
	"      <arg type='s' name='"RSU_INTERFACE_PATH"'"
	"           direction='in'/>"
	"    </method>"
	"  </interface>"
	"  <interface name='"DLEYNA_SERVER_INTERFACE_RENDERER_DEVICE"'>"
	"    <method name='"RSU_INTERFACE_CANCEL"'>"
	"    </method>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_DEVICE_TYPE"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_UDN"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_FRIENDLY_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_ICON_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_MANUFACTURER"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_MANUFACTURER_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_MODEL_DESCRIPTION"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_MODEL_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_MODEL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_SERIAL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_PRESENTATION_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"RSU_INTERFACE_PROP_PROTOCOL_INFO"'"
	"       access='read'/>"
	"  </interface>"
	"</node>";

static void prv_process_task(dleyna_task_atom_t *task, gpointer user_data);

static void prv_rsu_method_call(dleyna_connector_id_t conn,
				const gchar *sender,
				const gchar *object,
				const gchar *interface,
				const gchar *method,
				GVariant *parameters,
				dleyna_connector_msg_id_t invocation);

static void prv_rsu_device_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation);

static void prv_props_method_call(dleyna_connector_id_t conn,
				  const gchar *sender,
				  const gchar *object,
				  const gchar *interface,
				  const gchar *method,
				  GVariant *parameters,
				  dleyna_connector_msg_id_t invocation);

static void prv_rsu_player_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation);

static void prv_rsu_push_host_method_call(dleyna_connector_id_t conn,
					  const gchar *sender,
					  const gchar *object,
					  const gchar *interface,
					  const gchar *method,
					  GVariant *parameters,
					  dleyna_connector_msg_id_t invocation);

static void prv_renderer_device_method_call(
					dleyna_connector_id_t conn,
					const gchar *sender,
					const gchar *object,
					const gchar *interface,
					const gchar *method,
					GVariant *parameters,
					dleyna_connector_msg_id_t invocation);

static const dleyna_connector_dispatch_cb_t g_root_vtables[1] = {
	prv_rsu_method_call
};

static const dleyna_connector_dispatch_cb_t
				g_server_vtables[RSU_INTERFACE_INFO_MAX] = {
	/* MUST be in the exact same order as g_msu_server_introspection */
	prv_props_method_call,
	prv_rsu_device_method_call,
	prv_rsu_player_method_call,
	prv_rsu_push_host_method_call,
	prv_renderer_device_method_call
};

const dleyna_connector_t *dlr_server_get_connector(void)
{
	return g_context.connector;;
}

static void prv_process_sync_task(rsu_task_t *task)
{
	GError *error;

	switch (task->type) {
	case RSU_TASK_GET_VERSION:
		rsu_task_complete(task);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		break;
	case RSU_TASK_GET_SERVERS:
		task->result = rsu_upnp_get_server_ids(g_context.upnp);
		rsu_task_complete(task);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		break;
	case RSU_TASK_RAISE:
	case RSU_TASK_QUIT:
		error = g_error_new(DLEYNA_SERVER_ERROR,
				    DLEYNA_ERROR_NOT_SUPPORTED,
				    "Command not supported.");
		rsu_task_fail(task, error);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		g_error_free(error);
		break;
	default:
		break;
	}
}

static void prv_async_task_complete(rsu_task_t *task, GError *error)
{
	DLEYNA_LOG_DEBUG("Enter");

	if (error) {
		rsu_task_fail(task, error);
		g_error_free(error);
	} else {
		rsu_task_complete(task);
	}

	dleyna_task_queue_task_completed(task->atom.queue_id);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_process_async_task(rsu_task_t *task)
{
	rsu_async_task_t *async_task = (rsu_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	async_task->cancellable = g_cancellable_new();

	switch (task->type) {
	case RSU_TASK_GET_PROP:
		rsu_upnp_get_prop(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case RSU_TASK_GET_ALL_PROPS:
		rsu_upnp_get_all_props(g_context.upnp, task,
				       prv_async_task_complete);
		break;
	case RSU_TASK_SET_PROP:
		rsu_upnp_set_prop(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case RSU_TASK_PLAY:
		rsu_upnp_play(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case RSU_TASK_PAUSE:
		rsu_upnp_pause(g_context.upnp, task,
			       prv_async_task_complete);
		break;
	case RSU_TASK_PLAY_PAUSE:
		rsu_upnp_play_pause(g_context.upnp, task,
				    prv_async_task_complete);
		break;
	case RSU_TASK_STOP:
		rsu_upnp_stop(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case RSU_TASK_NEXT:
		rsu_upnp_next(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case RSU_TASK_PREVIOUS:
		rsu_upnp_previous(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case RSU_TASK_OPEN_URI:
		rsu_upnp_open_uri(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case RSU_TASK_SEEK:
		rsu_upnp_seek(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case RSU_TASK_SET_POSITION:
		rsu_upnp_set_position(g_context.upnp, task,
				      prv_async_task_complete);
		break;
	case RSU_TASK_HOST_URI:
		rsu_upnp_host_uri(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case RSU_TASK_REMOVE_URI:
		rsu_upnp_remove_uri(g_context.upnp, task,
				    prv_async_task_complete);
		break;
	default:
		break;
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_process_task(dleyna_task_atom_t *task, gpointer user_data)
{
	rsu_task_t *client_task = (rsu_task_t *)task;

	if (client_task->synchronous)
		prv_process_sync_task(client_task);
	else
		prv_process_async_task(client_task);
}

static void prv_cancel_task(dleyna_task_atom_t *task, gpointer user_data)
{
	rsu_task_cancel((rsu_task_t *)task);
}

static void prv_delete_task(dleyna_task_atom_t *task, gpointer user_data)
{
	rsu_task_delete((rsu_task_t *)task);
}

static void prv_remove_client(const gchar *name)
{
	dleyna_task_processor_remove_queues_for_source(g_context.processor,
						       name);

	rsu_upnp_lost_client(g_context.upnp, name);

	g_context.watchers--;
	if (g_context.watchers == 0)
		if (!dleyna_settings_is_never_quit(g_context.settings))
			dleyna_task_processor_set_quitting(g_context.processor);
}

static void prv_lost_client(const gchar *name)
{
	DLEYNA_LOG_INFO("Client %s lost", name);
	prv_remove_client(name);
}

static void prv_control_point_initialize(const dleyna_connector_t *connector,
					 dleyna_task_processor_t *processor,
					 dleyna_settings_t *settings)
{
	memset(&g_context, 0, sizeof(g_context));

	g_context.processor = processor;
	g_context.settings = settings;
	g_context.connector = connector;
	g_context.connector->set_client_lost_cb(prv_lost_client);
}

static void prv_control_point_free(void)
{
	if (g_context.upnp)
		rsu_upnp_delete(g_context.upnp);

	if (g_context.connection) {
		if (g_context.rsu_id)
			g_context.connector->unpublish_object(
							g_context.connection,
							g_context.rsu_id);
	}
}

static void prv_add_task(rsu_task_t *task, const gchar *source,
			 const gchar *sink)
{
	const dleyna_task_queue_key_t *queue_id;

	if (g_context.connector->watch_client(source))
		g_context.watchers++;

	queue_id = dleyna_task_processor_lookup_queue(g_context.processor,
						      source, sink);
	if (!queue_id)
		queue_id = dleyna_task_processor_add_queue(
					g_context.processor,
					source,
					sink,
					DLEYNA_TASK_QUEUE_FLAG_AUTO_START,
					prv_process_task,
					prv_cancel_task,
					prv_delete_task);

	dleyna_task_queue_add_task(queue_id, &task->atom);
}

static void prv_rsu_method_call(dleyna_connector_id_t conn,
				const gchar *sender, const gchar *object,
				const gchar *interface,
				const gchar *method, GVariant *parameters,
				dleyna_connector_msg_id_t invocation)
{
	rsu_task_t *task;

	DLEYNA_LOG_INFO("Calling %s method", method);

	if (!strcmp(method, RSU_INTERFACE_RELEASE)) {
		g_context.connector->unwatch_client(sender);
		prv_remove_client(sender);
		g_context.connector->return_response(invocation, NULL);
	} else {
		if (!strcmp(method, RSU_INTERFACE_GET_VERSION))
			task = rsu_task_get_version_new(invocation);
		else if (!strcmp(method, RSU_INTERFACE_GET_SERVERS))
			task = rsu_task_get_servers_new(invocation);
		else
			goto finished;

		prv_add_task(task, sender, RSU_SINK);
	}

finished:

	return;
}

static const gchar *prv_get_device_id(const gchar *object, GError **error)
{
	rsu_device_t *device;

	device = rsu_device_from_path(object,
				rsu_upnp_get_server_udn_map(g_context.upnp));


	if (!device) {
		DLEYNA_LOG_WARNING("Cannot locate device for %s", object);

		*error = g_error_new(DLEYNA_SERVER_ERROR,
				     DLEYNA_ERROR_OBJECT_NOT_FOUND,
				     "Cannot locate device corresponding to"
				     " the specified path");
		goto on_error;
	}

	return device->path;

on_error:

	return NULL;
}

static void prv_props_method_call(dleyna_connector_id_t conn,
				  const gchar *sender,
				  const gchar *object,
				  const gchar *interface,
				  const gchar *method,
				  GVariant *parameters,
				  dleyna_connector_msg_id_t invocation)
{
	rsu_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, RSU_INTERFACE_GET_ALL))
		task = rsu_task_get_props_new(invocation, object, parameters);
	else if (!strcmp(method, RSU_INTERFACE_GET))
		task = rsu_task_get_prop_new(invocation, object, parameters);
	else if (!strcmp(method, RSU_INTERFACE_SET))
		task = rsu_task_set_prop_new(invocation, object, parameters);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_rsu_device_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation)
{
	rsu_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, RSU_INTERFACE_RAISE))
		task = rsu_task_raise_new(invocation);
	else if (!strcmp(method, RSU_INTERFACE_QUIT))
		task = rsu_task_quit_new(invocation);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_rsu_player_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation)
{
	rsu_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, RSU_INTERFACE_PLAY))
		task = rsu_task_play_new(invocation, object);
	else if (!strcmp(method, RSU_INTERFACE_PAUSE))
		task = rsu_task_pause_new(invocation, object);
	else if (!strcmp(method, RSU_INTERFACE_PLAY_PAUSE))
		task = rsu_task_play_pause_new(invocation, object);
	else if (!strcmp(method, RSU_INTERFACE_STOP))
		task = rsu_task_stop_new(invocation, object);
	else if (!strcmp(method, RSU_INTERFACE_NEXT))
		task = rsu_task_next_new(invocation, object);
	else if (!strcmp(method, RSU_INTERFACE_PREVIOUS))
		task = rsu_task_previous_new(invocation, object);
	else if (!strcmp(method, RSU_INTERFACE_OPEN_URI))
		task = rsu_task_open_uri_new(invocation, object, parameters);
	else if (!strcmp(method, RSU_INTERFACE_SEEK))
		task = rsu_task_seek_new(invocation, object, parameters);
	else if (!strcmp(method, RSU_INTERFACE_SET_POSITION))
		task = rsu_task_set_position_new(invocation, object,
						 parameters);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_rsu_push_host_method_call(dleyna_connector_id_t conn,
					  const gchar *sender,
					  const gchar *object,
					  const gchar *interface,
					  const gchar *method,
					  GVariant *parameters,
					  dleyna_connector_msg_id_t invocation)
{
	rsu_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto on_error;
	}

	if (!strcmp(method, RSU_INTERFACE_HOST_FILE))
		task = rsu_task_host_uri_new(invocation, object, parameters);
	else if (!strcmp(method, RSU_INTERFACE_REMOVE_FILE))
		task = rsu_task_remove_uri_new(invocation, object, parameters);
	else
		goto on_error;

	prv_add_task(task, sender, device_id);

on_error:

	return;
}

static void prv_renderer_device_method_call(
					dleyna_connector_id_t conn,
					const gchar *sender,
					const gchar *object,
					const gchar *interface,
					const gchar *method,
					GVariant *parameters,
					dleyna_connector_msg_id_t invocation)
{
	const gchar *device_id = NULL;
	GError *error = NULL;
	const dleyna_task_queue_key_t *queue_id;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, RSU_INTERFACE_CANCEL)) {
		queue_id = dleyna_task_processor_lookup_queue(
							g_context.processor,
							sender, device_id);
		if (queue_id)
			dleyna_task_processor_cancel_queue(queue_id);

		g_context.connector->return_response(invocation, NULL);
	}

finished:

		return;
}

static void prv_found_media_server(const gchar *path)
{
	DLEYNA_LOG_INFO("New media server %s", path);

	(void) g_context.connector->notify(g_context.connection,
					   DLEYNA_SERVER_OBJECT,
					   DLEYNA_SERVER_INTERFACE_MANAGER,
					   RSU_INTERFACE_FOUND_SERVER,
					   g_variant_new("(s)", path),
					   NULL);
}

static void prv_lost_media_server(const gchar *path)
{
	DLEYNA_LOG_INFO("Lost %s", path);

	(void) g_context.connector->notify(g_context.connection,
					   DLEYNA_SERVER_OBJECT,
					   DLEYNA_SERVER_INTERFACE_MANAGER,
					   RSU_INTERFACE_LOST_SERVER,
					   g_variant_new("(s)", path),
					   NULL);

	dleyna_task_processor_remove_queues_for_sink(g_context.processor, path);
}

static gboolean prv_control_point_start_service(
					dleyna_connector_id_t connection)
{
	gboolean retval = TRUE;

	g_context.connection = connection;

	g_context.rsu_id = g_context.connector->publish_object(
							connection,
							DLEYNA_SERVER_OBJECT,
							TRUE,
							0,
							g_root_vtables);

	if (!g_context.rsu_id) {
		retval = FALSE;
		goto out;
	} else {
		g_context.upnp = rsu_upnp_new(connection,
					     g_server_vtables,
					     prv_found_media_server,
					     prv_lost_media_server);
	}

out:

	return retval;
}

static const gchar *prv_control_point_server_name(void)
{
	return DLEYNA_SERVER_NAME;
}

static const gchar *prv_control_point_server_introspection(void)
{
	return g_server_introspection;
}

static const gchar *prv_control_point_root_introspection(void)
{
	return g_root_introspection;
}

static const dleyna_control_point_t g_control_point = {
	prv_control_point_initialize,
	prv_control_point_free,
	prv_control_point_server_name,
	prv_control_point_server_introspection,
	prv_control_point_root_introspection,
	prv_control_point_start_service
};

const dleyna_control_point_t *dleyna_control_point_get_renderer(void)
{
	return &g_control_point;
}

