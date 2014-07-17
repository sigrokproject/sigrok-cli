/*
 * This file is part of the sigrok-cli project.
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

#include "sigrok-cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glib.h>

struct sr_channel *find_channel(GSList *channellist, const char *channelname)
{
	struct sr_channel *ch;
	GSList *l;

	ch = NULL;
	for (l = channellist; l; l = l->next) {
		ch = l->data;
		if (!strcmp(ch->name, channelname))
			break;
	}
	ch = l ? l->data : NULL;

	return ch;
}

GSList *parse_channelstring(struct sr_dev_inst *sdi, const char *channelstring)
{
	struct sr_channel *ch;
	GSList *channellist;
	int ret, n, b, e, i;
	char **tokens, **range, **names, *eptr, str[8];

	if (!channelstring || !channelstring[0])
		/* Use all channels by default. */
		return g_slist_copy(sdi->channels);

	ret = SR_OK;
	range = NULL;
	names = NULL;
	channellist = NULL;
	tokens = g_strsplit(channelstring, ",", 0);
	for (i = 0; tokens[i]; i++) {
		if (tokens[i][0] == '\0') {
			g_critical("Invalid empty channel.");
			ret = SR_ERR;
			break;
		}
		if (strchr(tokens[i], '-')) {
			/* A range of channels in the form a-b. This will only work
			 * if the channels are named as numbers -- so every channel
			 * in the range must exist as a channel name string in the
			 * device. */
			range = g_strsplit(tokens[i], "-", 2);
			if (!range[0] || !range[1] || range[2]) {
				/* Need exactly two arguments. */
				g_critical("Invalid channel syntax '%s'.", tokens[i]);
				ret = SR_ERR;
				goto range_fail;
			}

			b = strtol(range[0], &eptr, 10);
			if (eptr == range[0] || *eptr != '\0') {
				g_critical("Invalid channel '%s'.", range[0]);
				ret = SR_ERR;
				goto range_fail;
			}
			e = strtol(range[1], NULL, 10);
			if (eptr == range[1] || *eptr != '\0') {
				g_critical("Invalid channel '%s'.", range[1]);
				ret = SR_ERR;
				goto range_fail;
			}
			if (b < 0 || b >= e) {
				g_critical("Invalid channel range '%s'.", tokens[i]);
				ret = SR_ERR;
				goto range_fail;
			}

			while (b <= e) {
				n = snprintf(str, 8, "%d", b);
				if (n < 0 || n > 8) {
					g_critical("Invalid channel '%d'.", b);
					ret = SR_ERR;
					break;
				}
				ch = find_channel(sdi->channels, str);
				if (!ch) {
					g_critical("unknown channel '%d'.", b);
					ret = SR_ERR;
					break;
				}
				channellist = g_slist_append(channellist, ch);
				b++;
			}
range_fail:
			if (range)
				g_strfreev(range);

			if (ret != SR_OK)
				break;
		} else {
			names = g_strsplit(tokens[i], "=", 2);
			if (!names[0] || (names[1] && names[2])) {
				/* Need one or two arguments. */
				g_critical("Invalid channel '%s'.", tokens[i]);
				g_strfreev(names);
				ret = SR_ERR;
				break;
			}

			ch = find_channel(sdi->channels, names[0]);
			if (!ch) {
				g_critical("unknown channel '%s'.", names[0]);
				g_strfreev(names);
				ret = SR_ERR;
				break;
			}
			if (names[1]) {
				/* Rename channel. */
				g_free(ch->name);
				ch->name = g_strdup(names[1]);
			}
			channellist = g_slist_append(channellist, ch);

			g_strfreev(names);
		}
	}

	if (ret != SR_OK) {
		g_slist_free(channellist);
		channellist = NULL;
	}

	g_strfreev(tokens);

	return channellist;
}

int parse_trigger_match(char c)
{
	int match;

	if (c == '0')
		match = SR_TRIGGER_ZERO;
	else if (c == '1')
		match = SR_TRIGGER_ONE;
	else if (c == 'r')
		match = SR_TRIGGER_RISING;
	else if (c == 'f')
		match = SR_TRIGGER_FALLING;
	else if (c == 'e')
		match = SR_TRIGGER_EDGE;
	else if (c == 'o')
		match = SR_TRIGGER_OVER;
	else if (c == 'u')
		match = SR_TRIGGER_UNDER;
	else
		match = 0;

	return match;
}

int parse_triggerstring(const struct sr_dev_inst *sdi, const char *s,
		struct sr_trigger **trigger)
{
	struct sr_channel *ch;
	struct sr_trigger_stage *stage;
	GVariant *gvar;
	GSList *l;
	gsize num_matches;
	gboolean found_match, error;
	const int32_t *matches;
	int32_t match;
	unsigned int j;
	int t, i;
	char **tokens, *sep;

	if (sr_config_list(sdi->driver, sdi, NULL, SR_CONF_TRIGGER_MATCH,
			&gvar) != SR_OK) {
		g_critical("Device doesn't support any triggers.");
		return FALSE;
	}
	matches = g_variant_get_fixed_array(gvar, &num_matches, sizeof(int32_t));

	*trigger = sr_trigger_new(NULL);
	error = FALSE;
	tokens = g_strsplit(s, ",", -1);
	for (i = 0; tokens[i]; i++) {
		if (!(sep = strchr(tokens[i], '='))) {
			g_critical("Invalid trigger '%s'.", tokens[i]);
			error = TRUE;
			break;
		}
		*sep++ = 0;
		ch = NULL;
		for (l = sdi->channels; l; l = l->next) {
			ch = l->data;
			if (ch->enabled && !strcmp(ch->name, tokens[i]))
				break;
			ch = NULL;
		}
		if (!ch) {
			g_critical("Invalid channel '%s'.", tokens[i]);
			error = TRUE;
			break;
		}
		for (t = 0; sep[t]; t++) {
			if (!(match = parse_trigger_match(sep[t]))) {
				g_critical("Invalid trigger match '%c'.", sep[t]);
				error = TRUE;
				break;
			}
			found_match = FALSE;
			for (j = 0; j < num_matches; j++) {
				if (matches[j] == match) {
					found_match = TRUE;
					break;
				}
			}
			if (!found_match) {
				g_critical("Trigger match '%c' not supported by device.", sep[t]);
				error = TRUE;
				break;
			}
			/* Make sure this ends up in the right stage, creating
			 * them as needed. */
			while (!(stage = g_slist_nth_data((*trigger)->stages, t)))
				sr_trigger_stage_add(*trigger);
			if (sr_trigger_match_add(stage, ch, match, 0) != SR_OK) {
				error = TRUE;
				break;
			}
		}
	}
	g_strfreev(tokens);
	g_variant_unref(gvar);

	if (error)
		sr_trigger_free(*trigger);

	return !error;
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

static char *strcanon(const char *str)
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
