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
#include "sigrok-cli.h"

gboolean opt_version = FALSE;
gint opt_loglevel = SR_LOG_WARN; /* Show errors+warnings by default. */
gboolean opt_scan_devs = FALSE;
gboolean opt_wait_trigger = FALSE;
gchar *opt_input_file = NULL;
gchar *opt_output_file = NULL;
gchar *opt_drv = NULL;
gchar *opt_config = NULL;
gchar *opt_channels = NULL;
gchar *opt_channel_group = NULL;
gchar *opt_triggers = NULL;
gchar **opt_pds = NULL;
#ifdef HAVE_SRD
gchar *opt_pd_annotations = NULL;
gchar *opt_pd_meta = NULL;
gchar *opt_pd_binary = NULL;
#endif
gchar *opt_input_format = NULL;
gchar *opt_output_format = NULL;
gchar *opt_transform_module = NULL;
gboolean opt_show = FALSE;
gchar *opt_time = NULL;
gchar *opt_samples = NULL;
gchar *opt_frames = NULL;
gboolean opt_continuous = FALSE;
gchar *opt_get = NULL;
gboolean opt_set = FALSE;

/*
 * Defines a callback function that generates an error if an
 * option occurs twice.
 */
#define CHECK_ONCE(option) \
static gboolean check_ ## option                                          \
	(const gchar *option_name, const gchar *value,                    \
	gpointer data, GError **error)                                    \
{                                                                         \
	(void)data;                                                       \
                                                                          \
	static gboolean seen = FALSE;                                     \
	if (seen) {                                                       \
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, \
		            "superfluous option \"%s\"", option_name);    \
		return FALSE;                                             \
	}                                                                 \
                                                                          \
	option = g_strdup(value);                                         \
	seen = TRUE;                                                      \
	return TRUE;                                                      \
}

CHECK_ONCE(opt_drv)
CHECK_ONCE(opt_config)
CHECK_ONCE(opt_input_format)
CHECK_ONCE(opt_output_format)
CHECK_ONCE(opt_transform_module)
CHECK_ONCE(opt_channels)
CHECK_ONCE(opt_channel_group)
CHECK_ONCE(opt_triggers)
#ifdef HAVE_SRD
CHECK_ONCE(opt_pd_annotations)
CHECK_ONCE(opt_pd_meta)
CHECK_ONCE(opt_pd_binary)
#endif
CHECK_ONCE(opt_time)
CHECK_ONCE(opt_samples)
CHECK_ONCE(opt_frames)
CHECK_ONCE(opt_get)

#undef CHECK_STR_ONCE

static gchar **input_file_array = NULL;
static gchar **output_file_array = NULL;

static const GOptionEntry optargs[] = {
	{"version", 'V', 0, G_OPTION_ARG_NONE, &opt_version,
			"Show version and support list", NULL},
	{"loglevel", 'l', 0, G_OPTION_ARG_INT, &opt_loglevel,
			"Set loglevel (5 is most verbose)", NULL},
	{"driver", 'd', 0, G_OPTION_ARG_CALLBACK, &check_opt_drv,
			"The driver to use", NULL},
	{"config", 'c', 0, G_OPTION_ARG_CALLBACK, &check_opt_config,
			"Specify device configuration options", NULL},
	{"input-file", 'i', 0, G_OPTION_ARG_FILENAME_ARRAY, &input_file_array,
			"Load input from file", NULL},
	{"input-format", 'I', 0, G_OPTION_ARG_CALLBACK, &check_opt_input_format,
			"Input format", NULL},
	{"output-file", 'o', 0, G_OPTION_ARG_FILENAME_ARRAY, &output_file_array,
			"Save output to file", NULL},
	{"output-format", 'O', 0, G_OPTION_ARG_CALLBACK, &check_opt_output_format,
			"Output format", NULL},
	{"transform-module", 'T', 0, G_OPTION_ARG_CALLBACK, &check_opt_transform_module,
			"Transform module", NULL},
	{"channels", 'C', 0, G_OPTION_ARG_CALLBACK, &check_opt_channels,
			"Channels to use", NULL},
	{"channel-group", 'g', 0, G_OPTION_ARG_CALLBACK, &check_opt_channel_group,
			"Channel groups", NULL},
	{"triggers", 't', 0, G_OPTION_ARG_CALLBACK, &check_opt_triggers,
			"Trigger configuration", NULL},
	{"wait-trigger", 'w', 0, G_OPTION_ARG_NONE, &opt_wait_trigger,
			"Wait for trigger", NULL},
#ifdef HAVE_SRD
	{"protocol-decoders", 'P', 0, G_OPTION_ARG_STRING_ARRAY, &opt_pds,
			"Protocol decoders to run", NULL},
	{"protocol-decoder-annotations", 'A', 0, G_OPTION_ARG_CALLBACK, &check_opt_pd_annotations,
			"Protocol decoder annotation(s) to show", NULL},
	{"protocol-decoder-meta", 'M', 0, G_OPTION_ARG_CALLBACK, &check_opt_pd_meta,
			"Protocol decoder meta output to show", NULL},
	{"protocol-decoder-binary", 'B', 0, G_OPTION_ARG_CALLBACK, &check_opt_pd_binary,
			"Protocol decoder binary output to show", NULL},
#endif
	{"scan", 0, 0, G_OPTION_ARG_NONE, &opt_scan_devs,
			"Scan for devices", NULL},
	{"show", 0, 0, G_OPTION_ARG_NONE, &opt_show,
			"Show device/format/decoder details", NULL},
	{"time", 0, 0, G_OPTION_ARG_CALLBACK, &check_opt_time,
			"How long to sample (ms)", NULL},
	{"samples", 0, 0, G_OPTION_ARG_CALLBACK, &check_opt_samples,
			"Number of samples to acquire", NULL},
	{"frames", 0, 0, G_OPTION_ARG_CALLBACK, &check_opt_frames,
			"Number of frames to acquire", NULL},
	{"continuous", 0, 0, G_OPTION_ARG_NONE, &opt_continuous,
			"Sample continuously", NULL},
	{"get", 0, 0, G_OPTION_ARG_CALLBACK, &check_opt_get, "Get device option only", NULL},
	{"set", 0, 0, G_OPTION_ARG_NONE, &opt_set, "Set device options only", NULL},
	{NULL, 0, 0, 0, NULL, NULL, NULL}
};

/*
 * Parses the command line and sets all the 'opt_...' variables.
 * Returns zero on success, non-zero otherwise.
 */
int parse_options(int argc, char **argv)
{
	GError *error = NULL;
	GOptionContext *context = g_option_context_new(NULL);
	int ret = 1;

	g_option_context_add_main_entries(context, optargs, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_critical("%s", error->message);
		goto done;
	}

	/*
	 * Because of encoding issues with filenames (mentioned in the glib
	 * documentation), we don't check them with a callback function, but
	 * collect them into arrays and then check if the arrays contain at
	 * most one element.
	 */
	if (NULL != input_file_array) {
		if (NULL != input_file_array[0] && NULL != input_file_array[1]) {
			g_critical("option \"--input-file/-i\" only allowed once");
			goto done;
		}
		opt_input_file = g_strdup(input_file_array[0]);
	}

	if (NULL != output_file_array) {
		if (NULL != output_file_array[0] && NULL != output_file_array[1]) {
			g_critical("option \"--output-file/-o\" only allowed once");
			goto done;
		}
		opt_output_file = g_strdup(output_file_array[0]);
	}

	if (1 != argc) {
		g_critical("superfluous command line argument \"%s\"", argv[1]);
		goto done;
	}

	ret = 0;

done:
	g_option_context_free(context);
	g_strfreev(input_file_array);
	g_strfreev(output_file_array);
	input_file_array = NULL;
	output_file_array = NULL;

	return ret;
}

void show_help(void)
{
	GOptionContext *context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, optargs, NULL);

	char *help = g_option_context_get_help(context, TRUE, NULL);
	printf("%s", help);
	g_free(help);

	g_option_context_free(context);
}
