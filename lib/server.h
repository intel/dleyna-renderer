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
 * Regis Merlino <regis.merlino@intel.com>
 *
 */

#ifndef DLR_SERVER_H__
#define DLR_SERVER_H__

#include <libdleyna/core/connector.h>
#include <libdleyna/core/task-processor.h>

#define DLR_RENDERER_SINK "dleyna-renderer"

typedef struct dlr_device_t_ dlr_device_t;
typedef struct dlr_upnp_t_ dlr_upnp_t;

dlr_upnp_t *dlr_renderer_service_get_upnp(void);

dleyna_task_processor_t *dlr_renderer_service_get_task_processor(void);

const dleyna_connector_t *dlr_renderer_get_connector(void);

#endif /* DLR_SERVER_H__ */
