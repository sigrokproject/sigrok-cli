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
#include <string.h>

extern gint opt_loglevel;
extern gchar *opt_pds;

static gint sort_inputs(gconstpointer a, gconstpointer b)
{
	const struct sr_input_format *ia = a, *ib = b;

	return strcmp(ia->id, ib->id);
}

static gint sort_outputs(gconstpointer a, gconstpointer b)
{
	const struct sr_output_format *oa = a, *ob = b;

	return strcmp(oa->id, ob->id);
}

static gint sort_drivers(gconstpointer a, gconstpointer b)
{
	const struct sr_dev_driver *sdda = a, *sddb = b;

	return strcmp(sdda->name, sddb->name);
}

#ifdef HAVE_SRD
static gint sort_pds(gconstpointer a, gconstpointer b)
{
	const struct srd_decoder *sda = a, *sdb = b;

	return strcmp(sda->id, sdb->id);
}
#endif

void show_version(void)
{
	struct sr_dev_driver **drivers, *driver;
	struct sr_input_format **inputs, *input;
	struct sr_output_format **outputs, *output;
	const GSList *l;
	GSList *sl;
	int i;
#ifdef HAVE_SRD
	struct srd_decoder *dec;
#endif

	printf("sigrok-cli %s\n\n", VERSION);

	printf("Using libsigrok %s (lib version %s).\n",
	       sr_package_version_string_get(), sr_lib_version_string_get());
#ifdef HAVE_SRD
	printf("Using libsigrokdecode %s (lib version %s).\n\n",
	       srd_package_version_string_get(), srd_lib_version_string_get());
#endif

	printf("Supported hardware drivers:\n");
	drivers = sr_driver_list();
	for (sl = NULL, i = 0; drivers[i]; i++)
		sl = g_slist_append(sl, drivers[i]);
	sl = g_slist_sort(sl, sort_drivers);
	for (l = sl; l; l = l->next) {
		driver = l->data;
		printf("  %-20s %s\n", driver->name, driver->longname);
	}
	printf("\n");
	g_slist_free(sl);

	printf("Supported input formats:\n");
	inputs = sr_input_list();
	for (sl = NULL, i = 0; inputs[i]; i++)
		sl = g_slist_append(sl, inputs[i]);
	sl = g_slist_sort(sl, sort_inputs);
	for (l = sl; l; l = l->next) {
		input = l->data;
		printf("  %-20s %s\n", input->id, input->description);
	}
	printf("\n");
	g_slist_free(sl);

	printf("Supported output formats:\n");
	outputs = sr_output_list();
	for (sl = NULL, i = 0; outputs[i]; i++)
		sl = g_slist_append(sl, outputs[i]);
	sl = g_slist_sort(sl, sort_outputs);
	for (l = sl; l; l = l->next) {
		output = l->data;
		printf("  %-20s %s\n", output->id, output->description);
	}
	printf("\n");
	g_slist_free(sl);

#ifdef HAVE_SRD
	if (srd_init(NULL) == SRD_OK) {
		printf("Supported protocol decoders:\n");
		srd_decoder_load_all();
		sl = g_slist_copy((GSList *)srd_decoder_list());
		sl = g_slist_sort(sl, sort_pds);
		for (l = sl; l; l = l->next) {
			dec = l->data;
			printf("  %-20s %s\n", dec->id, dec->longname);
			/* Print protocol description upon "-l 3" or higher. */
			if (opt_loglevel >= SR_LOG_INFO)
				printf("  %-20s %s\n", "", dec->desc);
		}
		g_slist_free(sl);
		srd_exit();
	}
	printf("\n");
#endif
}

static gint sort_channels(gconstpointer a, gconstpointer b)
{
	const struct sr_channel *pa = a, *pb = b;

	return pa->index - pb->index;
}

static void print_dev_line(const struct sr_dev_inst *sdi)
{
	struct sr_channel *ch;
	GSList *sl, *l;
	GString *s;
	GVariant *gvar;

	s = g_string_sized_new(128);
	g_string_assign(s, sdi->driver->name);
	if (sr_config_get(sdi->driver, sdi, NULL, SR_CONF_CONN, &gvar) == SR_OK) {
		g_string_append(s, ":conn=");
		g_string_append(s, g_variant_get_string(gvar, NULL));
		g_variant_unref(gvar);
	}
	g_string_append(s, " - ");
	if (sdi->vendor && sdi->vendor[0])
		g_string_append_printf(s, "%s ", sdi->vendor);
	if (sdi->model && sdi->model[0])
		g_string_append_printf(s, "%s ", sdi->model);
	if (sdi->version && sdi->version[0])
		g_string_append_printf(s, "%s ", sdi->version);
	if (sdi->channels) {
		if (g_slist_length(sdi->channels) == 1) {
			ch = sdi->channels->data;
			g_string_append_printf(s, "with 1 channel: %s", ch->name);
		} else {
			sl = g_slist_sort(g_slist_copy(sdi->channels), sort_channels);
			g_string_append_printf(s, "with %d channels:", g_slist_length(sl));
			for (l = sl; l; l = l->next) {
				ch = l->data;
				g_string_append_printf(s, " %s", ch->name);
			}
			g_slist_free(sl);
		}
	}
	g_string_append_printf(s, "\n");
	printf("%s", s->str);
	g_string_free(s, TRUE);

}

void show_dev_list(void)
{
	struct sr_dev_inst *sdi;
	GSList *devices, *l;

	if (!(devices = device_scan()))
		return;

	printf("The following devices were found:\n");
	for (l = devices; l; l = l->next) {
		sdi = l->data;
		print_dev_line(sdi);
	}
	g_slist_free(devices);

}

void show_dev_detail(void)
{
	struct sr_dev_inst *sdi;
	const struct sr_config_info *srci;
	struct sr_channel *ch;
	struct sr_channel_group *channel_group, *cg;
	GSList *devices, *cgl, *chl;
	GVariant *gvar_opts, *gvar_dict, *gvar_list, *gvar;
	gsize num_opts, num_elements;
	double dlow, dhigh, dcur_low, dcur_high;
	const uint64_t *uint64, p, q, low, high;
	uint64_t cur_low, cur_high;
	const int32_t *opts;
	unsigned int num_devices, o, i;
	char *tmp_str;
	char *s;
	const char *charopts, **stropts;

	if (!(devices = device_scan())) {
		g_critical("No devices found.");
		return;
	}

	num_devices = g_slist_length(devices);
	if (num_devices > 1) {
		g_critical("%d devices found. Use --scan to show them, "
				"and select one to show.", num_devices);
		return;
	}

	sdi = devices->data;
	print_dev_line(sdi);

	if (sr_dev_open(sdi) != SR_OK) {
		g_critical("Failed to open device.");
		return;
	}

	if ((sr_config_list(sdi->driver, NULL, NULL, SR_CONF_SCAN_OPTIONS,
			&gvar_opts) == SR_OK)) {
		opts = g_variant_get_fixed_array(gvar_opts, &num_elements,
				sizeof(int32_t));
		printf("Supported driver options:\n");
		for (i = 0; i < num_elements; i++) {
			if (!(srci = sr_config_info_get(opts[i])))
				continue;
			printf("    %s\n", srci->id);
		}
		g_variant_unref(gvar_opts);
	}

	/* Selected channels and channel group may affect which options are
	 * returned, or which values for them. */
	select_channels(sdi);
	channel_group = select_channel_group(sdi);

	if ((sr_config_list(sdi->driver, sdi, channel_group, SR_CONF_DEVICE_OPTIONS,
			&gvar_opts)) != SR_OK)
		/* Driver supports no device instance options. */
		return;

	if (sdi->channel_groups) {
		printf("Channel groups:\n");
		for (cgl = sdi->channel_groups; cgl; cgl = cgl->next) {
			cg = cgl->data;
			printf("    %s: channel%s", cg->name,
					g_slist_length(cg->channels) > 1 ? "s" : "");
			for (chl = cg->channels; chl; chl = chl->next) {
				ch = chl->data;
				printf(" %s", ch->name);
			}
			printf("\n");
		}
	}

	printf("Supported configuration options");
	if (sdi->channel_groups) {
		if (!channel_group)
			printf(" across all channel groups");
		else
			printf(" on channel group %s", channel_group->name);
	}
	printf(":\n");
	opts = g_variant_get_fixed_array(gvar_opts, &num_opts, sizeof(int32_t));
	for (o = 0; o < num_opts; o++) {
		if (!(srci = sr_config_info_get(opts[o])))
			continue;

		if (srci->key == SR_CONF_TRIGGER_TYPE) {
			if (sr_config_list(sdi->driver, sdi, channel_group, srci->key,
					&gvar) != SR_OK) {
				printf("\n");
				continue;
			}
			charopts = g_variant_get_string(gvar, NULL);
			printf("    Supported triggers: ");
			while (*charopts) {
				printf("%c ", *charopts);
				charopts++;
			}
			printf("\n");
			g_variant_unref(gvar);

		} else if (srci->key == SR_CONF_LIMIT_SAMPLES) {
			/* If implemented in config_list(), this denotes the
			 * maximum number of samples a device can send. This
			 * really applies only to logic analyzers, and then
			 * only to those that don't support compression, or
			 * have it turned off by default. The values returned
			 * are the low/high limits. */
			if (sr_config_list(sdi->driver, sdi, channel_group, srci->key,
					&gvar) != SR_OK) {
				continue;
			}
			g_variant_get(gvar, "(tt)", &low, &high);
			g_variant_unref(gvar);
			printf("    Maximum number of samples: %"PRIu64"\n", high);

		} else if (srci->key == SR_CONF_SAMPLERATE) {
			/* Supported samplerates */
			printf("    %s", srci->id);
			if (sr_config_list(sdi->driver, sdi, channel_group, SR_CONF_SAMPLERATE,
					&gvar_dict) != SR_OK) {
				printf("\n");
				continue;
			}
			if ((gvar_list = g_variant_lookup_value(gvar_dict,
					"samplerates", G_VARIANT_TYPE("at")))) {
				uint64 = g_variant_get_fixed_array(gvar_list,
						&num_elements, sizeof(uint64_t));
				printf(" - supported samplerates:\n");
				for (i = 0; i < num_elements; i++) {
					if (!(s = sr_samplerate_string(uint64[i])))
						continue;
					printf("      %s\n", s);
					g_free(s);
				}
				g_variant_unref(gvar_list);
			} else if ((gvar_list = g_variant_lookup_value(gvar_dict,
					"samplerate-steps", G_VARIANT_TYPE("at")))) {
				uint64 = g_variant_get_fixed_array(gvar_list,
						&num_elements, sizeof(uint64_t));
				/* low */
				if (!(s = sr_samplerate_string(uint64[0])))
					continue;
				printf(" (%s", s);
				g_free(s);
				/* high */
				if (!(s = sr_samplerate_string(uint64[1])))
					continue;
				printf(" - %s", s);
				g_free(s);
				/* step */
				if (!(s = sr_samplerate_string(uint64[2])))
					continue;
				printf(" in steps of %s)\n", s);
				g_free(s);
				g_variant_unref(gvar_list);
			}
			g_variant_unref(gvar_dict);

		} else if (srci->key == SR_CONF_BUFFERSIZE) {
			/* Supported buffer sizes */
			printf("    %s", srci->id);
			if (sr_config_list(sdi->driver, sdi, channel_group,
					SR_CONF_BUFFERSIZE, &gvar_list) != SR_OK) {
				printf("\n");
				continue;
			}
			uint64 = g_variant_get_fixed_array(gvar_list,
					&num_elements, sizeof(uint64_t));
			printf(" - supported buffer sizes:\n");
			for (i = 0; i < num_elements; i++)
				printf("      %"PRIu64"\n", uint64[i]);
			g_variant_unref(gvar_list);

		} else if (srci->key == SR_CONF_TIMEBASE) {
			/* Supported time bases */
			printf("    %s", srci->id);
			if (sr_config_list(sdi->driver, sdi, channel_group,
					SR_CONF_TIMEBASE, &gvar_list) != SR_OK) {
				printf("\n");
				continue;
			}
			printf(" - supported time bases:\n");
			num_elements = g_variant_n_children(gvar_list);
			for (i = 0; i < num_elements; i++) {
				gvar = g_variant_get_child_value(gvar_list, i);
				g_variant_get(gvar, "(tt)", &p, &q);
				s = sr_period_string(p * q);
				printf("      %s\n", s);
				g_free(s);
			}
			g_variant_unref(gvar_list);

		} else if (srci->key == SR_CONF_VDIV) {
			/* Supported volts/div values */
			printf("    %s", srci->id);
			if (sr_config_list(sdi->driver, sdi, channel_group,
					SR_CONF_VDIV, &gvar_list) != SR_OK) {
				printf("\n");
				continue;
			}
			printf(" - supported volts/div:\n");
			num_elements = g_variant_n_children(gvar_list);
			for (i = 0; i < num_elements; i++) {
				gvar = g_variant_get_child_value(gvar_list, i);
				g_variant_get(gvar, "(tt)", &p, &q);
				s = sr_voltage_string(p, q);
				printf("      %s\n", s);
				g_free(s);
			}
			g_variant_unref(gvar_list);

		} else if (srci->datatype == SR_T_STRING) {
			printf("    %s: ", srci->id);
			if (sr_config_get(sdi->driver, sdi, channel_group, srci->key,
					&gvar) == SR_OK) {
				tmp_str = g_strdup(g_variant_get_string(gvar, NULL));
				g_variant_unref(gvar);
			} else
				tmp_str = NULL;

			if (sr_config_list(sdi->driver, sdi, channel_group, srci->key,
					&gvar) != SR_OK) {
				printf("\n");
				continue;
			}

			stropts = g_variant_get_strv(gvar, &num_elements);
			for (i = 0; i < num_elements; i++) {
				if (i)
					printf(", ");
				printf("%s", stropts[i]);
				if (tmp_str && !strcmp(tmp_str, stropts[i]))
					printf(" (current)");
			}
			printf("\n");
			g_free(stropts);
			g_free(tmp_str);
			g_variant_unref(gvar);

		} else if (srci->datatype == SR_T_UINT64_RANGE) {
			printf("    %s: ", srci->id);
			if (sr_config_list(sdi->driver, sdi, channel_group, srci->key,
					&gvar_list) != SR_OK) {
				printf("\n");
				continue;
			}

			if (sr_config_get(sdi->driver, sdi, NULL, srci->key, &gvar) == SR_OK) {
				g_variant_get(gvar, "(tt)", &cur_low, &cur_high);
				g_variant_unref(gvar);
			} else {
				cur_low = 0;
				cur_high = 0;
			}

			num_elements = g_variant_n_children(gvar_list);
			for (i = 0; i < num_elements; i++) {
				gvar = g_variant_get_child_value(gvar_list, i);
				g_variant_get(gvar, "(tt)", &low, &high);
				g_variant_unref(gvar);
				if (i)
					printf(", ");
				printf("%"PRIu64"-%"PRIu64, low, high);
				if (low == cur_low && high == cur_high)
					printf(" (current)");
			}
			printf("\n");
			g_variant_unref(gvar_list);

		} else if (srci->datatype == SR_T_BOOL) {
			printf("    %s: ", srci->id);
			if (sr_config_get(sdi->driver, sdi, NULL, srci->key,
					&gvar) == SR_OK) {
				if (g_variant_get_boolean(gvar))
					printf("on (current), off\n");
				else
					printf("on, off (current)\n");
				g_variant_unref(gvar);
			} else
				printf("on, off\n");

		} else if (srci->datatype == SR_T_DOUBLE_RANGE) {
			printf("    %s: ", srci->id);
			if (sr_config_list(sdi->driver, sdi, channel_group, srci->key,
					&gvar_list) != SR_OK) {
				printf("\n");
				continue;
			}

			if (sr_config_get(sdi->driver, sdi, NULL, srci->key, &gvar) == SR_OK) {
				g_variant_get(gvar, "(dd)", &dcur_low, &dcur_high);
				g_variant_unref(gvar);
			} else {
				dcur_low = 0;
				dcur_high = 0;
			}

			num_elements = g_variant_n_children(gvar_list);
			for (i = 0; i < num_elements; i++) {
				gvar = g_variant_get_child_value(gvar_list, i);
				g_variant_get(gvar, "(dd)", &dlow, &dhigh);
				g_variant_unref(gvar);
				if (i)
					printf(", ");
				printf("%.1f-%.1f", dlow, dhigh);
				if (dlow == dcur_low && dhigh == dcur_high)
					printf(" (current)");
			}
			printf("\n");
			g_variant_unref(gvar_list);

		} else {

			/* Everything else */
			printf("    %s\n", srci->id);
		}
	}
	g_variant_unref(gvar_opts);

	sr_dev_close(sdi);
	g_slist_free(devices);

}

#ifdef HAVE_SRD
void show_pd_detail(void)
{
	GSList *l, *ll, *ol;
	struct srd_decoder *dec;
	struct srd_decoder_option *o;
	char **pdtokens, **pdtok, *optsep, **ann, *val, *doc;
	struct srd_channel *pdch;
	struct srd_decoder_annotation_row *r;

	pdtokens = g_strsplit(opt_pds, ",", -1);
	for (pdtok = pdtokens; *pdtok; pdtok++) {
		/* Strip options. */
		if ((optsep = strchr(*pdtok, ':')))
			*optsep = '\0';
		if (!(dec = srd_decoder_get_by_id(*pdtok))) {
			g_critical("Protocol decoder %s not found.", *pdtok);
			return;
		}
		printf("ID: %s\nName: %s\nLong name: %s\nDescription: %s\n",
				dec->id, dec->name, dec->longname, dec->desc);
		printf("License: %s\n", dec->license);
		printf("Annotation classes:\n");
		if (dec->annotations) {
			for (l = dec->annotations; l; l = l->next) {
				ann = l->data;
				printf("- %s: %s\n", ann[0], ann[1]);
			}
		} else {
			printf("None.\n");
		}
		printf("Annotation rows:\n");
		if (dec->annotation_rows) {
			for (l = dec->annotation_rows; l; l = l->next) {
				r = l->data;
				printf("- %s (%s): ", r->id, r->desc);
				for (ll = r->ann_classes; ll; ll = ll->next)
					printf("%d ", GPOINTER_TO_INT(ll->data));
				printf("\n");
			}
		} else {
			printf("None.\n");
		}
		printf("Required channels:\n");
		if (dec->channels) {
			for (l = dec->channels; l; l = l->next) {
				pdch = l->data;
				printf("- %s (%s): %s\n",
				       pdch->id, pdch->name, pdch->desc);
			}
		} else {
			printf("None.\n");
		}
		printf("Optional channels:\n");
		if (dec->opt_channels) {
			for (l = dec->opt_channels; l; l = l->next) {
				pdch = l->data;
				printf("- %s (%s): %s\n",
				       pdch->id, pdch->name, pdch->desc);
			}
		} else {
			printf("None.\n");
		}
		printf("Options:\n");
		if (dec->options) {
			for (l = dec->options; l; l = l->next) {
				o = l->data;
				printf("- %s: %s (", o->id, o->desc);
				for (ol = o->values; ol; ol = ol->next) {
					val = g_variant_print(ol->data, FALSE);
					printf("%s, ", val);
					g_free(val);
				}
				val = g_variant_print(o->def, FALSE);
				printf("default %s)\n", val);
				g_free(val);
			}
		} else {
			printf("None.\n");
		}
		if ((doc = srd_decoder_doc_get(dec))) {
			printf("Documentation:\n%s\n",
			       doc[0] == '\n' ? doc + 1 : doc);
			g_free(doc);
		}
	}

	g_strfreev(pdtokens);
}
#endif

