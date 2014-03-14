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

#include "sigrok-cli.h"
#include "config.h"
#include <glib.h>
#include <string.h>

extern struct sr_context *sr_ctx;
extern gchar *opt_drv;
extern gchar *opt_channel_group;

/* Convert driver options hash to GSList of struct sr_config. */
static GSList *hash_to_hwopt(GHashTable *hash)
{
	struct sr_config *src;
	GList *gl, *keys;
	GSList *opts;
	char *key;

	keys = g_hash_table_get_keys(hash);
	opts = NULL;
	for (gl = keys; gl; gl = gl->next) {
		key = gl->data;
		src = g_malloc(sizeof(struct sr_config));
		if (opt_to_gvar(key, g_hash_table_lookup(hash, key), src) != 0)
			return NULL;
		opts = g_slist_append(opts, src);
	}
	g_list_free(keys);

	return opts;
}

static void free_drvopts(struct sr_config *src)
{
	g_variant_unref(src->data);
	g_free(src);
}

GSList *device_scan(void)
{
	struct sr_dev_driver **drivers, *driver;
	GHashTable *drvargs;
	GSList *drvopts, *devices, *tmpdevs, *l;
	int i;
	char *drvname;

	if (opt_drv) {
		drvargs = parse_generic_arg(opt_drv, TRUE);
		drvname = g_strdup(g_hash_table_lookup(drvargs, "sigrok_key"));
		g_hash_table_remove(drvargs, "sigrok_key");
		driver = NULL;
		drivers = sr_driver_list();
		for (i = 0; drivers[i]; i++) {
			if (strcmp(drivers[i]->name, drvname))
				continue;
			driver = drivers[i];
		}
		if (!driver) {
			g_critical("Driver %s not found.", drvname);
			g_hash_table_destroy(drvargs);
			g_free(drvname);
			return NULL;
		}
		g_free(drvname);
		if (sr_driver_init(sr_ctx, driver) != SR_OK) {
			g_critical("Failed to initialize driver.");
			g_hash_table_destroy(drvargs);
			return NULL;
		}
		drvopts = NULL;
		if (g_hash_table_size(drvargs) > 0) {
			if (!(drvopts = hash_to_hwopt(drvargs))) {
				/* Unknown options, already logged. */
				g_hash_table_destroy(drvargs);
				return NULL;
			}
		}
		g_hash_table_destroy(drvargs);
		devices = sr_driver_scan(driver, drvopts);
		g_slist_free_full(drvopts, (GDestroyNotify)free_drvopts);
	} else {
		/* No driver specified, let them all scan on their own. */
		devices = NULL;
		drivers = sr_driver_list();
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
	GSList *l;

	if (!opt_channel_group)
		return NULL;

	if (!sdi->channel_groups) {
		g_critical("This device does not have any channel groups.");
		return NULL;
	}

	for (l = sdi->channel_groups; l; l = l->next) {
		cg = l->data;
		if (!strcasecmp(opt_channel_group, cg->name)) {
			return cg;
		}
	}

	return NULL;
}

