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

#include <config.h>
#include <glib.h>
#include <string.h>
#include "sigrok-cli.h"

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
		/* Caller specified driver. Use it. Only this one. */
		if (!parse_driver(opt_drv, &driver, &drvopts))
			return NULL;
		devices = sr_driver_scan(driver, drvopts);
		g_slist_free_full(drvopts, (GDestroyNotify)free_drvopts);
	} else if (opt_dont_scan) {
		/* No -d choice, and -D "don't scan" requested. Do nothing. */
		devices = NULL;
	} else {
		/* No driver specified. Scan all available drivers. */
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

/**
 * Lookup a channel group from its name.
 *
 * Uses the previously stored option value to lookup a channel group.
 * Returns a reference to the channel group when the lookup succeeded,
 * or #NULL after lookup failure, or #NULL for the global channel group
 * (the device's global parameters). Emits an error message when the
 * lookup failed while a channel group's name was specified.
 *
 * @param[in] sdi Device instance.
 *
 * @returns The channel group, or #NULL for failed lookup.
 */
struct sr_channel_group *lookup_channel_group(struct sr_dev_inst *sdi)
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
		if (g_ascii_strcasecmp(opt_channel_group, cg->name) != 0)
			continue;
		return cg;
	}
	g_critical("Invalid channel group '%s'", opt_channel_group);

	return NULL;
}
