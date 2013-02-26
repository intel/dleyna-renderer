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

#ifndef DLR_HOST_SERVICE_H__
#define DLR_HOST_SERVICE_H__

typedef struct dlr_host_service_t_ dlr_host_service_t;

void dlr_host_service_new(dlr_host_service_t **host_service);

gchar *dlr_host_service_add(dlr_host_service_t *host_service,
			    const gchar *device_if, const gchar *client,
			    const gchar *file, GError **error);

gboolean dlr_host_service_remove(dlr_host_service_t *host_service,
				 const gchar *device_if, const gchar *client,
				 const gchar *file);

void dlr_host_service_lost_client(dlr_host_service_t *host_service,
				  const gchar *client);

void dlr_host_service_delete(dlr_host_service_t *host_service);

#endif /*DLR_HOST_SERVICE_H__ */
