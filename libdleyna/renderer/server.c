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

#ifdef UA_PREFIX
	#define DLR_PRG_NAME UA_PREFIX " dLeyna/" VERSION
#else
	#define DLR_PRG_NAME "dLeyna/" VERSION
#endif

#define DLR_INTERFACE_GET_VERSION "GetVersion"
#define DLR_INTERFACE_GET_RENDERERS "GetRenderers"
#define DLR_INTERFACE_RESCAN "Rescan"
#define DLR_INTERFACE_RELEASE "Release"

#define DLR_INTERFACE_FOUND_RENDERER "FoundRenderer"
#define DLR_INTERFACE_LOST_RENDERER "LostRenderer"

#define DLR_INTERFACE_HOST_FILE "HostFile"
#define DLR_INTERFACE_REMOVE_FILE "RemoveFile"

#define DLR_INTERFACE_VERSION "Version"
#define DLR_INTERFACE_RENDERERS "Renderers"

#define DLR_INTERFACE_PATH "Path"
#define DLR_INTERFACE_URI "Uri"
#define DLR_INTERFACE_ID "Id"
#define DLR_INTERFACE_METADATA "Metadata"

#define DLR_INTERFACE_CHANGED_PROPERTIES "changed_properties"
#define DLR_INTERFACE_INVALIDATED_PROPERTIES "invalidated_properties"
#define DLR_INTERFACE_GET "Get"
#define DLR_INTERFACE_GET_ALL "GetAll"
#define DLR_INTERFACE_SET "Set"
#define DLR_INTERFACE_INTERFACE_NAME "interface_name"
#define DLR_INTERFACE_PROPERTY_NAME "property_name"
#define DLR_INTERFACE_PROPERTIES_VALUE "properties"
#define DLR_INTERFACE_VALUE "value"
#define DLR_INTERFACE_OFFSET "offset"
#define DLR_INTERFACE_POSITION "position"
#define DLR_INTERFACE_TRACKID "trackid"
#define DLR_INTERFACE_TRACK_NUMBER "TrackNumber"

#define DLR_INTERFACE_RAISE "Raise"
#define DLR_INTERFACE_QUIT "Quit"
#define DLR_INTERFACE_PLAY "Play"
#define DLR_INTERFACE_PLAY_PAUSE "PlayPause"
#define DLR_INTERFACE_NEXT "Next"
#define DLR_INTERFACE_PREVIOUS "Previous"
#define DLR_INTERFACE_PAUSE "Pause"
#define DLR_INTERFACE_STOP "Stop"
#define DLR_INTERFACE_OPEN_URI "OpenUri"
#define DLR_INTERFACE_OPEN_URI_EX "OpenUriEx"
#define DLR_INTERFACE_SEEK "Seek"
#define DLR_INTERFACE_SET_POSITION "SetPosition"
#define DLR_INTERFACE_GOTO_TRACK "GotoTrack"

#define DLR_INTERFACE_CANCEL "Cancel"
#define DLR_INTERFACE_GET_ICON "GetIcon"
#define DLR_INTERFACE_RESOLUTION "Resolution"
#define DLR_INTERFACE_ICON_BYTES "Bytes"
#define DLR_INTERFACE_MIME_TYPE "MimeType"
#define DLR_INTERFACE_REQ_MIME_TYPE "RequestedMimeType"

typedef struct dlr_context_t_ dlr_context_t;
struct dlr_context_t_ {
	guint dlr_id;
	dleyna_connector_id_t connection;
	guint watchers;
	dleyna_task_processor_t *processor;
	const dleyna_connector_t *connector;
	dlr_upnp_t *upnp;
	dleyna_settings_t *settings;
};

static dlr_context_t g_context;

static const gchar g_root_introspection[] =
	"<node>"
	"  <interface name='"DLEYNA_SERVER_INTERFACE_MANAGER"'>"
	"    <method name='"DLR_INTERFACE_GET_VERSION"'>"
	"      <arg type='s' name='"DLR_INTERFACE_VERSION"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_RELEASE"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_GET_RENDERERS"'>"
	"      <arg type='ao' name='"DLR_INTERFACE_RENDERERS"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_RESCAN"'>"
	"    </method>"
	"    <signal name='"DLR_INTERFACE_FOUND_RENDERER"'>"
	"      <arg type='o' name='"DLR_INTERFACE_PATH"'/>"
	"    </signal>"
	"    <signal name='"DLR_INTERFACE_LOST_RENDERER"'>"
	"      <arg type='o' name='"DLR_INTERFACE_PATH"'/>"
	"    </signal>"
	"  </interface>"
	"</node>";

static const gchar g_server_introspection[] =
	"<node>"
	"  <interface name='"DLR_INTERFACE_PROPERTIES"'>"
	"    <method name='"DLR_INTERFACE_GET"'>"
	"      <arg type='s' name='"DLR_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLR_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"DLR_INTERFACE_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_GET_ALL"'>"
	"      <arg type='s' name='"DLR_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='a{sv}' name='"DLR_INTERFACE_PROPERTIES_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_SET"'>"
	"      <arg type='s' name='"DLR_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLR_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"DLR_INTERFACE_VALUE"'"
	"           direction='in'/>"
	"    </method>"
	"    <signal name='"DLR_INTERFACE_PROPERTIES_CHANGED"'>"
	"      <arg type='s' name='"DLR_INTERFACE_INTERFACE_NAME"'/>"
	"      <arg type='a{sv}' name='"DLR_INTERFACE_CHANGED_PROPERTIES"'/>"
	"      <arg type='as' name='"DLR_INTERFACE_INVALIDATED_PROPERTIES"'/>"
	"    </signal>"
	"  </interface>"
	"  <interface name='"DLR_INTERFACE_SERVER"'>"
	"    <method name='"DLR_INTERFACE_RAISE"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_QUIT"'>"
	"    </method>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_QUIT"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_RAISE"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_SET_FULLSCREEN"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_HAS_TRACK_LIST"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_IDENTITY"'"
	"       access='read'/>"
	"    <property type='as' name='"DLR_INTERFACE_PROP_SUPPORTED_URIS"'"
	"       access='read'/>"
	"    <property type='as' name='"DLR_INTERFACE_PROP_SUPPORTED_MIME"'"
	"       access='read'/>"
	"  </interface>"
	"  <interface name='"DLR_INTERFACE_PLAYER"'>"
	"    <method name='"DLR_INTERFACE_PLAY"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_PAUSE"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_PLAY_PAUSE"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_STOP"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_NEXT"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_PREVIOUS"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_OPEN_URI"'>"
	"      <arg type='s' name='"DLR_INTERFACE_URI"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_OPEN_URI_EX"'>"
	"      <arg type='s' name='"DLR_INTERFACE_URI"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLR_INTERFACE_METADATA"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_SEEK"'>"
	"      <arg type='x' name='"DLR_INTERFACE_OFFSET"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_SET_POSITION"'>"
	"      <arg type='o' name='"DLR_INTERFACE_TRACKID"'"
	"           direction='in'/>"
	"      <arg type='x' name='"DLR_INTERFACE_POSITION"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_GOTO_TRACK"'>"
	"      <arg type='u' name='"DLR_INTERFACE_TRACK_NUMBER"'"
	"           direction='in'/>"
	"    </method>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_PLAYBACK_STATUS"'"
	"       access='read'/>"
	"    <property type='d' name='"DLR_INTERFACE_PROP_RATE"'"
	"       access='readwrite'/>"
	"    <property type='d' name='"DLR_INTERFACE_PROP_MINIMUM_RATE"'"
	"       access='read'/>"
	"    <property type='d' name='"DLR_INTERFACE_PROP_MAXIMUM_RATE"'"
	"       access='read'/>"
	"    <property type='ad'"
	"       name='"DLR_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS"'"
	"       access='read'/>"
	"    <property type='d' name='"DLR_INTERFACE_PROP_VOLUME"'"
	"       access='readwrite'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_PLAY"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_SEEK"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_CONTROL"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_PAUSE"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_NEXT"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_CAN_PREVIOUS"'"
	"       access='read'/>"
	"    <property type='x' name='"DLR_INTERFACE_PROP_POSITION"'"
	"       access='read'/>"
	"    <property type='a{sv}' name='"DLR_INTERFACE_PROP_METADATA"'"
	"       access='read'/>"
	"    <property type='u' name='"DLR_INTERFACE_PROP_CURRENT_TRACK"'"
	"       access='read'/>"
	"    <property type='u' name='"DLR_INTERFACE_PROP_NUMBER_OF_TRACKS"'"
	"       access='read'/>"
	"    <property type='b' name='"DLR_INTERFACE_PROP_MUTE"'"
	"       access='readwrite'/>"
	"  </interface>"
	"  <interface name='"DLEYNA_INTERFACE_PUSH_HOST"'>"
	"    <method name='"DLR_INTERFACE_HOST_FILE"'>"
	"      <arg type='s' name='"DLR_INTERFACE_PATH"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLR_INTERFACE_URI"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_REMOVE_FILE"'>"
	"      <arg type='s' name='"DLR_INTERFACE_PATH"'"
	"           direction='in'/>"
	"    </method>"
	"  </interface>"
	"  <interface name='"DLEYNA_SERVER_INTERFACE_RENDERER_DEVICE"'>"
	"    <method name='"DLR_INTERFACE_CANCEL"'>"
	"    </method>"
	"    <method name='"DLR_INTERFACE_GET_ICON"'>"
	"      <arg type='s' name='"DLR_INTERFACE_REQ_MIME_TYPE"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLR_INTERFACE_RESOLUTION"'"
	"           direction='in'/>"
	"      <arg type='ay' name='"DLR_INTERFACE_ICON_BYTES"'"
	"           direction='out'/>"
	"      <arg type='s' name='"DLR_INTERFACE_MIME_TYPE"'"
	"           direction='out'/>"
	"    </method>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_DEVICE_TYPE"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_UDN"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_FRIENDLY_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_ICON_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_MANUFACTURER"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_MANUFACTURER_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_MODEL_DESCRIPTION"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_MODEL_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_MODEL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_SERIAL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_PRESENTATION_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"DLR_INTERFACE_PROP_PROTOCOL_INFO"'"
	"       access='read'/>"
	"  </interface>"
	"</node>";

static void prv_process_task(dleyna_task_atom_t *task, gpointer user_data);

static void prv_dlr_method_call(dleyna_connector_id_t conn,
				const gchar *sender,
				const gchar *object,
				const gchar *interface,
				const gchar *method,
				GVariant *parameters,
				dleyna_connector_msg_id_t invocation);

static void prv_dlr_device_method_call(dleyna_connector_id_t conn,
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

static void prv_dlr_player_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation);

static void prv_dlr_push_host_method_call(dleyna_connector_id_t conn,
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
	prv_dlr_method_call
};

static const dleyna_connector_dispatch_cb_t
				g_server_vtables[DLR_INTERFACE_INFO_MAX] = {
	/* MUST be in the exact same order as g_msu_server_introspection */
	prv_props_method_call,
	prv_dlr_device_method_call,
	prv_dlr_player_method_call,
	prv_dlr_push_host_method_call,
	prv_renderer_device_method_call
};

const dleyna_connector_t *dlr_renderer_get_connector(void)
{
	return g_context.connector;
}

dleyna_task_processor_t *dlr_renderer_service_get_task_processor(void)
{
	return g_context.processor;
}

dlr_upnp_t *dlr_renderer_service_get_upnp(void)
{
	return g_context.upnp;
}

static void prv_process_sync_task(dlr_task_t *task)
{
	GError *error;

	switch (task->type) {
	case DLR_TASK_GET_VERSION:
		dlr_task_complete(task);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		break;
	case DLR_TASK_GET_SERVERS:
		task->result = dlr_upnp_get_server_ids(g_context.upnp);
		dlr_task_complete(task);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		break;
	case DLR_TASK_RESCAN:
		dlr_upnp_rescan(g_context.upnp);
		dlr_task_complete(task);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		break;
	case DLR_TASK_RAISE:
	case DLR_TASK_QUIT:
		error = g_error_new(DLEYNA_SERVER_ERROR,
				    DLEYNA_ERROR_NOT_SUPPORTED,
				    "Command not supported.");
		dlr_task_fail(task, error);
		dleyna_task_queue_task_completed(task->atom.queue_id);
		g_error_free(error);
		break;
	default:
		break;
	}
}

static void prv_async_task_complete(dlr_task_t *task, GError *error)
{
	DLEYNA_LOG_DEBUG("Enter");

	if (error) {
		dlr_task_fail(task, error);
		g_error_free(error);
	} else {
		dlr_task_complete(task);
	}

	dleyna_task_queue_task_completed(task->atom.queue_id);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_process_async_task(dlr_task_t *task)
{
	dlr_async_task_t *async_task = (dlr_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	async_task->cancellable = g_cancellable_new();

	switch (task->type) {
	case DLR_TASK_GET_PROP:
		dlr_upnp_get_prop(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLR_TASK_GET_ALL_PROPS:
		dlr_upnp_get_all_props(g_context.upnp, task,
				       prv_async_task_complete);
		break;
	case DLR_TASK_SET_PROP:
		dlr_upnp_set_prop(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLR_TASK_PLAY:
		dlr_upnp_play(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case DLR_TASK_PAUSE:
		dlr_upnp_pause(g_context.upnp, task,
			       prv_async_task_complete);
		break;
	case DLR_TASK_PLAY_PAUSE:
		dlr_upnp_play_pause(g_context.upnp, task,
				    prv_async_task_complete);
		break;
	case DLR_TASK_STOP:
		dlr_upnp_stop(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case DLR_TASK_NEXT:
		dlr_upnp_next(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case DLR_TASK_PREVIOUS:
		dlr_upnp_previous(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLR_TASK_OPEN_URI:
		dlr_upnp_open_uri(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLR_TASK_SEEK:
		dlr_upnp_seek(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case DLR_TASK_SET_POSITION:
		dlr_upnp_set_position(g_context.upnp, task,
				      prv_async_task_complete);
		break;
	case DLR_TASK_GOTO_TRACK:
		dlr_upnp_goto_track(g_context.upnp, task,
				    prv_async_task_complete);
		break;
	case DLR_TASK_HOST_URI:
		dlr_upnp_host_uri(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLR_TASK_REMOVE_URI:
		dlr_upnp_remove_uri(g_context.upnp, task,
				    prv_async_task_complete);
		break;
	case DLR_TASK_GET_ICON:
		dlr_upnp_get_icon(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	default:
		break;
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_process_task(dleyna_task_atom_t *task, gpointer user_data)
{
	dlr_task_t *client_task = (dlr_task_t *)task;

	if (client_task->synchronous)
		prv_process_sync_task(client_task);
	else
		prv_process_async_task(client_task);
}

static void prv_cancel_task(dleyna_task_atom_t *task, gpointer user_data)
{
	dlr_task_cancel((dlr_task_t *)task);
}

static void prv_delete_task(dleyna_task_atom_t *task, gpointer user_data)
{
	dlr_task_delete((dlr_task_t *)task);
}

static void prv_remove_client(const gchar *name)
{
	dleyna_task_processor_remove_queues_for_source(g_context.processor,
						       name);

	dlr_upnp_lost_client(g_context.upnp, name);

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

	g_set_prgname(DLR_PRG_NAME);
}

static void prv_control_point_stop_service(void)
{
	dlr_upnp_unsubscribe(g_context.upnp);

	if (g_context.upnp)
		dlr_upnp_delete(g_context.upnp);

	if (g_context.connection) {
		if (g_context.dlr_id)
			g_context.connector->unpublish_object(
							g_context.connection,
							g_context.dlr_id);
	}
}

static void prv_control_point_free(void)
{
}

static void prv_add_task(dlr_task_t *task, const gchar *source,
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

static void prv_dlr_method_call(dleyna_connector_id_t conn,
				const gchar *sender, const gchar *object,
				const gchar *interface,
				const gchar *method, GVariant *parameters,
				dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task;

	DLEYNA_LOG_INFO("Calling %s method", method);

	if (!strcmp(method, DLR_INTERFACE_RELEASE)) {
		g_context.connector->unwatch_client(sender);
		prv_remove_client(sender);
		g_context.connector->return_response(invocation, NULL);
	} else {
		if (!strcmp(method, DLR_INTERFACE_GET_VERSION))
			task = dlr_task_get_version_new(invocation);
		else if (!strcmp(method, DLR_INTERFACE_GET_RENDERERS))
			task = dlr_task_get_servers_new(invocation);
		else if (!strcmp(method, DLR_INTERFACE_RESCAN))
			task = dlr_task_rescan_new(invocation);
		else
			goto finished;

		prv_add_task(task, sender, DLR_RENDERER_SINK);
	}

finished:

	return;
}

static const gchar *prv_get_device_id(const gchar *object, GError **error)
{
	dlr_device_t *device;

	device = dlr_device_from_path(object,
				dlr_upnp_get_server_udn_map(g_context.upnp));


	if (!device) {
		DLEYNA_LOG_WARNING("Cannot locate device for %s", object);

		*error = g_error_new(DLEYNA_SERVER_ERROR,
				     DLEYNA_ERROR_OBJECT_NOT_FOUND,
				     "Cannot locate device corresponding to the specified path");
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
	dlr_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, DLR_INTERFACE_GET_ALL))
		task = dlr_task_get_props_new(invocation, object, parameters);
	else if (!strcmp(method, DLR_INTERFACE_GET))
		task = dlr_task_get_prop_new(invocation, object, parameters);
	else if (!strcmp(method, DLR_INTERFACE_SET))
		task = dlr_task_set_prop_new(invocation, object, parameters);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_dlr_device_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, DLR_INTERFACE_RAISE))
		task = dlr_task_raise_new(invocation);
	else if (!strcmp(method, DLR_INTERFACE_QUIT))
		task = dlr_task_quit_new(invocation);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_dlr_player_method_call(dleyna_connector_id_t conn,
				       const gchar *sender,
				       const gchar *object,
				       const gchar *interface,
				       const gchar *method,
				       GVariant *parameters,
				       dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, DLR_INTERFACE_PLAY))
		task = dlr_task_play_new(invocation, object);
	else if (!strcmp(method, DLR_INTERFACE_PAUSE))
		task = dlr_task_pause_new(invocation, object);
	else if (!strcmp(method, DLR_INTERFACE_PLAY_PAUSE))
		task = dlr_task_play_pause_new(invocation, object);
	else if (!strcmp(method, DLR_INTERFACE_STOP))
		task = dlr_task_stop_new(invocation, object);
	else if (!strcmp(method, DLR_INTERFACE_NEXT))
		task = dlr_task_next_new(invocation, object);
	else if (!strcmp(method, DLR_INTERFACE_PREVIOUS))
		task = dlr_task_previous_new(invocation, object);
	else if (!strcmp(method, DLR_INTERFACE_OPEN_URI))
		task = dlr_task_open_uri_new(invocation, object, parameters);
	else if (!strcmp(method, DLR_INTERFACE_OPEN_URI_EX))
		task = dlr_task_open_uri_ex_new(invocation, object, parameters);
	else if (!strcmp(method, DLR_INTERFACE_SEEK))
		task = dlr_task_seek_new(invocation, object, parameters);
	else if (!strcmp(method, DLR_INTERFACE_SET_POSITION))
		task = dlr_task_set_position_new(invocation, object,
						 parameters);
	else if (!strcmp(method, DLR_INTERFACE_GOTO_TRACK))
		task = dlr_task_goto_track_new(invocation, object, parameters);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_dlr_push_host_method_call(dleyna_connector_id_t conn,
					  const gchar *sender,
					  const gchar *object,
					  const gchar *interface,
					  const gchar *method,
					  GVariant *parameters,
					  dleyna_connector_msg_id_t invocation)
{
	dlr_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto on_error;
	}

	if (!strcmp(method, DLR_INTERFACE_HOST_FILE))
		task = dlr_task_host_uri_new(invocation, object, sender,
					     parameters);
	else if (!strcmp(method, DLR_INTERFACE_REMOVE_FILE))
		task = dlr_task_remove_uri_new(invocation, object, sender,
					       parameters);
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
	dlr_task_t *task;
	const gchar *device_id = NULL;
	GError *error = NULL;
	const dleyna_task_queue_key_t *queue_id;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, DLR_INTERFACE_CANCEL)) {
		queue_id = dleyna_task_processor_lookup_queue(
							g_context.processor,
							sender, device_id);
		if (queue_id)
			dleyna_task_processor_cancel_queue(queue_id);

		g_context.connector->return_response(invocation, NULL);
	} else if (!strcmp(method, DLR_INTERFACE_GET_ICON)) {
		task = dlr_task_get_icon_new(invocation, object, parameters);

		prv_add_task(task, sender, device_id);
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
					   DLR_INTERFACE_FOUND_RENDERER,
					   g_variant_new("(o)", path),
					   NULL);
}

static void prv_lost_media_server(const gchar *path)
{
	DLEYNA_LOG_INFO("Lost %s", path);

	(void) g_context.connector->notify(g_context.connection,
					   DLEYNA_SERVER_OBJECT,
					   DLEYNA_SERVER_INTERFACE_MANAGER,
					   DLR_INTERFACE_LOST_RENDERER,
					   g_variant_new("(o)", path),
					   NULL);

	dleyna_task_processor_remove_queues_for_sink(g_context.processor, path);
}

static gboolean prv_control_point_start_service(
					dleyna_connector_id_t connection)
{
	gboolean retval = TRUE;

	g_context.connection = connection;

	g_context.dlr_id = g_context.connector->publish_object(
							connection,
							DLEYNA_SERVER_OBJECT,
							TRUE,
							0,
							g_root_vtables);

	if (!g_context.dlr_id) {
		retval = FALSE;
		goto out;
	} else {
		g_context.upnp = dlr_upnp_new(connection,
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
	prv_control_point_start_service,
	prv_control_point_stop_service
};

const dleyna_control_point_t *dleyna_control_point_get_renderer(void)
{
	return &g_control_point;
}

