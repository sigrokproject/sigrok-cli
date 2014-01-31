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
#include <glib.h>
#include <string.h>

extern gint opt_loglevel;
extern gchar *opt_pds;

void show_version(void)
{
	struct sr_dev_driver **drivers;
	struct sr_input_format **inputs;
	struct sr_output_format **outputs;
	int i;
#ifdef HAVE_SRD
	struct srd_decoder *dec;
	const GSList *l;
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
	for (i = 0; drivers[i]; i++) {
		printf("  %-20s %s\n", drivers[i]->name, drivers[i]->longname);
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
	printf("  %-20s %s\n", "sigrok", "Default file output format");
	printf("\n");

#ifdef HAVE_SRD
	if (srd_init(NULL) == SRD_OK) {
		printf("Supported protocol decoders:\n");
		srd_decoder_load_all();
		for (l = srd_decoder_list(); l; l = l->next) {
			dec = l->data;
			printf("  %-20s %s\n", dec->id, dec->longname);
			/* Print protocol description upon "-l 3" or higher. */
			if (opt_loglevel >= SR_LOG_INFO)
				printf("  %-20s %s\n", "", dec->desc);
		}
		srd_exit();
	}
	printf("\n");
#endif
}

static void print_dev_line(const struct sr_dev_inst *sdi)
{
	struct sr_probe *probe;
	GSList *l;
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
	if (sdi->probes) {
		if (g_slist_length(sdi->probes) == 1) {
			probe = sdi->probes->data;
			g_string_append_printf(s, "with 1 probe: %s", probe->name);
		} else {
			g_string_append_printf(s, "with %d probes:", g_slist_length(sdi->probes));
			for (l = sdi->probes; l; l = l->next) {
				probe = l->data;
				g_string_append_printf(s, " %s", probe->name);
			}
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
	struct sr_probe *probe;
	struct sr_probe_group *probe_group, *pg;
	GSList *devices, *pgl, *prl;
	GVariant *gvar_opts, *gvar_dict, *gvar_list, *gvar;
	gsize num_opts, num_elements;
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

	/* Selected probes and probe group may affect which options are
	 * returned, or which values for them. */
	select_probes(sdi);
	probe_group = select_probe_group(sdi);

	if ((sr_config_list(sdi->driver, sdi, probe_group, SR_CONF_DEVICE_OPTIONS,
			&gvar_opts)) != SR_OK)
		/* Driver supports no device instance options. */
		return;

	if (sdi->probe_groups) {
		printf("Probe groups:\n");
		for (pgl = sdi->probe_groups; pgl; pgl = pgl->next) {
			pg = pgl->data;
			printf("    %s: channel%s", pg->name,
					g_slist_length(pg->probes) > 1 ? "s" : "");
			for (prl = pg->probes; prl; prl = prl->next) {
				probe = prl->data;
				printf(" %s", probe->name);
			}
			printf("\n");
		}
	}

	printf("Supported configuration options");
	if (sdi->probe_groups) {
		if (!probe_group)
			printf(" across all probe groups");
		else
			printf(" on probe group %s", probe_group->name);
	}
	printf(":\n");
	opts = g_variant_get_fixed_array(gvar_opts, &num_opts, sizeof(int32_t));
	for (o = 0; o < num_opts; o++) {
		if (!(srci = sr_config_info_get(opts[o])))
			continue;

		if (srci->key == SR_CONF_TRIGGER_TYPE) {
			if (sr_config_list(sdi->driver, sdi, probe_group, srci->key,
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
			if (sr_config_list(sdi->driver, sdi, probe_group, srci->key,
					&gvar) != SR_OK) {
				continue;
			}
			g_variant_get(gvar, "(tt)", &low, &high);
			g_variant_unref(gvar);
			printf("    Maximum number of samples: %"PRIu64"\n", high);

		} else if (srci->key == SR_CONF_SAMPLERATE) {
			/* Supported samplerates */
			printf("    %s", srci->id);
			if (sr_config_list(sdi->driver, sdi, probe_group, SR_CONF_SAMPLERATE,
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
			if (sr_config_list(sdi->driver, sdi, probe_group,
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
			if (sr_config_list(sdi->driver, sdi, probe_group,
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
			if (sr_config_list(sdi->driver, sdi, probe_group,
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

		} else if (srci->datatype == SR_T_CHAR) {
			printf("    %s: ", srci->id);
			if (sr_config_get(sdi->driver, sdi, probe_group, srci->key,
					&gvar) == SR_OK) {
				tmp_str = g_strdup(g_variant_get_string(gvar, NULL));
				g_variant_unref(gvar);
			} else
				tmp_str = NULL;

			if (sr_config_list(sdi->driver, sdi, probe_group, srci->key,
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
			if (sr_config_list(sdi->driver, sdi, probe_group, srci->key,
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
	GSList *l, *ll;
	struct srd_decoder *dec;
	struct srd_decoder_option *o;
	char **pdtokens, **pdtok, *optsep, **ann, *val, *doc;
	struct srd_probe *p;
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
		printf("Annotations:\n");
		if (dec->annotations) {
			for (l = dec->annotations; l; l = l->next) {
				ann = l->data;
				printf("- %s\n  %s\n", ann[0], ann[1]);
			}
		} else {
			printf("None.\n");
		}
		printf("Annotation rows:\n");
		if (dec->annotation_rows) {
			for (l = dec->annotation_rows; l; l = l->next) {
				r = l->data;
				printf("- %s (%s): ", r->desc, r->id);
				for (ll = r->ann_classes; ll; ll = ll->next)
					printf("%d ", GPOINTER_TO_INT(ll->data));
				printf("\n");
			}
		} else {
			printf("None.\n");
		}
		printf("Required probes:\n");
		if (dec->probes) {
			for (l = dec->probes; l; l = l->next) {
				p = l->data;
				printf("- %s (%s): %s\n",
				       p->name, p->id, p->desc);
			}
		} else {
			printf("None.\n");
		}
		printf("Optional probes:\n");
		if (dec->opt_probes) {
			for (l = dec->opt_probes; l; l = l->next) {
				p = l->data;
				printf("- %s (%s): %s\n",
				       p->name, p->id, p->desc);
			}
		} else {
			printf("None.\n");
		}
		printf("Options:\n");
		if (dec->options) {
			for (l = dec->options; l; l = l->next) {
				o = l->data;
				val = g_variant_print(o->def, FALSE);
				printf("- %s: %s (default %s)\n", o->id, o->desc, val);
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

