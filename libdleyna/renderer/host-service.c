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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libsoup/soup.h>
#include <glib.h>

#include <libgupnp-av/gupnp-dlna.h>
#include <libgupnp-dlna/gupnp-dlna-profile.h>
#include <libgupnp-dlna/gupnp-dlna-profile-guesser.h>

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>

#include "host-service.h"

#define DLR_HOST_SERVICE_ROOT "/dleynarenderer"

typedef struct dlr_host_file_t_ dlr_host_file_t;
struct dlr_host_file_t_ {
	unsigned int id;
	GPtrArray *clients;
	gchar *mime_type;
	GMappedFile *mapped_file;
	unsigned int mapped_count;
	gchar *path;
	gchar *dlna_header;
};

typedef struct dlr_host_server_t_ dlr_host_server_t;
struct dlr_host_server_t_ {
	GHashTable *files;
	SoupServer *soup_server;
	unsigned int counter;
};

struct dlr_host_service_t_ {
	GHashTable *servers;
};

static void prv_compute_mime_and_dlna_header(const gchar *filename,
					     gchar **mime_type,
					     gchar **dlna_header,
					     GError **error)
{
	gchar *uri;
	GString *header;
	GUPnPDLNAProfile *profile;
	GUPnPDLNAProfileGuesser *guesser;
	gboolean relaxed_mode = TRUE;
	gboolean extended_mode = TRUE;
	const char *profile_name;
	const char *dlna_mime_type;
	GUPnPDLNAOperation operation;
	GUPnPDLNAFlags flags;
	GUPnPDLNAConversion conversion;
	gchar *content_type = NULL;

	*mime_type = NULL;

	*dlna_header = NULL;

	*error = NULL;

	header = g_string_new("");

	guesser = gupnp_dlna_profile_guesser_new(relaxed_mode, extended_mode);

	uri = g_filename_to_uri(filename, NULL, error);
	if (uri == NULL) {
		DLEYNA_LOG_WARNING("Unable to convert filename: %s", filename);

		if (*error) {
			DLEYNA_LOG_WARNING("Error: %s", (*error)->message);

			g_error_free(*error);
			*error = NULL;
		}

		goto on_error;
	}

	profile = gupnp_dlna_profile_guesser_guess_profile_sync(guesser,
								uri,
								5000,
								NULL,
								error);
	if (profile == NULL) {
		DLEYNA_LOG_WARNING("Unable to guess profile for URI: %s", uri);

		if (*error) {
			DLEYNA_LOG_WARNING("Error: %s", (*error)->message);

			g_error_free(*error);
			*error = NULL;
		}

		goto on_error;
	}

	profile_name = gupnp_dlna_profile_get_name(profile);
	if (profile_name != NULL)
		g_string_append_printf(header, "DLNA.ORG_PN=%s;", profile_name);

	operation = GUPNP_DLNA_OPERATION_RANGE;
	g_string_append_printf(header, "DLNA.ORG_OP=%.2x;", operation);

	conversion = GUPNP_DLNA_CONVERSION_NONE;
	g_string_append_printf(header, "DLNA.ORG_CI=%.2x;", conversion);

	dlna_mime_type = gupnp_dlna_profile_get_mime(profile);
	if (dlna_mime_type != NULL) {
		*mime_type = g_strdup(dlna_mime_type);

		flags = GUPNP_DLNA_FLAGS_BACKGROUND_TRANSFER_MODE;
		flags |= GUPNP_DLNA_FLAGS_CONNECTION_STALL;
		flags |= GUPNP_DLNA_FLAGS_DLNA_V15;

		if (g_content_type_is_a(dlna_mime_type, "image/*")) {
			flags |= GUPNP_DLNA_FLAGS_INTERACTIVE_TRANSFER_MODE;
		} else if (g_content_type_is_a(dlna_mime_type, "audio/*") ||
			   g_content_type_is_a(dlna_mime_type, "video/*")) {
			flags |= GUPNP_DLNA_FLAGS_STREAMING_TRANSFER_MODE;
		} else {
			DLEYNA_LOG_WARNING("Unsupported Mime Type: %s",
					   dlna_mime_type);

			goto on_error;
		}

		g_string_append_printf(header, "DLNA.ORG_FLAGS=%.8x", flags);
		g_string_append_printf(header, "000000000000000000000000");
	} else {
		DLEYNA_LOG_WARNING("Unable to discover mime_type");
	}

on_error:

	if (*mime_type == NULL) {
		content_type = g_content_type_guess(filename, NULL, 0, NULL);

		if (content_type != NULL) {
			*mime_type = g_content_type_get_mime_type(content_type);

			if (*mime_type == NULL)
				*error = g_error_new(DLEYNA_SERVER_ERROR,
						     DLEYNA_ERROR_BAD_MIME,
						     "Unable to determine MIME Type for %s",
						     filename);

			g_free(content_type);
		} else {
			*error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_BAD_MIME,
					     "Unable to determine Content Type for %s",
					     filename);
		}
	}

	DLEYNA_LOG_DEBUG("contentFeatures.dlna.org: %s", header->str);

	g_object_unref(guesser);

	g_free(uri);

	if (*mime_type)
		*dlna_header = g_string_free(header, FALSE);
	else
		(void) g_string_free(header, TRUE);

	return;
}

static void prv_host_file_delete(gpointer host_file)
{
	dlr_host_file_t *hf = host_file;
	unsigned int i;

	if (hf) {
		g_free(hf->path);
		for (i = 0; i < hf->mapped_count; ++i)
			g_mapped_file_unref(hf->mapped_file);

		g_ptr_array_unref(hf->clients);

		g_free(hf->mime_type);
		g_free(hf->dlna_header);
		g_free(hf);
	}
}

static dlr_host_file_t *prv_host_file_new(const gchar *file, unsigned int id,
					  GError **error)
{
	dlr_host_file_t *hf = NULL;
	gchar *extension;

	if (!g_file_test(file, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS)) {
		*error = g_error_new(DLEYNA_SERVER_ERROR,
				     DLEYNA_ERROR_OBJECT_NOT_FOUND,
				     "File %s does not exist or is not a regular file",
				     file);
		goto on_error;
	}

	hf = g_new0(dlr_host_file_t, 1);
	hf->id = id;
	hf->clients = g_ptr_array_new_with_free_func(g_free);

	extension = strrchr(file, '.');
	hf->path = g_strdup_printf(DLR_HOST_SERVICE_ROOT"/%d%s",
				   hf->id, extension ? extension : "");

	prv_compute_mime_and_dlna_header(file, &hf->mime_type, &hf->dlna_header,
					 error);

	if (*error != NULL)
		goto on_error;

	return hf;

on_error:

	prv_host_file_delete(hf);

	return NULL;
}

static void prv_host_server_delete(gpointer host_server)
{
	dlr_host_server_t *server = host_server;

	if (server) {
		soup_server_quit(server->soup_server);
		g_object_unref(server->soup_server);
		g_hash_table_unref(server->files);
		g_free(server);
	}
}

static dlr_host_file_t *prv_host_server_find_file(dlr_host_server_t *hs,
						  const gchar *url,
						  const gchar **file_name)
{
	dlr_host_file_t *retval = NULL;
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_hash_table_iter_init(&iter, hs->files);

	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (!strcmp(((dlr_host_file_t *)value)->path, url)) {
			retval = value;
			*file_name = key;
			break;
		}
	}

	return retval;
}

static void prv_soup_message_finished_cb(SoupMessage *msg, gpointer user_data)
{
	dlr_host_file_t *hf = user_data;

	if (hf->mapped_count > 0) {
		g_mapped_file_unref(hf->mapped_file);
		--hf->mapped_count;

		if (hf->mapped_count == 0)
			hf->mapped_file = NULL;
	}
}

static void prv_soup_server_cb(SoupServer *server, SoupMessage *msg,
			       const char *path, GHashTable *query,
			       SoupClientContext *client, gpointer user_data)
{
	dlr_host_file_t *hf;
	dlr_host_server_t *hs = user_data;
	const gchar *file_name;
	const char *hdr;

	if ((msg->method != SOUP_METHOD_GET) &&
	    (msg->method != SOUP_METHOD_HEAD)) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
		goto on_error;
	}

	hf = prv_host_server_find_file(hs, path, &file_name);

	if (!hf) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		goto on_error;
	}

	hdr = soup_message_headers_get_one(msg->request_headers,
					   "getContentFeatures.dlna.org");

	if (hdr) {
		if (strcmp(hdr, "1") != 0) {
			soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
			goto on_error;
		}

		if ((hf->dlna_header) && strlen(hf->dlna_header) > 0)
			soup_message_headers_append(msg->response_headers,
						    "contentFeatures.dlna.org",
						    hf->dlna_header);
	}

	if (hf->mapped_file) {
		g_mapped_file_ref(hf->mapped_file);
		++hf->mapped_count;
	} else {
		hf->mapped_file = g_mapped_file_new(file_name,
						    FALSE,
						    NULL);

		if (!hf->mapped_file) {
			soup_message_set_status(msg,
						SOUP_STATUS_NOT_FOUND);
			goto on_error;
		}

		hf->mapped_count = 1;
	}

	if (msg->method == SOUP_METHOD_GET) {
		g_signal_connect(msg, "finished",
				 G_CALLBACK(prv_soup_message_finished_cb), hf);

		soup_message_set_response(
				msg, hf->mime_type,
				SOUP_MEMORY_STATIC,
				g_mapped_file_get_contents(hf->mapped_file),
				g_mapped_file_get_length(hf->mapped_file));
	} else {
		soup_message_headers_set_content_type(msg->response_headers,
						      hf->mime_type, NULL);

		soup_message_headers_set_content_length(
			msg->response_headers,
			g_mapped_file_get_length(hf->mapped_file));
	}

	soup_message_set_status(msg, SOUP_STATUS_OK);

on_error:

	return;
}

static dlr_host_server_t *prv_host_server_new(const gchar *device_if,
					      GError **error)
{
	dlr_host_server_t *server = NULL;
	SoupAddress *addr;

	addr = soup_address_new(device_if, SOUP_ADDRESS_ANY_PORT);

	if (soup_address_resolve_sync(addr, NULL) != SOUP_STATUS_OK) {
		*error = g_error_new(DLEYNA_SERVER_ERROR,
				     DLEYNA_ERROR_HOST_FAILED,
				     "Unable to create host server on %s",
				     device_if);
		goto on_error;
	}

	server = g_new(dlr_host_server_t, 1);
	server->files = g_hash_table_new_full(g_str_hash, g_str_equal,
					      g_free, prv_host_file_delete);

	server->soup_server = soup_server_new(SOUP_SERVER_INTERFACE, addr,
					      NULL);
	soup_server_add_handler(server->soup_server, DLR_HOST_SERVICE_ROOT,
				prv_soup_server_cb, server, NULL);
	soup_server_run_async(server->soup_server);
	server->counter = 0;

on_error:

	g_object_unref(addr);

	return server;
}

void dlr_host_service_new(dlr_host_service_t **host_service)
{
	dlr_host_service_t *hs;

	hs = g_new(dlr_host_service_t, 1);
	hs->servers = g_hash_table_new_full(g_str_hash, g_str_equal,
					    g_free, prv_host_server_delete);

	*host_service = hs;
}

static gchar *prv_add_new_file(dlr_host_server_t *server, const gchar *client,
			       const gchar *device_if, const gchar *file,
			       GError **error)
{
	unsigned int i;
	dlr_host_file_t *hf;
	gchar *str;

	hf = g_hash_table_lookup(server->files, file);

	if (!hf) {
		hf = prv_host_file_new(file, server->counter++, error);

		if (!hf)
			goto on_error;

		g_ptr_array_add(hf->clients, g_strdup(client));
		g_hash_table_insert(server->files, g_strdup(file), hf);
	} else {
		for (i = 0; i < hf->clients->len; ++i)
			if (!strcmp(g_ptr_array_index(hf->clients, i), client))
				break;

		if (i == hf->clients->len)
			g_ptr_array_add(hf->clients, g_strdup(client));
	}

	str = g_strdup_printf("http://%s:%d%s", device_if,
			      soup_server_get_port(server->soup_server),
			      hf->path);

	return str;

on_error:

	return NULL;
}

gchar *dlr_host_service_add(dlr_host_service_t *host_service,
			    const gchar *device_if, const gchar *client,
			    const gchar *file, GError **error)
{
	dlr_host_server_t *server;
	gchar *retval = NULL;

	server = g_hash_table_lookup(host_service->servers, device_if);

	if (!server) {
		server = prv_host_server_new(device_if, error);

		if (!server)
			goto on_error;

		g_hash_table_insert(host_service->servers, g_strdup(device_if),
				    server);
	}

	retval = prv_add_new_file(server, client, device_if, file, error);

on_error:

	return retval;
}

static gboolean prv_remove_client(dlr_host_service_t *host_service,
				  const gchar *client,
				  dlr_host_server_t *server,
				  const gchar *device_if,
				  const gchar *file,
				  dlr_host_file_t *hf)
{
	unsigned int i;
	gboolean retval = FALSE;

	for (i = 0; i < hf->clients->len; ++i)
		if (!strcmp(g_ptr_array_index(hf->clients, i), client))
			break;

	if (i == hf->clients->len)
		goto on_error;

	g_ptr_array_remove_index(hf->clients, i);

	retval = TRUE;

on_error:

	return retval;
}

gboolean dlr_host_service_remove(dlr_host_service_t *host_service,
				 const gchar *device_if, const gchar *client,
				 const gchar *file)
{
	gboolean retval = FALSE;
	dlr_host_file_t *hf;
	dlr_host_server_t *server;

	server = g_hash_table_lookup(host_service->servers, device_if);

	if (!server)
		goto on_error;

	hf = g_hash_table_lookup(server->files, file);

	if (!hf)
		goto on_error;

	retval = prv_remove_client(host_service, client, server,
				   device_if, file, hf);
	if (!retval)
		goto on_error;

	if (hf->clients->len == 0)
		g_hash_table_remove(server->files, file);

	if (g_hash_table_size(server->files) == 0)
		g_hash_table_remove(host_service->servers, device_if);

on_error:

	return retval;
}

void dlr_host_service_lost_client(dlr_host_service_t *host_service,
				  const gchar *client)
{
	GHashTableIter iter;
	GHashTableIter iter2;
	gpointer value;
	gpointer key;
	gpointer value2;
	gpointer key2;
	dlr_host_server_t *server;
	dlr_host_file_t *hf;

	g_hash_table_iter_init(&iter, host_service->servers);

	while (g_hash_table_iter_next(&iter, &key, &value)) {
		server = value;
		g_hash_table_iter_init(&iter2, server->files);

		while (g_hash_table_iter_next(&iter2, &key2, &value2)) {
			hf = value2;

			if (!prv_remove_client(host_service, client, server,
					       key, key2, hf))
				continue;

			if (hf->clients->len > 0)
				continue;

			g_hash_table_iter_remove(&iter2);
		}

		if (g_hash_table_size(server->files) == 0)
			g_hash_table_iter_remove(&iter);
	}
}

void dlr_host_service_delete(dlr_host_service_t *host_service)
{
	if (host_service) {
		g_hash_table_unref(host_service->servers);
		g_free(host_service);
	}
}
