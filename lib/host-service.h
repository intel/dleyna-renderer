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

#ifndef RSU_HOST_SERVICE_H__
#define RSU_HOST_SERVICE_H__

typedef struct rsu_host_service_t_ rsu_host_service_t;

void rsu_host_service_new(rsu_host_service_t **host_service);
gchar *rsu_host_service_add(rsu_host_service_t *host_service,
			    const gchar *device_if, const gchar *client,
			    const gchar *file, GError **error);
gboolean rsu_host_service_remove(rsu_host_service_t *host_service,
				 const gchar *device_if, const gchar *client,
				 const gchar *file);
void rsu_host_service_lost_client(rsu_host_service_t *host_service,
				  const gchar *client);
void rsu_host_service_delete(rsu_host_service_t *host_service);

#endif
