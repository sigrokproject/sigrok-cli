/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <string.h>
#include "sigrok-cli.h"
#include "config.h"

static void free_drvopts(struct sr_config *src)
{
	g_variant_unref(src->data);
	g_free(src);
}

GSList *device_scan(void)
{
	struct sr_dev_driver **drivers, *driver;
	GSList *drvopts, *devices, *tmpdevs, *l;
	int i;

	if (opt_drv) {
		if (!parse_driver(opt_drv, &driver, &drvopts))
			return NULL;
		devices = sr_driver_scan(driver, drvopts);
		g_slist_free_full(drvopts, (GDestroyNotify)free_drvopts);
	} else {
		/* No driver specified, let them all scan on their own. */
		devices = NULL;
		drivers = sr_driver_list(sr_ctx);
		for (i = 0; drivers[i]; i++) {
			driver = drivers[i];
			if (sr_driver_init(sr_ctx, driver) != SR_OK) {
				g_critical("Failed to initialize driver.");
				return NULL;
			}
			tmpdevs = sr_driver_scan(driver, NULL);
			for (l = tmpdevs; l; l = l->next)
				devices = g_slist_append(devices, l->data);
			g_slist_free(tmpdevs);
		}
	}

	return devices;
}

struct sr_channel_group *select_channel_group(struct sr_dev_inst *sdi)
{
	struct sr_channel_group *cg;
	GSList *l, *channel_groups;

	if (!opt_channel_group)
		return NULL;

	channel_groups = sr_dev_inst_channel_groups_get(sdi);

	if (!channel_groups) {
		g_critical("This device does not have any channel groups.");
		return NULL;
	}

	for (l = channel_groups; l; l = l->next) {
		cg = l->data;
		if (!strcasecmp(opt_channel_group, cg->name)) {
			return cg;
		}
	}
	g_critical("Invalid channel group '%s'", opt_channel_group);

	return NULL;
}
