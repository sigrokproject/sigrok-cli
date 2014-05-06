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
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdlib.h>

static struct sr_output_format *output_format = NULL;
static int default_output_format = FALSE;
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

struct sr_output *setup_output_format(const struct sr_dev_inst *sdi)
{
	GHashTable *fmtargs;
	struct sr_output *o;
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
	if (!fmtspec)
		g_critical("Invalid output format.");
	outputs = sr_output_list();
	for (i = 0; outputs[i]; i++) {
		if (strcmp(outputs[i]->id, fmtspec))
			continue;
		g_hash_table_remove(fmtargs, "sigrok_key");
		output_format = outputs[i];
		break;
	}
	if (!output_format)
		g_critical("Invalid output format '%s'.", opt_output_format);
	o = sr_output_new(output_format, fmtargs, sdi);
	g_hash_table_destroy(fmtargs);

	return o;
}

void datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog *analog;
	struct sr_config *src;
	struct sr_channel *ch;
	static struct sr_output *o = NULL;
	static uint64_t rcvd_samples_logic = 0;
	static uint64_t rcvd_samples_analog = 0;
	static uint64_t samplerate = 0;
	static int triggered = 0;
	static FILE *outfile = NULL;
	GSList *l;
	GString *out;
	GVariant *gvar;
	uint64_t end_sample;
	uint64_t input_len;
	int i;
	char **channels;

	(void) cb_data;

	/* If the first packet to come in isn't a header, don't even try. */
	if (packet->type != SR_DF_HEADER && o == NULL)
		return;

	switch (packet->type) {
	case SR_DF_HEADER:
		g_debug("cli: Received SR_DF_HEADER.");
		o = setup_output_format(sdi);

		/* Prepare non-stdout output. */
		outfile = stdout;
		if (opt_output_file) {
			if (default_output_format) {
				outfile = NULL;
			} else {
				/* saving to a file in whatever format was set
				 * with --format, so all we need is a filehandle */
				outfile = g_fopen(opt_output_file, "wb");
			}
		}
		rcvd_samples_logic = rcvd_samples_analog = 0;

		if (sr_config_get(sdi->driver, sdi, NULL, SR_CONF_SAMPLERATE,
				&gvar) == SR_OK) {
			samplerate = g_variant_get_uint64(gvar);
			g_variant_unref(gvar);
		}

#ifdef HAVE_SRD
		if (opt_pds) {
			if (samplerate) {
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
		g_debug("cli: Received SR_DF_META.");
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			switch (src->key) {
			case SR_CONF_SAMPLERATE:
				samplerate = g_variant_get_uint64(src->data);
				g_debug("cli: Got samplerate %"PRIu64" Hz.", samplerate);
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
				g_debug("cli: Got sample interval %"PRIu64" ms.", samplerate);
				break;
			default:
				/* Unknown metadata is not an error. */
				break;
			}
		}
		break;

	case SR_DF_TRIGGER:
		g_debug("cli: Received SR_DF_TRIGGER.");
		triggered = 1;
		break;

	case SR_DF_LOGIC:
		logic = packet->payload;
		g_message("cli: Received SR_DF_LOGIC (%"PRIu64" bytes, unitsize = %d).",
				logic->length, logic->unitsize);
		if (logic->length == 0)
			break;

		/* Don't store any samples until triggered. */
		if (opt_wait_trigger && !triggered)
			break;

		if (limit_samples && rcvd_samples_logic >= limit_samples)
			break;

		end_sample = rcvd_samples_logic + logic->length / logic->unitsize;
		/* Cut off last packet according to the sample limit. */
		if (limit_samples && end_sample > limit_samples)
			end_sample = limit_samples;
		input_len = (end_sample - rcvd_samples_logic) * logic->unitsize;

		if (opt_output_file && default_output_format) {
			/* Saving to a session file. */
			if (rcvd_samples_logic == 0) {
				/* First packet with logic data, init session file. */
				channels = g_malloc(sizeof(char *) * g_slist_length(sdi->channels));
				for (i = 0, l = sdi->channels; l; l = l->next) {
					ch = l->data;
					if (ch->enabled && ch->type == SR_CHANNEL_LOGIC)
						channels[i++] = ch->name;
				}
				channels[i] = NULL;
				sr_session_save_init(opt_output_file, samplerate,
						channels);
				g_free(channels);
			}
			save_chunk_logic(logic->data, input_len, logic->unitsize);
		} else {
			if (opt_pds) {
#ifdef HAVE_SRD
				if (srd_session_send(srd_sess, rcvd_samples_logic, end_sample,
						logic->data, input_len) != SRD_OK)
					sr_session_stop();
#endif
			}
		}

		rcvd_samples_logic = end_sample;
		break;

	case SR_DF_ANALOG:
		analog = packet->payload;
		g_message("cli: Received SR_DF_ANALOG (%d samples).", analog->num_samples);
		if (analog->num_samples == 0)
			break;

		if (limit_samples && rcvd_samples_analog >= limit_samples)
			break;

		rcvd_samples_analog += analog->num_samples;
		break;

	case SR_DF_FRAME_BEGIN:
		g_debug("cli: Received SR_DF_FRAME_BEGIN.");
		break;

	case SR_DF_FRAME_END:
		g_debug("cli: Received SR_DF_FRAME_END.");
		break;

	default:
		break;
	}

	if (o && outfile && !opt_pds) {
		if (sr_output_send(o, packet, &out) == SR_OK && out) {
			fwrite(out->str, 1, out->len, outfile);
			fflush(outfile);
			g_string_free(out, TRUE);
		}
	}

	/* SR_DF_END needs to be handled after the output module's receive()
	 * is called, so it can properly clean up that module. */
	if (packet->type == SR_DF_END) {
		g_debug("cli: Received SR_DF_END.");

		if (o)
			sr_output_free(o);
		o = NULL;

		if (outfile && outfile != stdout)
			fclose(outfile);

		if (opt_output_file && default_output_format)
			/* Flush whatever is left out to the session file. */
			save_chunk_logic(NULL, 0, 0);

		if (limit_samples) {
			if (rcvd_samples_logic > 0 && rcvd_samples_logic < limit_samples)
				g_warning("Device only sent %" PRIu64 " samples.",
					   rcvd_samples_logic);
			else if (rcvd_samples_analog > 0 && rcvd_samples_analog < limit_samples)
				g_warning("Device only sent %" PRIu64 " samples.",
					   rcvd_samples_analog);
		}
	}

}

int opt_to_gvar(char *key, char *value, struct sr_config *src)
{
	const struct sr_config_info *srci;
	double tmp_double, dlow, dhigh;
	uint64_t tmp_u64, p, q, low, high;
	GVariant *rational[2], *range[2];
	gboolean tmp_bool;
	int ret;

	if (!(srci = sr_config_info_name_get(key))) {
		g_critical("Unknown device option '%s'.", (char *) key);
		return -1;
	}
	src->key = srci->key;

	if ((value == NULL) &&
		(srci->datatype != SR_T_BOOL)) {
		g_critical("Option '%s' needs a value.", (char *)key);
		return -1;
	}

	ret = 0;
	switch (srci->datatype) {
	case SR_T_UINT64:
		ret = sr_parse_sizestring(value, &tmp_u64);
		if (ret != 0)
			break;
		src->data = g_variant_new_uint64(tmp_u64);
		break;
	case SR_T_INT32:
		ret = sr_parse_sizestring(value, &tmp_u64);
		if (ret != 0)
			break;
		src->data = g_variant_new_int32(tmp_u64);
		break;
	case SR_T_STRING:
		src->data = g_variant_new_string(value);
		break;
	case SR_T_BOOL:
		if (!value)
			tmp_bool = TRUE;
		else
			tmp_bool = sr_parse_boolstring(value);
		src->data = g_variant_new_boolean(tmp_bool);
		break;
	case SR_T_FLOAT:
		tmp_double = strtof(value, NULL);
		src->data = g_variant_new_double(tmp_double);
		break;
	case SR_T_RATIONAL_PERIOD:
		if ((ret = sr_parse_period(value, &p, &q)) != SR_OK)
			break;
		rational[0] = g_variant_new_uint64(p);
		rational[1] = g_variant_new_uint64(q);
		src->data = g_variant_new_tuple(rational, 2);
		break;
	case SR_T_RATIONAL_VOLT:
		if ((ret = sr_parse_voltage(value, &p, &q)) != SR_OK)
			break;
		rational[0] = g_variant_new_uint64(p);
		rational[1] = g_variant_new_uint64(q);
		src->data = g_variant_new_tuple(rational, 2);
		break;
	case SR_T_UINT64_RANGE:
		if (sscanf(value, "%"PRIu64"-%"PRIu64, &low, &high) != 2) {
			ret = -1;
			break;
		} else {
			range[0] = g_variant_new_uint64(low);
			range[1] = g_variant_new_uint64(high);
			src->data = g_variant_new_tuple(range, 2);
		}
		break;
	case SR_T_DOUBLE_RANGE:
		if (sscanf(value, "%lf-%lf", &dlow, &dhigh) != 2) {
			ret = -1;
			break;
		} else {
			range[0] = g_variant_new_double(dlow);
			range[1] = g_variant_new_double(dhigh);
			src->data = g_variant_new_tuple(range, 2);
		}
		break;
	default:
		ret = -1;
	}

	return ret;
}

int set_dev_options(struct sr_dev_inst *sdi, GHashTable *args)
{
	struct sr_config src;
	struct sr_channel_group *cg;
	GHashTableIter iter;
	gpointer key, value;
	int ret;

	g_hash_table_iter_init(&iter, args);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if ((ret = opt_to_gvar(key, value, &src)) != 0)
			return ret;
		cg = select_channel_group(sdi);
		ret = sr_config_set(sdi, cg, src.key, src.data);
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
	uint64_t min_samples, max_samples;
	int max_channels, i;
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

	if (select_channels(sdi) != SR_OK) {
		g_critical("Failed to set channels.");
		sr_session_destroy();
		return;
	}

	if (opt_triggers) {
		if (!(triggerlist = sr_parse_triggerstring(sdi, opt_triggers))) {
			sr_session_destroy();
			return;
		}
		max_channels = g_slist_length(sdi->channels);
		for (i = 0; i < max_channels; i++) {
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
		if (sr_config_list(sdi->driver, sdi, NULL,
				SR_CONF_LIMIT_SAMPLES, &gvar) == SR_OK) {
			/* The device has no compression, or compression is turned
			 * off, and publishes its sample memory size. */
			g_variant_get(gvar, "(tt)", &min_samples, &max_samples);
			g_variant_unref(gvar);
			if (limit_samples < min_samples) {
				g_critical("The device stores at least %"PRIu64
						" samples with the current settings.", min_samples);
			}
			if (limit_samples > max_samples) {
				g_critical("The device can store only %"PRIu64
						" samples with the current settings.", max_samples);
			}
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

void save_chunk_logic(uint8_t *data, uint64_t data_len, int unitsize)
{
	static uint8_t *buf = NULL;
	static int buf_len = 0;
	static int last_unitsize = 0;
	int max;

	if (!buf)
		buf = g_malloc(SAVE_CHUNK_SIZE);

	if (buf_len + data_len > SAVE_CHUNK_SIZE) {
		max = (SAVE_CHUNK_SIZE - buf_len) / unitsize * unitsize;
		memcpy(buf + buf_len, data, max);
		sr_session_append(opt_output_file, buf, unitsize,
				(buf_len + max) / unitsize);
		memcpy(buf, data + max, data_len - max);
		buf_len = data_len - max;
	} else if (data_len == 0 && last_unitsize != 0) {
		/* End of data, flush the buffer out. */
		sr_session_append(opt_output_file, buf, last_unitsize,
				buf_len / last_unitsize);
	} else {
		/* Buffer chunk. */
		memcpy(buf + buf_len, data, data_len);
		buf_len += data_len;
	}
	last_unitsize = unitsize;

}
