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
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

extern gchar *opt_input_file;
extern gchar *opt_input_format;
extern gchar *opt_channels;


/**
 * Return the input file format which the CLI tool should use.
 *
 * If the user specified -I / --input-format, use that one. Otherwise, try to
 * autodetect the format as good as possible. Failing that, return NULL.
 *
 * @param filename The filename of the input file. Must not be NULL.
 * @param opt The -I / --input-file option the user specified (or NULL).
 *
 * @return A pointer to the 'struct sr_input_format' that should be used,
 *         or NULL if no input format was selected or auto-detected.
 */
static struct sr_input_format *determine_input_file_format(
			const char *filename, const char *opt)
{
	int i;
	struct sr_input_format **inputs;

	/* If there are no input formats, return NULL right away. */
	inputs = sr_input_list();
	if (!inputs) {
		g_critical("No supported input formats available.");
		return NULL;
	}

	/* If the user specified -I / --input-format, use that one. */
	if (opt) {
		for (i = 0; inputs[i]; i++) {
			if (strcasecmp(inputs[i]->id, opt))
				continue;
			g_debug("Using user-specified input file format '%s'.",
					inputs[i]->id);
			return inputs[i];
		}

		/* The user specified an unknown input format, return NULL. */
		g_critical("Error: specified input file format '%s' is "
			"unknown.", opt);
		return NULL;
	}

	/* Otherwise, try to find an input module that can handle this file. */
	for (i = 0; inputs[i]; i++) {
		if (inputs[i]->format_match(filename))
			break;
	}

	/* Return NULL if no input module wanted to touch this. */
	if (!inputs[i]) {
		g_critical("Error: no matching input module found.");
		return NULL;
	}

	g_debug("cli: Autodetected '%s' input format for file '%s'.",
		inputs[i]->id, filename);

	return inputs[i];
}

static void load_input_file_format(void)
{
	GHashTable *fmtargs = NULL;
	struct stat st;
	struct sr_input *in;
	struct sr_input_format *input_format;
	char *fmtspec = NULL;

	if (opt_input_format) {
		fmtargs = parse_generic_arg(opt_input_format, TRUE);
		fmtspec = g_hash_table_lookup(fmtargs, "sigrok_key");
	}

	if (!(input_format = determine_input_file_format(opt_input_file,
						   fmtspec))) {
		/* The exact cause was already logged. */
		return;
	}

	if (fmtargs)
		g_hash_table_remove(fmtargs, "sigrok_key");

	if (stat(opt_input_file, &st) == -1) {
		g_critical("Failed to load %s: %s", opt_input_file,
			strerror(errno));
		exit(1);
	}

	/* Initialize the input module. */
	if (!(in = g_try_malloc(sizeof(struct sr_input)))) {
		g_critical("Failed to allocate input module.");
		exit(1);
	}
	in->format = input_format;
	in->param = fmtargs;
	if (in->format->init) {
		if (in->format->init(in, opt_input_file) != SR_OK) {
			g_critical("Input format init failed.");
			exit(1);
		}
	}

	if (select_channels(in->sdi) != SR_OK)
		return;

	sr_session_new();
	sr_session_datafeed_callback_add(datafeed_in, NULL);
	if (sr_session_dev_add(in->sdi) != SR_OK) {
		g_critical("Failed to use device.");
		sr_session_destroy();
		return;
	}

	input_format->loadfile(in, opt_input_file);

	sr_session_destroy();

	if (fmtargs)
		g_hash_table_destroy(fmtargs);
}

void load_input_file(void)
{
	GSList *sessions;
	struct sr_dev_inst *sdi;
	int ret;

	if (sr_session_load(opt_input_file) == SR_OK) {
		/* sigrok session file */
		ret = sr_session_dev_list(&sessions);
		if (ret != SR_OK || !sessions->data) {
			g_critical("Failed to access session device.");
			sr_session_destroy();
			return;
		}
		sdi = sessions->data;
		if (select_channels(sdi) != SR_OK) {
			sr_session_destroy();
			return;
		}
		sr_session_datafeed_callback_add(datafeed_in, NULL);
		sr_session_start();
		sr_session_run();
		sr_session_stop();
	}
	else {
		/* fall back on input modules */
		load_input_file_format();
	}
}
