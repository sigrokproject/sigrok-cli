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
#include <stdlib.h>
#include <glib.h>

struct sr_context *sr_ctx = NULL;
#ifdef HAVE_SRD
struct srd_session *srd_sess = NULL;
#endif

static gboolean opt_version = FALSE;
gint opt_loglevel = SR_LOG_WARN; /* Show errors+warnings by default. */
static gboolean opt_scan_devs = FALSE;
gboolean opt_wait_trigger = FALSE;
gchar *opt_input_file = NULL;
gchar *opt_output_file = NULL;
gchar *opt_drv = NULL;
gchar *opt_config = NULL;
static gchar *opt_channels = NULL;
gchar *opt_channel_group = NULL;
gchar *opt_triggers = NULL;
gchar *opt_pds = NULL;
#ifdef HAVE_SRD
static gchar *opt_pd_stack = NULL;
static gchar *opt_pd_annotations = NULL;
static gchar *opt_pd_meta = NULL;
static gchar *opt_pd_binary = NULL;
#endif
gchar *opt_input_format = NULL;
gchar *opt_output_format = NULL;
static gchar *opt_show = NULL;
gchar *opt_time = NULL;
gchar *opt_samples = NULL;
gchar *opt_frames = NULL;
gchar *opt_continuous = NULL;
static gchar *opt_set = NULL;

static GOptionEntry optargs[] = {
	{"version", 'V', 0, G_OPTION_ARG_NONE, &opt_version,
			"Show version and support list", NULL},
	{"loglevel", 'l', 0, G_OPTION_ARG_INT, &opt_loglevel,
			"Set loglevel (5 is most verbose)", NULL},
	{"driver", 'd', 0, G_OPTION_ARG_STRING, &opt_drv,
			"The driver to use", NULL},
	{"config", 'c', 0, G_OPTION_ARG_STRING, &opt_config,
			"Specify device configuration options", NULL},
	{"input-file", 'i', 0, G_OPTION_ARG_FILENAME, &opt_input_file,
			"Load input from file", NULL},
	{"input-format", 'I', 0, G_OPTION_ARG_STRING, &opt_input_format,
			"Input format", NULL},
	{"output-file", 'o', 0, G_OPTION_ARG_FILENAME, &opt_output_file,
			"Save output to file", NULL},
	{"output-format", 'O', 0, G_OPTION_ARG_STRING, &opt_output_format,
			"Output format", NULL},
	{"channels", 'C', 0, G_OPTION_ARG_STRING, &opt_channels,
			"Channels to use", NULL},
	{"channel-group", 'g', 0, G_OPTION_ARG_STRING, &opt_channel_group,
			"Channel groups", NULL},
	{"triggers", 't', 0, G_OPTION_ARG_STRING, &opt_triggers,
			"Trigger configuration", NULL},
	{"wait-trigger", 'w', 0, G_OPTION_ARG_NONE, &opt_wait_trigger,
			"Wait for trigger", NULL},
#ifdef HAVE_SRD
	{"protocol-decoders", 'P', 0, G_OPTION_ARG_STRING, &opt_pds,
			"Protocol decoders to run", NULL},
	{"protocol-decoder-stack", 'S', 0, G_OPTION_ARG_STRING, &opt_pd_stack,
			"Protocol decoder stack", NULL},
	{"protocol-decoder-annotations", 'A', 0, G_OPTION_ARG_STRING, &opt_pd_annotations,
			"Protocol decoder annotation(s) to show", NULL},
	{"protocol-decoder-meta", 'M', 0, G_OPTION_ARG_STRING, &opt_pd_meta,
			"Protocol decoder meta output to show", NULL},
	{"protocol-decoder-binary", 'B', 0, G_OPTION_ARG_STRING, &opt_pd_binary,
			"Protocol decoder binary output to show", NULL},
#endif
	{"scan", 0, 0, G_OPTION_ARG_NONE, &opt_scan_devs,
			"Scan for devices", NULL},
	{"show", 0, 0, G_OPTION_ARG_NONE, &opt_show,
			"Show device detail", NULL},
	{"time", 0, 0, G_OPTION_ARG_STRING, &opt_time,
			"How long to sample (ms)", NULL},
	{"samples", 0, 0, G_OPTION_ARG_STRING, &opt_samples,
			"Number of samples to acquire", NULL},
	{"frames", 0, 0, G_OPTION_ARG_STRING, &opt_frames,
			"Number of frames to acquire", NULL},
	{"continuous", 0, 0, G_OPTION_ARG_NONE, &opt_continuous,
			"Sample continuously", NULL},
	{"set", 0, 0, G_OPTION_ARG_NONE, &opt_set, "Set device options only", NULL},
	{NULL, 0, 0, 0, NULL, NULL, NULL}
};


static void logger(const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer cb_data)
{
	(void)log_domain;
	(void)cb_data;

	/*
	 * All messages, warnings, errors etc. go to stderr (not stdout) in
	 * order to not mess up the CLI tool data output, e.g. VCD output.
	 */
	if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)
			|| opt_loglevel > SR_LOG_WARN) {
		fprintf(stderr, "%s\n", message);
		fflush(stderr);
	}

	if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL))
		exit(1);

}

int select_channels(struct sr_dev_inst *sdi)
{
	struct sr_channel *ch;
	GSList *selected_channels, *l;

	if (opt_channels) {
		if (!(selected_channels = parse_channelstring(sdi, opt_channels)))
			return SR_ERR;

		for (l = sdi->channels; l; l = l->next) {
			ch = l->data;
			if (g_slist_find(selected_channels, ch))
				ch->enabled = TRUE;
			else
				ch->enabled = FALSE;
		}
		g_slist_free(selected_channels);
	}
#ifdef HAVE_SRD
	map_pd_channels(sdi);
#endif
	return SR_OK;
}

static void set_options(void)
{
	struct sr_dev_inst *sdi;
	GSList *devices;
	GHashTable *devargs;

	if (!opt_config) {
		g_critical("No setting specified.");
		return;
	}

	if (!(devargs = parse_generic_arg(opt_config, FALSE)))
		return;

	if (!(devices = device_scan())) {
		g_critical("No devices found.");
		return;
	}
	sdi = devices->data;

	if (sr_dev_open(sdi) != SR_OK) {
		g_critical("Failed to open device.");
		return;
	}

	set_dev_options(sdi, devargs);

	sr_dev_close(sdi);
	g_slist_free(devices);
	g_hash_table_destroy(devargs);

}

int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error;
	int ret;
	char *help;

	g_log_set_default_handler(logger, NULL);

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, optargs, NULL);

	ret = 1;
	error = NULL;
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_critical("%s", error->message);
		goto done;
	}

	/* Set the loglevel (amount of messages to output) for libsigrok. */
	if (sr_log_loglevel_set(opt_loglevel) != SR_OK)
		goto done;

	if (sr_init(&sr_ctx) != SR_OK)
		goto done;

#ifdef HAVE_SRD
	/* Set the loglevel (amount of messages to output) for libsigrokdecode. */
	if (srd_log_loglevel_set(opt_loglevel) != SRD_OK)
		goto done;

	if (opt_pds) {
		if (srd_init(NULL) != SRD_OK)
			goto done;
		if (srd_session_new(&srd_sess) != SRD_OK) {
			g_critical("Failed to create new decode session.");
			goto done;
		}
		if (register_pds(opt_pds, opt_pd_annotations) != 0)
			goto done;
		if (setup_pd_stack(opt_pds, opt_pd_stack, opt_pd_annotations) != 0)
			goto done;

		/* Only one output type is ever shown. */
		if (opt_pd_binary) {
			if (setup_pd_binary(opt_pd_binary) != 0)
				goto done;
			if (srd_pd_output_callback_add(srd_sess, SRD_OUTPUT_BINARY,
					show_pd_binary, NULL) != SRD_OK)
				goto done;
		} else if (opt_pd_meta) {
			if (setup_pd_meta(opt_pd_meta) != 0)
				goto done;
			if (srd_pd_output_callback_add(srd_sess, SRD_OUTPUT_META,
					show_pd_meta, NULL) != SRD_OK)
				goto done;
		} else {
			if (opt_pd_annotations)
				if (setup_pd_annotations(opt_pd_annotations) != 0)
					goto done;
			if (srd_pd_output_callback_add(srd_sess, SRD_OUTPUT_ANN,
					show_pd_annotations, NULL) != SRD_OK)
				goto done;
		}
	}
#endif

	if (opt_version)
		show_version();
	else if (opt_scan_devs)
		show_dev_list();
#ifdef HAVE_SRD
	else if (opt_pds && opt_show)
		show_pd_detail();
#endif
	else if (opt_show)
		show_dev_detail();
	else if (opt_input_file)
		load_input_file();
	else if (opt_set)
		set_options();
	else if (opt_samples || opt_time || opt_frames || opt_continuous)
		run_session();
	else {
		help = g_option_context_get_help(context, TRUE, NULL);
		printf("%s", help);
		g_free(help);
	}

#ifdef HAVE_SRD
	if (opt_pds)
		srd_exit();
#endif

	ret = 0;

done:
	if (sr_ctx)
		sr_exit(sr_ctx);

	g_option_context_free(context);

	return ret;
}
