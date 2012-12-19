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
#include <libsigrok/libsigrok.h>
#include "sigrok-cli.h"

struct sr_probe *find_probe(GSList *probelist, const char *probename)
{
	struct sr_probe *probe;
	GSList *l;

	probe = NULL;
	for (l = probelist; l; l = l->next) {
		probe = l->data;
		if (!strcmp(probe->name, probename))
			break;
	}
	probe = l ? l->data : NULL;

	return probe;
}

GSList *parse_probestring(struct sr_dev_inst *sdi, const char *probestring)
{
	struct sr_probe *probe;
	GSList *probelist;
	int ret, n, b, e, i;
	char **tokens, **range, **names, *eptr, str[8];

	if (!probestring || !probestring[0])
		/* All probes are enabled by default by the driver. */
		return NULL;

	ret = SR_OK;
	range = NULL;
	names = NULL;
	probelist = NULL;
	tokens = g_strsplit(probestring, ",", 0);
	for (i = 0; tokens[i]; i++) {
		if (tokens[i][0] == '\0') {
			g_critical("Invalid empty probe.");
			ret = SR_ERR;
			break;
		}
		if (strchr(tokens[i], '-')) {
			/* A range of probes in the form a-b. This will only work
			 * if the probes are named as numbers -- so every probe
			 * in the range must exist as a probe name string in the
			 * device. */
			range = g_strsplit(tokens[i], "-", 2);
			if (!range[0] || !range[1] || range[2]) {
				/* Need exactly two arguments. */
				g_critical("Invalid probe syntax '%s'.", tokens[i]);
				ret = SR_ERR;
				break;
			}

			b = strtol(range[0], &eptr, 10);
			if (eptr == range[0] || *eptr != '\0') {
				g_critical("Invalid probe '%s'.", range[0]);
				ret = SR_ERR;
				break;
			}
			e = strtol(range[1], NULL, 10);
			if (eptr == range[1] || *eptr != '\0') {
				g_critical("Invalid probe '%s'.", range[1]);
				ret = SR_ERR;
				break;
			}
			if (b < 0 || b >= e) {
				g_critical("Invalid probe range '%s'.", tokens[i]);
				ret = SR_ERR;
				break;
			}

			while (b <= e) {
				n = snprintf(str, 8, "%d", b);
				if (n < 0 || n > 8) {
					g_critical("Invalid probe '%d'.", b);
					ret = SR_ERR;
					break;
				}
				probe = find_probe(sdi->probes, str);
				if (!probe) {
					g_critical("unknown probe '%d'.", b);
					ret = SR_ERR;
					break;
				}
				probelist = g_slist_append(probelist, probe);
				b++;
			}
			if (ret != SR_OK)
				break;
		} else {
			names = g_strsplit(tokens[i], "=", 2);
			if (!names[0] || (names[1] && names[2])) {
				/* Need one or two arguments. */
				g_critical("Invalid probe '%s'.", tokens[i]);
				ret = SR_ERR;
				break;
			}

			probe = find_probe(sdi->probes, names[0]);
			if (!probe) {
				g_critical("unknown probe '%s'.", names[0]);
				ret = SR_ERR;
				break;
			}
			if (names[1]) {
				/* Rename probe. */
				g_free(probe->name);
				probe->name = g_strdup(names[1]);
			}
			probelist = g_slist_append(probelist, probe);
		}
	}
	if (range)
		g_strfreev(range);

	if (names)
		g_strfreev(names);

	if (ret != SR_OK) {
		g_slist_free(probelist);
		probelist = NULL;
	}

	g_strfreev(tokens);

	return probelist;
}

GHashTable *parse_generic_arg(const char *arg, gboolean sep_first)
{
	GHashTable *hash;
	int i;
	char **elements, *e;

	if (!arg || !arg[0])
		return NULL;

	i = 0;
	hash = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, g_free);
	elements = g_strsplit(arg, ":", 0);
	if (sep_first)
		g_hash_table_insert(hash, g_strdup("sigrok_key"),
				g_strdup(elements[i++]));
	for (; elements[i]; i++) {
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

char *strcanon(const char *str)
{
	int p0, p1;
	char *s;

	/* Returns newly allocated string. */
	s = g_ascii_strdown(str, -1);
	for (p0 = p1 = 0; str[p0]; p0++) {
		if ((s[p0] >= 'a' && s[p0] <= 'z')
				|| (s[p0] >= '0' && s[p0] <= '9'))
			s[p1++] = s[p0];
	}
	s[p1] = '\0';

	return s;
}

int canon_cmp(const char *str1, const char *str2)
{
	int ret;
	char *s1, *s2;

	s1 = strcanon(str1);
	s2 = strcanon(str2);
	ret = g_ascii_strcasecmp(s1, s2);
	g_free(s2);
	g_free(s1);

	return ret;
}
