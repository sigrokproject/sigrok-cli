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

#include "config.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <libsigrok/libsigrok.h>
#ifdef HAVE_SRD
#include <libsigrokdecode/libsigrokdecode.h> /* First, so we avoid a _POSIX_C_SOURCE warning. */
#endif
#include "sigrok-cli.h"

static struct sr_output_format *output_format = NULL;
static int default_output_format = FALSE;
static char *output_format_param = NULL;
static GByteArray *savebuf;
static uint64_t limit_samples = 0;
static uint64_t limit_frames = 0;

extern gchar *opt_output_file;
extern gchar *opt_output_format;
extern gchar *opt_pds;
extern gboolean opt_wait_trigger;
extern gchar *opt_time;
extern gchar *opt_samples;
extern gchar *opt_frames;
extern gchar *opt_continuous;
extern gchar *opt_config;
extern gchar *opt_triggers;
#ifdef HAVE_SRD
extern struct srd_session *srd_sess;
#endif


static GArray *get_enabled_logic_probes(const struct sr_dev_inst *sdi)
{
	struct sr_probe *probe;
	GArray *probes;
	GSList *l;

	probes = g_array_new(FALSE, FALSE, sizeof(int));
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->type != SR_PROBE_LOGIC)
			continue;
		if (probe->enabled != TRUE)
			continue;
		g_array_append_val(probes, probe->index);
	}

	return probes;
}

static int set_limit_time(const struct sr_dev_inst *sdi)
{
	GVariant *gvar;
	uint64_t time_msec;
	uint64_t samplerate;

	if (!(time_msec = sr_parse_timestring(opt_time))) {
		g_critical("Invalid time '%s'", opt_time);
		return SR_ERR;
	}

	if (sr_dev_has_option(sdi, SR_CONF_LIMIT_MSEC)) {
		gvar = g_variant_new_uint64(time_msec);
		if (sr_config_set(sdi, NULL, SR_CONF_LIMIT_MSEC, gvar) != SR_OK) {
			g_critical("Failed to configure time limit.");
			return SR_ERR;
		}
	} else if (sr_dev_has_option(sdi, SR_CONF_SAMPLERATE)) {
		/* Convert to samples based on the samplerate.  */
		sr_config_get(sdi->driver, sdi, NULL, SR_CONF_SAMPLERATE, &gvar);
		samplerate = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
		limit_samples = (samplerate) * time_msec / (uint64_t)1000;
		if (limit_samples == 0) {
			g_critical("Not enough time at this samplerate.");
			return SR_ERR;
		}
		gvar = g_variant_new_uint64(limit_samples);
		if (sr_config_set(sdi, NULL, SR_CONF_LIMIT_SAMPLES, gvar) != SR_OK) {
			g_critical("Failed to configure time-based sample limit.");
			return SR_ERR;
		}
	} else {
		g_critical("This device does not support time limits.");
		return SR_ERR;
	}

	return SR_OK;
}

int setup_output_format(void)
{
	GHashTable *fmtargs;
	GHashTableIter iter;
	gpointer key, value;
	struct sr_output_format **outputs;
	int i;
	char *fmtspec;

	if (opt_output_format && !strcmp(opt_output_format, "sigrok")) {
		/* Doesn't really exist as an output module - this is
		 * the session save mode. */
		g_free(opt_output_format);
		opt_output_format = NULL;
	}

	if (!opt_output_format) {
		opt_output_format = DEFAULT_OUTPUT_FORMAT;
		/* we'll need to remember this so when saving to a file
		 * later, sigrok session format will be used.
		 */
		default_output_format = TRUE;
	}

	fmtargs = parse_generic_arg(opt_output_format, TRUE);
	fmtspec = g_hash_table_lookup(fmtargs, "sigrok_key");
	if (!fmtspec) {
		g_critical("Invalid output format.");
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
		g_critical("Invalid output format %s.", opt_output_format);
		return 1;
	}
	g_hash_table_destroy(fmtargs);

	return 0;
}

void datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog *analog;
	struct sr_config *src;
	static struct sr_output *o = NULL;
	static GArray *logic_probelist = NULL;
	static uint64_t received_samples = 0;
	static int unitsize = 0;
	static int triggered = 0;
	static FILE *outfile = NULL;
	GSList *l;
	GString *out;
	int sample_size, ret;
	uint64_t samplerate, output_len, filter_out_len, end_sample;
	uint8_t *output_buf, *filter_out;

	(void) cb_data;

	/* If the first packet to come in isn't a header, don't even try. */
	if (packet->type != SR_DF_HEADER && o == NULL)
		return;

	sample_size = -1;
	switch (packet->type) {
	case SR_DF_HEADER:
		g_debug("cli: Received SR_DF_HEADER");
		/* Initialize the output module. */
		if (!(o = g_try_malloc(sizeof(struct sr_output)))) {
			g_critical("Output module malloc failed.");
			exit(1);
		}
		o->format = output_format;
		o->sdi = (struct sr_dev_inst *)sdi;
		o->param = output_format_param;
		if (o->format->init) {
			if (o->format->init(o) != SR_OK) {
				g_critical("Output format initialization failed.");
				exit(1);
			}
		}

		/* Prepare non-stdout output. */
		outfile = stdout;
		if (opt_output_file) {
			if (default_output_format) {
				/* output file is in session format, so we'll
				 * keep a copy of everything as it comes in
				 * and save from there after the session. */
				outfile = NULL;
				savebuf = g_byte_array_new();
			} else {
				/* saving to a file in whatever format was set
				 * with --format, so all we need is a filehandle */
				outfile = g_fopen(opt_output_file, "wb");
			}
		}

		/* Prepare for logic data. */
		logic_probelist = get_enabled_logic_probes(sdi);
		/* How many bytes we need to store the packed samples. */
		unitsize = (logic_probelist->len + 7) / 8;

#ifdef HAVE_SRD
		GVariant *gvar;
		if (opt_pds && logic_probelist->len) {
			if (sr_config_get(sdi->driver, sdi, NULL, SR_CONF_SAMPLERATE,
					&gvar) == SR_OK) {
				samplerate = g_variant_get_uint64(gvar);
				g_variant_unref(gvar);
				if (srd_session_metadata_set(srd_sess, SRD_CONF_SAMPLERATE,
						g_variant_new_uint64(samplerate)) != SRD_OK) {
					g_critical("Failed to configure decode session.");
					break;
				}
			}
			if (srd_session_start(srd_sess) != SRD_OK) {
				g_critical("Failed to start decode session.");
				break;
			}
		}
#endif
		break;

	case SR_DF_META:
		g_debug("cli: received SR_DF_META");
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			switch (src->key) {
			case SR_CONF_SAMPLERATE:
				samplerate = g_variant_get_uint64(src->data);
				g_debug("cli: got samplerate %"PRIu64" Hz", samplerate);
#ifdef HAVE_SRD
				if (opt_pds) {
					if (srd_session_metadata_set(srd_sess, SRD_CONF_SAMPLERATE,
							g_variant_new_uint64(samplerate)) != SRD_OK) {
						g_critical("Failed to pass samplerate to decoder.");
					}
				}
#endif
				break;
			case SR_CONF_SAMPLE_INTERVAL:
				samplerate = g_variant_get_uint64(src->data);
				g_debug("cli: got sample interval %"PRIu64" ms", samplerate);
				break;
			default:
				/* Unknown metadata is not an error. */
				break;
			}
		}
		break;

	case SR_DF_TRIGGER:
		g_debug("cli: received SR_DF_TRIGGER");
		if (o->format->event)
			o->format->event(o, SR_DF_TRIGGER, &output_buf,
					 &output_len);
		triggered = 1;
		break;

	case SR_DF_LOGIC:
		logic = packet->payload;
		g_message("cli: received SR_DF_LOGIC, %"PRIu64" bytes", logic->length);
		sample_size = logic->unitsize;
		if (logic->length == 0)
			break;

		/* Don't store any samples until triggered. */
		if (opt_wait_trigger && !triggered)
			break;

		if (limit_samples && received_samples >= limit_samples)
			break;

		ret = sr_filter_probes(sample_size, unitsize, logic_probelist,
				logic->data, logic->length,
				&filter_out, &filter_out_len);
		if (ret != SR_OK)
			break;

		/*
		 * What comes out of the filter is guaranteed to be packed into the
		 * minimum size needed to support the number of samples at this sample
		 * size. however, the driver may have submitted too much. Cut off
		 * the buffer of the last packet according to the sample limit.
		 */
		if (limit_samples && (received_samples + logic->length / sample_size >
				limit_samples * sample_size))
			filter_out_len = limit_samples * sample_size - received_samples;

		if (opt_output_file && default_output_format) {
			/* Saving to a session file. */
			g_byte_array_append(savebuf, filter_out, filter_out_len);
		} else {
			if (opt_pds) {
#ifdef HAVE_SRD
				end_sample = received_samples + filter_out_len / unitsize;
				if (srd_session_send(srd_sess, received_samples, end_sample,
						(uint8_t*)filter_out, filter_out_len) != SRD_OK)
					sr_session_stop();
#endif
			} else {
				output_len = 0;
				if (o->format->data && packet->type == o->format->df_type)
					o->format->data(o, filter_out, filter_out_len,
							&output_buf, &output_len);
				if (output_len) {
					fwrite(output_buf, 1, output_len, outfile);
					fflush(outfile);
					g_free(output_buf);
				}
			}
		}
		g_free(filter_out);

		received_samples += logic->length / sample_size;
		break;

	case SR_DF_ANALOG:
		analog = packet->payload;
		g_message("cli: received SR_DF_ANALOG, %d samples", analog->num_samples);
		if (analog->num_samples == 0)
			break;

		if (limit_samples && received_samples >= limit_samples)
			break;

		if (o->format->data && packet->type == o->format->df_type) {
			o->format->data(o, (const uint8_t *)analog->data,
					analog->num_samples * sizeof(float),
					&output_buf, &output_len);
			if (output_buf) {
				fwrite(output_buf, 1, output_len, outfile);
				fflush(outfile);
				g_free(output_buf);
			}
		}

		received_samples += analog->num_samples;
		break;

	case SR_DF_FRAME_BEGIN:
		g_debug("cli: received SR_DF_FRAME_BEGIN");
		if (o->format->event) {
			o->format->event(o, SR_DF_FRAME_BEGIN, &output_buf,
					 &output_len);
			if (output_buf) {
				fwrite(output_buf, 1, output_len, outfile);
				fflush(outfile);
				g_free(output_buf);
			}
		}
		break;

	case SR_DF_FRAME_END:
		g_debug("cli: received SR_DF_FRAME_END");
		if (o->format->event) {
			o->format->event(o, SR_DF_FRAME_END, &output_buf,
					 &output_len);
			if (output_buf) {
				fwrite(output_buf, 1, output_len, outfile);
				fflush(outfile);
				g_free(output_buf);
			}
		}
		break;

	default:
		break;
	}

	if (o && o->format->receive) {
		if (o->format->receive(o, sdi, packet, &out) == SR_OK && out) {
			fwrite(out->str, 1, out->len, outfile);
			fflush(outfile);
			g_string_free(out, TRUE);
		}
	}

	/* SR_DF_END needs to be handled after the output module's receive()
	 * is called, so it can properly clean up that module etc. */
	if (packet->type == SR_DF_END) {
		g_debug("cli: Received SR_DF_END");

		if (o->format->event) {
			o->format->event(o, SR_DF_END, &output_buf, &output_len);
			if (output_buf) {
				if (outfile)
					fwrite(output_buf, 1, output_len, outfile);
				g_free(output_buf);
				output_len = 0;
			}
		}

		if (limit_samples && received_samples < limit_samples)
			g_warning("Device only sent %" PRIu64 " samples.",
			       received_samples);

		if (opt_continuous)
			g_warning("Device stopped after %" PRIu64 " samples.",
			       received_samples);

		g_array_free(logic_probelist, TRUE);

		if (o->format->cleanup)
			o->format->cleanup(o);
		g_free(o);
		o = NULL;

		if (outfile && outfile != stdout)
			fclose(outfile);

		if (opt_output_file && default_output_format && savebuf->len) {
			if (sr_session_save(opt_output_file, sdi, savebuf->data,
					unitsize, savebuf->len / unitsize) != SR_OK)
				g_critical("Failed to save session.");
			g_byte_array_free(savebuf, FALSE);
		}
	}

}

int set_dev_options(struct sr_dev_inst *sdi, GHashTable *args)
{
	const struct sr_config_info *srci;
	struct sr_probe_group *pg;
	GHashTableIter iter;
	gpointer key, value;
	int ret;
	double tmp_double;
	uint64_t tmp_u64, p, q, low, high;
	gboolean tmp_bool;
	GVariant *val, *rational[2], *range[2];

	g_hash_table_iter_init(&iter, args);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (!(srci = sr_config_info_name_get(key))) {
			g_critical("Unknown device option '%s'.", (char *) key);
			return SR_ERR;
		}

		if ((value == NULL) &&
			(srci->datatype != SR_T_BOOL)) {
			g_critical("Option '%s' needs a value.", (char *)key);
			return SR_ERR;
		}
		val = NULL;
		switch (srci->datatype) {
		case SR_T_UINT64:
			ret = sr_parse_sizestring(value, &tmp_u64);
			if (ret != SR_OK)
				break;
			val = g_variant_new_uint64(tmp_u64);
			break;
		case SR_T_CHAR:
			val = g_variant_new_string(value);
			break;
		case SR_T_BOOL:
			if (!value)
				tmp_bool = TRUE;
			else
				tmp_bool = sr_parse_boolstring(value);
			val = g_variant_new_boolean(tmp_bool);
			break;
		case SR_T_FLOAT:
			tmp_double = strtof(value, NULL);
			val = g_variant_new_double(tmp_double);
			break;
		case SR_T_RATIONAL_PERIOD:
			if ((ret = sr_parse_period(value, &p, &q)) != SR_OK)
				break;
			rational[0] = g_variant_new_uint64(p);
			rational[1] = g_variant_new_uint64(q);
			val = g_variant_new_tuple(rational, 2);
			break;
		case SR_T_RATIONAL_VOLT:
			if ((ret = sr_parse_voltage(value, &p, &q)) != SR_OK)
				break;
			rational[0] = g_variant_new_uint64(p);
			rational[1] = g_variant_new_uint64(q);
			val = g_variant_new_tuple(rational, 2);
			break;
		case SR_T_UINT64_RANGE:
			if (sscanf(value, "%"PRIu64"-%"PRIu64, &low, &high) != 2) {
				ret = SR_ERR;
				break;
			} else {
				range[0] = g_variant_new_uint64(low);
				range[1] = g_variant_new_uint64(high);
				val = g_variant_new_tuple(range, 2);
			}
			break;
		default:
			ret = SR_ERR;
		}
		if (val) {
			pg = select_probe_group(sdi);
			ret = sr_config_set(sdi, pg, srci->key, val);
		}
		if (ret != SR_OK) {
			g_critical("Failed to set device option '%s'.", (char *)key);
			return ret;
		}
	}

	return SR_OK;
}

void run_session(void)
{
	GSList *devices;
	GHashTable *devargs;
	GVariant *gvar;
	struct sr_dev_inst *sdi;
	int max_probes, i;
	char **triggerlist;

	devices = device_scan();
	if (!devices) {
		g_critical("No devices found.");
		return;
	}
	if (g_slist_length(devices) > 1) {
		g_critical("sigrok-cli only supports one device for capturing.");
		return;
	}
	sdi = devices->data;

	sr_session_new();
	sr_session_datafeed_callback_add(datafeed_in, NULL);

	if (sr_dev_open(sdi) != SR_OK) {
		g_critical("Failed to open device.");
		return;
	}

	if (sr_session_dev_add(sdi) != SR_OK) {
		g_critical("Failed to add device to session.");
		sr_session_destroy();
		return;
	}

	if (opt_config) {
		if ((devargs = parse_generic_arg(opt_config, FALSE))) {
			if (set_dev_options(sdi, devargs) != SR_OK)
				return;
			g_hash_table_destroy(devargs);
		}
	}

	if (select_probes(sdi) != SR_OK) {
		g_critical("Failed to set probes.");
		sr_session_destroy();
		return;
	}

	if (opt_triggers) {
		if (!(triggerlist = sr_parse_triggerstring(sdi, opt_triggers))) {
			sr_session_destroy();
			return;
		}
		max_probes = g_slist_length(sdi->probes);
		for (i = 0; i < max_probes; i++) {
			if (triggerlist[i]) {
				sr_dev_trigger_set(sdi, i, triggerlist[i]);
				g_free(triggerlist[i]);
			}
		}
		g_free(triggerlist);
	}

	if (opt_continuous) {
		if (!sr_dev_has_option(sdi, SR_CONF_CONTINUOUS)) {
			g_critical("This device does not support continuous sampling.");
			sr_session_destroy();
			return;
		}
	}

	if (opt_time) {
		if (set_limit_time(sdi) != SR_OK) {
			sr_session_destroy();
			return;
		}
	}

	if (opt_samples) {
		if ((sr_parse_sizestring(opt_samples, &limit_samples) != SR_OK)) {
			g_critical("Invalid sample limit '%s'.", opt_samples);
			sr_session_destroy();
			return;
		}
		gvar = g_variant_new_uint64(limit_samples);
		if (sr_config_set(sdi, NULL, SR_CONF_LIMIT_SAMPLES, gvar) != SR_OK) {
			g_critical("Failed to configure sample limit.");
			sr_session_destroy();
			return;
		}
	}

	if (opt_frames) {
		if ((sr_parse_sizestring(opt_frames, &limit_frames) != SR_OK)) {
			g_critical("Invalid sample limit '%s'.", opt_samples);
			sr_session_destroy();
			return;
		}
		gvar = g_variant_new_uint64(limit_frames);
		if (sr_config_set(sdi, NULL, SR_CONF_LIMIT_FRAMES, gvar) != SR_OK) {
			g_critical("Failed to configure frame limit.");
			sr_session_destroy();
			return;
		}
	}

	if (sr_session_start() != SR_OK) {
		g_critical("Failed to start session.");
		sr_session_destroy();
		return;
	}

	if (opt_continuous)
		add_anykey();

	sr_session_run();

	if (opt_continuous)
		clear_anykey();

	sr_session_datafeed_callback_remove_all();
	sr_session_destroy();
	g_slist_free(devices);

}

