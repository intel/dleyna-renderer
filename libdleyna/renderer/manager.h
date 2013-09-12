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

#ifndef DLR_MANAGER_H__
#define DLR_MANAGER_H__

#include <libdleyna/core/connector.h>
#include <libdleyna/core/settings.h>
#include <libgupnp/gupnp-context-manager.h>

#include "task.h"

typedef struct dlr_manager_t_ dlr_manager_t;
typedef void (*dlr_manager_task_complete_t)(dlr_task_t *task, GError *error);

dlr_manager_t *dlr_manager_new(dleyna_connector_id_t connection,
			       GUPnPContextManager *connection_manager);

void dlr_manager_delete(dlr_manager_t *manager);

dleyna_white_list_t *dlr_manager_get_white_list(dlr_manager_t *manager);

void dlr_manager_get_all_props(dlr_manager_t *manager,
			       dleyna_settings_t *settings,
			       dlr_task_t *task,
			       dlr_manager_task_complete_t cb);

void dlr_manager_get_prop(dlr_manager_t *manager,
			  dleyna_settings_t *settings,
			  dlr_task_t *task,
			  dlr_manager_task_complete_t cb);

void dlr_manager_set_prop(dlr_manager_t *manager,
			  dleyna_settings_t *settings,
			  dlr_task_t *task,
			  dlr_manager_task_complete_t cb);

#endif /* DLR_MANAGER_H__ */
