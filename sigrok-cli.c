/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <sigrokdecode.h> /* First, so we avoid a _POSIX_C_SOURCE warning. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sigrok.h>
#include "sigrok-cli.h"
#include "config.h"

#define DEFAULT_OUTPUT_FORMAT "bits:width=64"

extern struct sr_hwcap_option sr_hwcap_options[];

static gboolean debug = 0;
static uint64_t limit_samples = 0;
static struct sr_output_format *output_format = NULL;
static int default_output_format = FALSE;
static char *output_format_param = NULL;
static char *input_format_param = NULL;
static GData *pd_ann_visible = NULL;

static gboolean opt_version = FALSE;
static gint opt_loglevel = SR_LOG_WARN; /* Show errors+warnings per default. */
static gboolean opt_list_devices = FALSE;
static gboolean opt_wait_trigger = FALSE;
static gchar *opt_input_file = NULL;
static gchar *opt_output_file = NULL;
static gchar *opt_device = NULL;
static gchar *opt_probes = NULL;
static gchar *opt_triggers = NULL;
static gchar *opt_pds = NULL;
static gchar *opt_pd_stack = NULL;
static gchar *opt_input_format = NULL;
static gchar *opt_format = NULL;
static gchar *opt_time = NULL;
static gchar *opt_samples = NULL;
static gchar *opt_continuous = NULL;

static GOptionEntry optargs[] = {
	{"version", 'V', 0, G_OPTION_ARG_NONE, &opt_version, "Show version and support list", NULL},
	{"loglevel", 'l', 0, G_OPTION_ARG_INT, &opt_loglevel, "Select libsigrok loglevel", NULL},
	{"list-devices", 'D', 0, G_OPTION_ARG_NONE, &opt_list_devices, "List devices", NULL},
	{"input-file", 'i', 0, G_OPTION_ARG_FILENAME, &opt_input_file, "Load input from file", NULL},
	{"output-file", 'o', 0, G_OPTION_ARG_FILENAME, &opt_output_file, "Save output to file", NULL},
	{"device", 'd', 0, G_OPTION_ARG_STRING, &opt_device, "Use device ID", NULL},
	{"probes", 'p', 0, G_OPTION_ARG_STRING, &opt_probes, "Probes to use", NULL},
	{"triggers", 't', 0, G_OPTION_ARG_STRING, &opt_triggers, "Trigger configuration", NULL},
	{"wait-trigger", 'w', 0, G_OPTION_ARG_NONE, &opt_wait_trigger, "Wait for trigger", NULL},
	{"protocol-decoders", 'a', 0, G_OPTION_ARG_STRING, &opt_pds, "Protocol decoder sequence", NULL},
	{"protocol-decoder-stack", 's', 0, G_OPTION_ARG_STRING, &opt_pd_stack, "Protocol decoder stack", NULL},
	{"input-format", 'I', 0, G_OPTION_ARG_STRING, &opt_input_format, "Input format", NULL},
	{"format", 'f', 0, G_OPTION_ARG_STRING, &opt_format, "Output format", NULL},
	{"time", 0, 0, G_OPTION_ARG_STRING, &opt_time, "How long to sample (ms)", NULL},
	{"samples", 0, 0, G_OPTION_ARG_STRING, &opt_samples, "Number of samples to acquire", NULL},
	{"continuous", 0, 0, G_OPTION_ARG_NONE, &opt_continuous, "Sample continuously", NULL},
	{NULL, 0, 0, 0, NULL, NULL, NULL}
};

static void show_version(void)
{
	GSList *plugins, *p, *l;
	struct sr_device_plugin *plugin;
	struct sr_input_format **inputs;
	struct sr_output_format **outputs;
	struct srd_decoder *dec;
	int i;

	printf("sigrok-cli %s\n\n", VERSION);
	printf("Supported hardware drivers:\n");
	plugins = sr_list_hwplugins();
	for (p = plugins; p; p = p->next) {
		plugin = p->data;
		printf("  %-20s %s\n", plugin->name, plugin->longname);
	}
	printf("\n");

	printf("Supported input formats:\n");
	inputs = sr_input_list();
	for (i = 0; inputs[i]; i++)
		printf("  %-20s %s\n", inputs[i]->id, inputs[i]->description);
	printf("\n");

	printf("Supported output formats:\n");
	outputs = sr_output_list();
	for (i = 0; outputs[i]; i++)
		printf("  %-20s %s\n", outputs[i]->id, outputs[i]->description);
	printf("\n");

	/* TODO: Error handling. */
	srd_init();

	printf("Supported protocol decoders:\n");
	for (l = srd_list_decoders(); l; l = l->next) {
		dec = l->data;
		printf("  %-20s %s\n", dec->id, dec->longname);
	}
	printf("\n");

	srd_exit();
}

static void print_device_line(struct sr_device *device)
{
	struct sr_device_instance *sdi;

	sdi = device->plugin->get_device_info(device->plugin_index, SR_DI_INSTANCE);

	if (sdi->vendor && sdi->vendor[0])
		printf("%s ", sdi->vendor);
	if (sdi->model && sdi->model[0])
		printf("%s ", sdi->model);
	if (sdi->version && sdi->version[0])
		printf("%s ", sdi->version);
	if (device->probes)
		printf("with %d probes", g_slist_length(device->probes));
	printf("\n");
}

static void show_device_list(void)
{
	struct sr_device *device, *demo_device;
	GSList *devices, *l;
	int devcnt;

	devcnt = 0;
	devices = sr_device_list();

	if (g_slist_length(devices) == 0)
		return;

	printf("The following devices were found:\nID    Device\n");
	demo_device = NULL;
	for (l = devices; l; l = l->next) {
		device = l->data;
		if (strstr(device->plugin->name, "demo")) {
			demo_device = device;
			continue;
		}
		printf("%-3d   ", devcnt++);
		print_device_line(device);
	}
	if (demo_device) {
		printf("demo  ");
		print_device_line(demo_device);
	}
}

static void show_device_detail(void)
{
	struct sr_device *device;
	struct sr_hwcap_option *hwo;
	struct sr_samplerates *samplerates;
	int cap, *capabilities, i;
	char *s, *title, *charopts, **stropts;

	device = parse_devicestring(opt_device);
	if (!device) {
		printf("No such device. Use -D to list all devices.\n");
		return;
	}

	print_device_line(device);

	if ((charopts = (char *)device->plugin->get_device_info(
			device->plugin_index, SR_DI_TRIGGER_TYPES))) {
		printf("Supported triggers: ");
		while (*charopts) {
			printf("%c ", *charopts);
			charopts++;
		}
		printf("\n");
	}

	title = "Supported options:\n";
	capabilities = device->plugin->get_capabilities();
	for (cap = 0; capabilities[cap]; cap++) {
		if (!(hwo = sr_find_hwcap_option(capabilities[cap])))
			continue;

		if (title) {
			printf("%s", title);
			title = NULL;
		}

		if (hwo->capability == SR_HWCAP_PATTERN_MODE) {
			printf("    %s", hwo->shortname);
			stropts = (char **)device->plugin->get_device_info(
					device->plugin_index, SR_DI_PATTERNMODES);
			if (!stropts) {
				printf("\n");
				continue;
			}
			printf(" - supported modes:\n");
			for (i = 0; stropts[i]; i++)
				printf("      %s\n", stropts[i]);
		} else if (hwo->capability == SR_HWCAP_SAMPLERATE) {
			printf("    %s", hwo->shortname);
			/* Supported samplerates */
			samplerates = device->plugin->get_device_info(
				device->plugin_index, SR_DI_SAMPLERATES);
			if (!samplerates) {
				printf("\n");
				continue;
			}

			if (samplerates->step) {
				/* low */
				if (!(s = sr_samplerate_string(samplerates->low)))
					continue;
				printf(" (%s", s);
				free(s);
				/* high */
				if (!(s = sr_samplerate_string(samplerates->high)))
					continue;
				printf(" - %s", s);
				free(s);
				/* step */
				if (!(s = sr_samplerate_string(samplerates->step)))
					continue;
				printf(" in steps of %s)\n", s);
				free(s);
			} else {
				printf(" - supported samplerates:\n");
				for (i = 0; samplerates->list[i]; i++) {
					printf("      %7s\n", sr_samplerate_string(samplerates->list[i]));
				}
			}
		} else {
			printf("    %s\n", hwo->shortname);
		}
	}
}

static void show_pd_detail(void)
{
	GSList *l;
	struct srd_decoder *dec;
	char **pdtokens, **pdtok, **ann, *doc;

	pdtokens = g_strsplit(opt_pds, ",", -1);
	for (pdtok = pdtokens; *pdtok; pdtok++) {
		if (!(dec = srd_get_decoder_by_id(*pdtok))) {
			printf("Protocol decoder %s not found.", *pdtok);
			return;
		}
		printf("ID: %s\nName: %s\nLong name: %s\nDescription: %s\n",
				dec->id, dec->name, dec->longname, dec->desc);
		printf("License: %s\n", dec->license);
		if (dec->annotations) {
			printf("Annotations:\n");
			for (l = dec->annotations; l; l = l->next) {
				ann = l->data;
				printf("- %s\n  %s\n", ann[0], ann[1]);
			}
		}
		if ((doc = srd_decoder_doc(dec))) {
			printf("Documentation:\n%s\n", doc[0] == '\n' ? doc+1 : doc);
			g_free(doc);
		}
	}

	g_strfreev(pdtokens);

}

static void datafeed_in(struct sr_device *device, struct sr_datafeed_packet *packet)
{
	static struct sr_output *o = NULL;
	static int probelist[SR_MAX_NUM_PROBES] = { 0 };
	static uint64_t received_samples = 0;
	static int unitsize = 0;
	static int triggered = 0;
	static FILE *outfile = NULL;
	struct sr_probe *probe;
	struct sr_datafeed_header *header;
	struct sr_datafeed_logic *logic;
	int num_enabled_probes, sample_size, ret, i;
	uint64_t output_len, filter_out_len;
	char *output_buf, *filter_out;

	/* If the first packet to come in isn't a header, don't even try. */
	if (packet->type != SR_DF_HEADER && o == NULL)
		return;

	sample_size = -1;
	switch (packet->type) {
	case SR_DF_HEADER:
		g_message("cli: Received SR_DF_HEADER");
		/* Initialize the output module. */
		if (!(o = malloc(sizeof(struct sr_output)))) {
			printf("Output module malloc failed.\n");
			exit(1);
		}
		o->format = output_format;
		o->device = device;
		o->param = output_format_param;
		if (o->format->init) {
			if (o->format->init(o) != SR_OK) {
				printf("Output format initialization failed.\n");
				exit(1);
			}
		}

		header = packet->payload;
		num_enabled_probes = 0;
		for (i = 0; i < header->num_logic_probes; i++) {
			probe = g_slist_nth_data(device->probes, i);
			if (probe->enabled)
				probelist[num_enabled_probes++] = probe->index;
		}
		/* How many bytes we need to store num_enabled_probes bits */
		unitsize = (num_enabled_probes + 7) / 8;

		outfile = stdout;
		if (opt_output_file) {
			if (default_output_format) {
				/* output file is in session format, which means we'll
				 * dump everything in the datastore as it comes in,
				 * and save from there after the session. */
				outfile = NULL;
				ret = sr_datastore_new(unitsize, &(device->datastore));
				if (ret != SR_OK) {
					printf("Failed to create datastore.\n");
					exit(1);
				}
			} else {
				/* saving to a file in whatever format was set
				 * with --format, so all we need is a filehandle */
				outfile = g_fopen(opt_output_file, "wb");
			}
		}
		if (opt_pds)
			srd_session_start(num_enabled_probes, unitsize,
					header->samplerate);
		break;
	case SR_DF_END:
		g_message("cli: Received SR_DF_END");
		if (!o) {
			g_message("cli: double end!");
			break;
		}
		if (o->format->event) {
			o->format->event(o, SR_DF_END, &output_buf, &output_len);
			if (output_len) {
				if (outfile)
					fwrite(output_buf, 1, output_len, outfile);
				free(output_buf);
				output_len = 0;
			}
		}
		if (limit_samples && received_samples < limit_samples)
			printf("Device only sent %" PRIu64 " samples.\n",
			       received_samples);
		if (opt_continuous)
			printf("Device stopped after %" PRIu64 " samples.\n",
			       received_samples);
		sr_session_halt();
		if (outfile && outfile != stdout)
			fclose(outfile);
		free(o);
		o = NULL;
		break;
	case SR_DF_TRIGGER:
		g_message("cli: received SR_DF_TRIGGER at %"PRIu64" ms",
				packet->timeoffset / 1000000);
		if (o->format->event)
			o->format->event(o, SR_DF_TRIGGER, &output_buf,
					 &output_len);
		triggered = 1;
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		sample_size = logic->unitsize;
		g_message("cli: received SR_DF_LOGIC at %f ms duration %f ms, %"PRIu64" bytes",
				packet->timeoffset / 1000000.0, packet->duration / 1000000.0,
				logic->length);
		break;
	case SR_DF_ANALOG:
		break;
	}

	/* not supporting anything but SR_DF_LOGIC for now */

	if (sample_size == -1 || logic->length == 0)
		return;

	/* Don't store any samples until triggered. */
	if (opt_wait_trigger && !triggered)
		return;

	if (limit_samples && received_samples >= limit_samples)
		return;

	/* TODO: filters only support SR_DF_LOGIC */
	ret = sr_filter_probes(sample_size, unitsize, probelist,
				   logic->data, logic->length,
				   &filter_out, &filter_out_len);
	if (ret != SR_OK)
		return;

	/* what comes out of the filter is guaranteed to be packed into the
	 * minimum size needed to support the number of samples at this sample
	 * size. however, the driver may have submitted too much -- cut off
	 * the buffer of the last packet according to the sample limit.
	 */
	if (limit_samples && (received_samples + logic->length / sample_size >
			limit_samples * sample_size))
		filter_out_len = limit_samples * sample_size - received_samples;

	if (device->datastore)
		sr_datastore_put(device->datastore, filter_out,
				 filter_out_len, sample_size, probelist);

	if (opt_output_file && default_output_format)
		/* saving to a session file, don't need to do anything else
		 * to this data for now. */
		goto cleanup;

	if (opt_pds) {
		if (srd_session_feed(received_samples, (uint8_t*)filter_out,
				filter_out_len) != SRD_OK)
			abort();
	} else {
		output_len = 0;
		if (o->format->data && packet->type == o->format->df_type)
			o->format->data(o, filter_out, filter_out_len, &output_buf, &output_len);
		if (output_len) {
			fwrite(output_buf, 1, output_len, outfile);
			free(output_buf);
		}
	}

cleanup:
	g_free(filter_out);
	received_samples += logic->length / sample_size;

}

/* Register the given PDs for this session.
 * Accepts a string of the form: "spi:sck=3:sdata=4,spi:sck=3:sdata=5"
 * That will instantiate two SPI decoders on the clock but different data
 * lines.
 */
static int register_pds(struct sr_device *device, const char *pdstring)
{
	gpointer dummy;
	char **pdtokens, **pdtok;

	/* Avoid compiler warnings. */
	(void)device;

	g_datalist_init(&pd_ann_visible);
	pdtokens = g_strsplit(pdstring, ",", -1);

	/* anything, but not NULL */
	dummy = register_pds;
	for (pdtok = pdtokens; *pdtok; pdtok++) {
		struct srd_decoder_instance *di;

		/* Configure probes from command line */
		char **optokens, **optok;
		optokens = g_strsplit(*pdtok, ":", -1);
		di = srd_instance_new(optokens[0], NULL);
		if(!di) {
			fprintf(stderr, "Failed to instantiate PD: %s\n",
					optokens[0]);
			g_strfreev(optokens);
			g_strfreev(pdtokens);
			return -1;
		}
		g_datalist_set_data(&pd_ann_visible, optokens[0], dummy);
		for (optok = optokens+1; *optok; optok++) {
			char probe[strlen(*optok)];
			int num;
			if (sscanf(*optok, "%[^=]=%d", probe, &num) == 2) {
				printf("Setting probe '%s' to %d\n", probe, num);
				srd_instance_set_probe(di, probe, num);
			} else {
				fprintf(stderr, "Error: Couldn't parse decoder "
					"options correctly! Aborting.\n");
				/* FIXME: Better error handling. */
				exit(EXIT_FAILURE);
			}
		}
		g_strfreev(optokens);

		/* TODO: Handle errors. */
	}

	g_strfreev(pdtokens);

	return 0;
}

void show_pd_annotation(struct srd_proto_data *pdata)
{
	int i;
	char **annotations;

	annotations = pdata->data;
	if (pdata->ann_format != 0) {
		/* CLI only shows the default annotation format */
		return;
	}

	if (!g_datalist_get_data(&pd_ann_visible, pdata->pdo->proto_id)) {
		/* not in the list of PDs whose annotations we're showing */
		return;
	}

	if (opt_loglevel > SR_LOG_WARN)
		printf("%"PRIu64"-%"PRIu64" ", pdata->start_sample, pdata->end_sample);
	printf("%s: ", pdata->pdo->proto_id);
	for (i = 0; annotations[i]; i++)
		printf("\"%s\" ", annotations[i]);
	printf("\n");

}

static int select_probes(struct sr_device *device)
{
	struct sr_probe *probe;
	char **probelist;
	int max_probes, i;

	if (!opt_probes)
		return SR_OK;

	/*
	 * This only works because a device by default initializes
	 * and enables all its probes.
	 */
	max_probes = g_slist_length(device->probes);
	probelist = parse_probestring(max_probes, opt_probes);
	if (!probelist) {
		return SR_ERR;
	}

	for (i = 0; i < max_probes; i++) {
		if (probelist[i]) {
			sr_device_probe_name(device, i + 1, probelist[i]);
			g_free(probelist[i]);
		} else {
			probe = sr_device_probe_find(device, i + 1);
			probe->enabled = FALSE;
		}
	}
	g_free(probelist);

	return SR_OK;
}

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
		fprintf(stderr, "cli: %s: no supported input formats "
			"available", __func__);
		return NULL;
	}

	/* If the user specified -I / --input-format, use that one. */
	if (opt) {
		for (i = 0; inputs[i]; i++) {
			if (strcasecmp(inputs[i]->id, opt_input_format))
				continue;
			printf("Using user-specified input file format"
			       " '%s'.\n", inputs[i]->id);
			return inputs[i];
		}

		/* The user specified an unknown input format, return NULL. */
		fprintf(stderr, "Error: Specified input file format '%s' is "
			"unknown.\n", opt_input_format);
		return NULL;
	}

	/* Otherwise, try to find an input module that can handle this file. */
	for (i = 0; inputs[i]; i++) {
		if (inputs[i]->format_match(filename))
			break;
	}

	/* Return NULL if no input module wanted to touch this. */
	if (!inputs[i]) {
		fprintf(stderr, "Error: No matching input module found.\n");
		return NULL;
	}
		
	printf("Using input file format '%s'.\n", inputs[i]->id);
	return inputs[i];
}

static void load_input_file_format(void)
{
	struct stat st;
	struct sr_input *in;
	struct sr_input_format *input_format;

	input_format = determine_input_file_format(opt_input_file,
						   opt_input_format);
	if (!input_format) {
		fprintf(stderr, "Error: Couldn't detect input file format.\n");
		return;
	}

	if (stat(opt_input_file, &st) == -1) {
		printf("Failed to load %s: %s\n", opt_input_file,
			strerror(errno));
		exit(1);
	}

	/* Initialize the input module. */
	if (!(in = malloc(sizeof(struct sr_input)))) {
		printf("Failed to allocate input module.\n");
		exit(1);
	}
	in->format = input_format;
	in->param = input_format_param;
	if (in->format->init) {
		if (in->format->init(in) != SR_OK) {
			printf("Input format init failed.\n");
			exit(1);
		}
	}

	if (select_probes(in->vdevice) > 0)
            return;

	sr_session_new();
	sr_session_datafeed_callback_add(datafeed_in);
	if (sr_session_device_add(in->vdevice) != SR_OK) {
		printf("Failed to use device.\n");
		sr_session_destroy();
		return;
	}

	input_format->loadfile(in, opt_input_file);
	if (opt_output_file && default_output_format) {
		if (sr_session_save(opt_output_file) != SR_OK)
			printf("Failed to save session.\n");
	}
	sr_session_destroy();

}

static void load_input_file(void)
{

	if (sr_session_load(opt_input_file) == SR_OK) {
		/* sigrok session file */
		sr_session_datafeed_callback_add(datafeed_in);
		sr_session_start();
		sr_session_run();
		sr_session_stop();
	}
	else {
		/* fall back on input modules */
		load_input_file_format();
	}

}

int num_real_devices(void)
{
	struct sr_device *device;
	GSList *devices, *l;
	int num_devices;

	num_devices = 0;
	devices = sr_device_list();
	for (l = devices; l; l = l->next) {
		device = l->data;
		if (!strstr(device->plugin->name, "demo"))
			num_devices++;
	}

	return num_devices;
}

static int set_device_options(struct sr_device *device, GHashTable *args)
{
	GHashTableIter iter;
	gpointer key, value;
	int ret, i;
	uint64_t tmp_u64;
	gboolean found;
	gboolean tmp_bool;

	g_hash_table_iter_init(&iter, args);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		found = FALSE;
		for (i = 0; sr_hwcap_options[i].capability; i++) {
			if (strcmp(sr_hwcap_options[i].shortname, key))
				continue;
			if ((value == NULL) && 
			    (sr_hwcap_options[i].type != SR_T_BOOL)) {
				printf("Option '%s' needs a value.\n", (char *)key);
				return SR_ERR;
			}
			found = TRUE;
			switch (sr_hwcap_options[i].type) {
			case SR_T_UINT64:
				ret = sr_parse_sizestring(value, &tmp_u64);
				if (ret != SR_OK)
					break;
				ret = device->plugin-> set_configuration(device-> plugin_index,
						sr_hwcap_options[i]. capability, &tmp_u64);
				break;
			case SR_T_CHAR:
				ret = device->plugin-> set_configuration(device-> plugin_index,
						sr_hwcap_options[i]. capability, value);
				break;
			case SR_T_BOOL:
				if (!value)
					tmp_bool = TRUE;
				else 
					tmp_bool = sr_parse_boolstring(value);
				ret = device->plugin-> set_configuration(device-> plugin_index,
						sr_hwcap_options[i]. capability, 
						GINT_TO_POINTER(tmp_bool));
				break;
			default:
				ret = SR_ERR;
			}

			if (ret != SR_OK) {
				printf("Failed to set device option '%s'.\n", (char *)key);
				return ret;
			}
			else
				break;
		}
		if (!found) {
			printf("Unknown device option '%s'.\n", (char *) key);
			return SR_ERR;
		}
	}

	return SR_OK;
}

static void run_session(void)
{
	struct sr_device *device;
	GHashTable *devargs;
	int num_devices, max_probes, *capabilities, i;
	uint64_t tmp_u64, time_msec;
	char **probelist, *devspec;

	devargs = NULL;
	if (opt_device) {
		devargs = parse_generic_arg(opt_device);
		devspec = g_hash_table_lookup(devargs, "sigrok_key");
		device = parse_devicestring(devspec);
		if (!device) {
			g_warning("Device not found.");
			return;
		}
		g_hash_table_remove(devargs, "sigrok_key");
	} else {
		num_devices = num_real_devices();
		if (num_devices == 1) {
			/* No device specified, but there is only one. */
			devargs = NULL;
			device = parse_devicestring("0");
		} else if (num_devices == 0) {
			printf("No devices found.\n");
			return;
		} else {
			printf("%d devices found, please select one.\n", num_devices);
			return;
		}
	}

	sr_session_new();
	sr_session_datafeed_callback_add(datafeed_in);

	if (sr_session_device_add(device) != SR_OK) {
		printf("Failed to use device.\n");
		sr_session_destroy();
		return;
	}

	if (devargs) {
		if (set_device_options(device, devargs) != SR_OK) {
			sr_session_destroy();
			return;
		}
		g_hash_table_destroy(devargs);
	}

	if (select_probes(device) != SR_OK)
            return;

	if (opt_continuous) {
		capabilities = device->plugin->get_capabilities();
		if (!sr_find_hwcap(capabilities, SR_HWCAP_CONTINUOUS)) {
			printf("This device does not support continuous sampling.");
			sr_session_destroy();
			return;
		}
	}

	if (opt_triggers) {
		probelist = sr_parse_triggerstring(device, opt_triggers);
		if (!probelist) {
			sr_session_destroy();
			return;
		}

		max_probes = g_slist_length(device->probes);
		for (i = 0; i < max_probes; i++) {
			if (probelist[i]) {
				sr_device_trigger_set(device, i + 1, probelist[i]);
				g_free(probelist[i]);
			}
		}
		g_free(probelist);
	}

	if (opt_time) {
		time_msec = sr_parse_timestring(opt_time);
		if (time_msec == 0) {
			printf("Invalid time '%s'\n", opt_time);
			sr_session_destroy();
			return;
		}

		capabilities = device->plugin->get_capabilities();
		if (sr_find_hwcap(capabilities, SR_HWCAP_LIMIT_MSEC)) {
			if (device->plugin->set_configuration(device->plugin_index,
							  SR_HWCAP_LIMIT_MSEC, &time_msec) != SR_OK) {
				printf("Failed to configure time limit.\n");
				sr_session_destroy();
				return;
			}
		}
		else {
			/* time limit set, but device doesn't support this...
			 * convert to samples based on the samplerate.
			 */
			limit_samples = 0;
			if (sr_device_has_hwcap(device, SR_HWCAP_SAMPLERATE)) {
				tmp_u64 = *((uint64_t *) device->plugin->get_device_info(
						device->plugin_index, SR_DI_CUR_SAMPLERATE));
				limit_samples = tmp_u64 * time_msec / (uint64_t) 1000;
			}
			if (limit_samples == 0) {
				printf("Not enough time at this samplerate.\n");
				sr_session_destroy();
				return;
			}

			if (device->plugin->set_configuration(device->plugin_index,
						  SR_HWCAP_LIMIT_SAMPLES, &limit_samples) != SR_OK) {
				printf("Failed to configure time-based sample limit.\n");
				sr_session_destroy();
				return;
			}
		}
	}

	if (opt_samples) {
		if ((sr_parse_sizestring(opt_samples, &limit_samples) != SR_OK)
			|| (device->plugin->set_configuration(device->plugin_index,
					SR_HWCAP_LIMIT_SAMPLES, &limit_samples) != SR_OK)) {
			printf("Failed to configure sample limit.\n");
			sr_session_destroy();
			return;
		}
	}

	if (device->plugin->set_configuration(device->plugin_index,
		  SR_HWCAP_PROBECONFIG, (char *)device->probes) != SR_OK) {
		printf("Failed to configure probes.\n");
		sr_session_destroy();
		return;
	}

	if (sr_session_start() != SR_OK) {
		printf("Failed to start session.\n");
		sr_session_destroy();
		return;
	}

	if (opt_continuous)
		add_anykey();

	sr_session_run();

	if (opt_continuous)
		clear_anykey();

	if (opt_output_file && default_output_format) {
		if (sr_session_save(opt_output_file) != SR_OK)
			printf("Failed to save session.\n");
	}
	sr_session_destroy();

}

static void logger(const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
	/* Avoid compiler warnings. */
	(void)log_domain;
	(void)user_data;

	/*
	 * All messages, warnings, errors etc. go to stderr (not stdout) in
	 * order to not mess up the CLI tool data output, e.g. VCD output.
	 */
	if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_WARNING)) {
		fprintf(stderr, "%s\n", message);
		fflush(stderr);
	} else {
		if ((log_level & G_LOG_LEVEL_MESSAGE && debug == 1)
		    || debug == 2) {
			printf("%s\n", message);
			fflush(stderr);
		}
	}
}

int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error;
	GHashTable *fmtargs;
	GHashTableIter iter;
	gpointer key, value;
	struct sr_output_format **outputs;
	struct srd_decoder_instance *di_from, *di_to;
	int i, ret;
	char *fmtspec, **pds;

	g_log_set_default_handler(logger, NULL);
	if (getenv("SIGROK_DEBUG"))
		debug = strtol(getenv("SIGROK_DEBUG"), NULL, 10);

	error = NULL;
	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, optargs, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_warning("%s", error->message);
		return 1;
	}

	/* Set the loglevel (amount of messages to output) for libsigrok. */
	if (sr_set_loglevel(opt_loglevel) != SR_OK) {
		fprintf(stderr, "cli: %s: sr_set_loglevel(%d) failed\n",
			__func__, opt_loglevel);
		return 1;
	}

	/* Set the loglevel (amount of messages to output) for libsigrokdecode. */
	if (srd_set_loglevel(opt_loglevel) != SRD_OK) {
		fprintf(stderr, "cli: %s: srd_set_loglevel(%d) failed\n",
			__func__, opt_loglevel);
		return 1;
	}

	if (sr_init() != SR_OK)
		return 1;

	if (opt_pds) {
		if (srd_init() != SRD_OK) {
			printf("Failed to initialize sigrokdecode\n");
			return 1;
		}
		if (register_pds(NULL, opt_pds) != 0) {
			printf("Failed to register protocol decoders\n");
			return 1;
		}
		if (srd_register_callback(SRD_OUTPUT_ANN,
				show_pd_annotation) != SRD_OK) {
			printf("Failed to register protocol decoder callback\n");
			return 1;
		}
	}

	if (opt_pd_stack) {
		pds = g_strsplit(opt_pd_stack, ":", 0);
		if (g_strv_length(pds) < 2) {
			printf("Specify at least two protocol decoders to stack.\n");
			return 1;
		}

		if (!(di_from = srd_instance_find(pds[0]))) {
			printf("Cannot stack protocol decoder '%s': instance not found.\n", pds[0]);
			return 1;
		}
		for (i = 1; pds[i]; i++) {
			if (!(di_to = srd_instance_find(pds[i]))) {
				printf("Cannot stack protocol decoder '%s': instance not found.\n", pds[i]);
				return 1;
			}
			if ((ret = srd_instance_stack(di_from, di_to) != SRD_OK))
				return ret;

			/* Don't show annotation from this PD. Only the last PD in
			 * the stack will be left on the annotation list.
			 */
			g_datalist_remove_data(&pd_ann_visible, di_from->instance_id);

			di_from = di_to;
		}
		g_strfreev(pds);
	}

	if (!opt_format) {
		opt_format = DEFAULT_OUTPUT_FORMAT;
		/* we'll need to remember this so when saving to a file
		 * later, sigrok session format will be used.
		 */
		default_output_format = TRUE;
	}
	fmtargs = parse_generic_arg(opt_format);
	fmtspec = g_hash_table_lookup(fmtargs, "sigrok_key");
	if (!fmtspec) {
		printf("Invalid output format.\n");
		return 1;
	}
	outputs = sr_output_list();
	for (i = 0; outputs[i]; i++) {
		if (strcmp(outputs[i]->id, fmtspec))
			continue;
		g_hash_table_remove(fmtargs, "sigrok_key");
		output_format = outputs[i];
		g_hash_table_iter_init(&iter, fmtargs);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			/* only supporting one parameter per output module
			 * for now, and only its value */
			output_format_param = g_strdup(value);
			break;
		}
		break;
	}
	if (!output_format) {
		printf("invalid output format %s\n", opt_format);
		return 1;
	}

	if (opt_version)
		show_version();
	else if (opt_list_devices)
		show_device_list();
	else if (opt_input_file)
		load_input_file();
	else if (opt_samples || opt_time || opt_continuous)
		run_session();
	else if (opt_device)
		show_device_detail();
	else if (opt_pds)
		show_pd_detail();
	else
		printf("%s", g_option_context_get_help(context, TRUE, NULL));

	if (opt_pds)
		srd_exit();

	g_option_context_free(context);
	g_hash_table_destroy(fmtargs);
	sr_exit();

	return 0;
}
