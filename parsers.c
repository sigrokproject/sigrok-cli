/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <sigrok.h>
#include "sigrok-cli.h"

char **parse_probestring(int max_probes, const char *probestring)
{
	int tmp, b, e, i;
	char **tokens, **range, **probelist, *name, str[8];
	gboolean error;

	error = FALSE;
	range = NULL;
	if (!(probelist = g_try_malloc0(max_probes * sizeof(char *)))) {
		/* TODO: Handle errors. */
	}
	tokens = g_strsplit(probestring, ",", max_probes);

	for (i = 0; tokens[i]; i++) {
		if (strchr(tokens[i], '-')) {
			/* A range of probes in the form 1-5. */
			range = g_strsplit(tokens[i], "-", 2);
			if (!range[0] || !range[1] || range[2]) {
				/* Need exactly two arguments. */
				g_critical("Invalid probe syntax '%s'.", tokens[i]);
				error = TRUE;
				break;
			}

			b = strtol(range[0], NULL, 10);
			e = strtol(range[1], NULL, 10);
			if (b < 1 || e > max_probes || b >= e) {
				g_critical("Invalid probe range '%s'.", tokens[i]);
				error = TRUE;
				break;
			}

			while (b <= e) {
				snprintf(str, 7, "%d", b);
				probelist[b - 1] = g_strdup(str);
				b++;
			}
		} else {
			tmp = strtol(tokens[i], NULL, 10);
			if (tmp < 1 || tmp > max_probes) {
				g_critical("Invalid probe %d.", tmp);
				error = TRUE;
				break;
			}

			if ((name = strchr(tokens[i], '='))) {
				probelist[tmp - 1] = g_strdup(++name);
				if (strlen(probelist[tmp - 1]) > SR_MAX_PROBENAME_LEN)
					probelist[tmp - 1][SR_MAX_PROBENAME_LEN] = 0;
			} else {
				snprintf(str, 7, "%d", tmp);
				probelist[tmp - 1] = g_strdup(str);
			}
		}
	}

	if (error) {
		for (i = 0; i < max_probes; i++)
			if (probelist[i])
				g_free(probelist[i]);
		g_free(probelist);
		probelist = NULL;
	}

	g_strfreev(tokens);
	if (range)
		g_strfreev(range);

	return probelist;
}

GHashTable *parse_generic_arg(const char *arg)
{
	GHashTable *hash;
	int i;
	char **elements, *e;

	if (!arg || !arg[0])
		return NULL;

	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	elements = g_strsplit(arg, ":", 0);
	g_hash_table_insert(hash, g_strdup("sigrok_key"), g_strdup(elements[0]));
	for (i = 1; elements[i]; i++) {
		e = strchr(elements[i], '=');
		if (!e)
			g_hash_table_insert(hash, g_strdup(elements[i]), NULL);
		else {
			*e++ = '\0';
			g_hash_table_insert(hash, g_strdup(elements[i]), g_strdup(e));
		}
	}
	g_strfreev(elements);

	return hash;
}

struct sr_dev *parse_devstring(const char *devstring)
{
	struct sr_dev *dev, *d;
	struct sr_dev_driver **drivers;
	GSList *devs, *l;
	int i, num_devs, dev_num, dev_cnt;
	char *tmp;

	if (!devstring)
		return NULL;

	dev = NULL;
	dev_num = strtol(devstring, &tmp, 10);
	if (tmp != devstring) {
		/* argument is numeric, meaning a device ID. Make all drivers
		 * scan for devices.
		 */
		num_devs = num_real_devs();
		if (dev_num < 0 || dev_num >= num_devs)
			return NULL;

		dev_cnt = 0;
		devs = sr_dev_list();
		for (l = devs; l; l = l->next) {
			d = l->data;
			if (sr_dev_has_hwcap(d, SR_HWCAP_DEMO_DEV))
				continue;
			if (dev_cnt == dev_num) {
				if (dev_num == dev_cnt) {
					dev = d;
					break;
				}
			}
			dev_cnt++;
		}
	} else {
		/* select device by driver -- only initialize that driver,
		 * no need to let them all scan
		 */
		dev = NULL;
		drivers = sr_driver_list();
		for (i = 0; drivers[i]; i++) {
			if (strcmp(drivers[i]->name, devstring))
				continue;
			num_devs = sr_driver_init(drivers[i]);
			if (num_devs == 1) {
				devs = sr_dev_list();
				dev = devs->data;
			} else if (num_devs > 1) {
				printf("driver '%s' found %d devices, select by ID instead.\n",
						devstring, num_devs);
			}
			/* fall through: selected driver found no devices */
			break;
		}
	}

	return dev;
}
