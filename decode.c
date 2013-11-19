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

#ifdef HAVE_SRD
static GHashTable *pd_ann_visible = NULL;
static GHashTable *pd_meta_visible = NULL;
static GHashTable *pd_binary_visible = NULL;

extern struct srd_session *srd_sess;
extern gint opt_loglevel;


static int opts_to_gvar(struct srd_decoder *dec, GHashTable *hash,
		GHashTable **options)
{
	struct srd_decoder_option *o;
	GSList *optl;
	GVariant *gvar;
	gint64 val_int;
	int ret;
	char *val_str, *conv;

	ret = TRUE;
	*options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			(GDestroyNotify)g_variant_unref);

	for (optl = dec->options; optl; optl = optl->next) {
		o = optl->data;
		if (!(val_str = g_hash_table_lookup(hash, o->id)))
			/* Not specified. */
			continue;
		if (g_variant_is_of_type(o->def, G_VARIANT_TYPE_STRING)) {
			gvar = g_variant_new_string(val_str);
		} else if (g_variant_is_of_type(o->def, G_VARIANT_TYPE_INT64)) {
			val_int = strtoll(val_str, &conv, 0);
			if (!conv || conv == val_str) {
				g_critical("Protocol decoder '%s' option '%s' "
						"requires a number.", dec->name, o->id);
				ret = FALSE;
				break;
			}
			gvar = g_variant_new_int64(val_int);
		} else {
			g_critical("Unsupported type for option '%s' (%s)",
					o->id, g_variant_get_type_string(o->def));
			ret = FALSE;
			break;
		}
		g_variant_ref_sink(gvar);
		g_hash_table_insert(*options, g_strdup(o->id), gvar);
		g_hash_table_remove(hash, o->id);
	}

	return ret;
}

static int probes_to_gvar(struct srd_decoder *dec, GHashTable *hash,
		GHashTable **probes)
{
	struct srd_probe *p;
	GSList *all_probes, *l;
	GVariant *gvar;
	gint32 val_int;
	int ret;
	char *val_str, *conv;

	ret = TRUE;
	*probes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			(GDestroyNotify)g_variant_unref);

	all_probes = g_slist_copy(dec->probes);
	all_probes = g_slist_concat(all_probes, g_slist_copy(dec->opt_probes));
	for (l = all_probes; l; l = l->next) {
		p = l->data;
		if (!(val_str = g_hash_table_lookup(hash, p->id)))
			/* Not specified. */
			continue;
		val_int = strtoll(val_str, &conv, 10);
		if (!conv || conv == val_str) {
			g_critical("Protocol decoder '%s' probes '%s' "
					"is not a number.", dec->name, p->id);
			ret = FALSE;
			break;
		}
		gvar = g_variant_new_int32(val_int);
		g_variant_ref_sink(gvar);
		g_hash_table_insert(*probes, g_strdup(p->id), gvar);
		g_hash_table_remove(hash, p->id);
	}
	g_slist_free(all_probes);

	return ret;
}

/* Register the given PDs for this session.
 * Accepts a string of the form: "spi:sck=3:sdata=4,spi:sck=3:sdata=5"
 * That will instantiate two SPI decoders on the clock but different data
 * lines.
 */
int register_pds(const char *opt_pds, char *opt_pd_annotations)
{
	struct srd_decoder *dec;
	GHashTable *pd_opthash, *options, *probes;
	GList *leftover, *l;
	struct srd_decoder_inst *di;
	int ret;
	char **pdtokens, **pdtok, *pd_name;

	pd_ann_visible = g_hash_table_new_full(g_str_hash, g_int_equal,
			g_free, NULL);
	ret = 0;
	pd_name = NULL;
	pd_opthash = options = probes = NULL;
	pdtokens = g_strsplit(opt_pds, ",", 0);
	for (pdtok = pdtokens; *pdtok; pdtok++) {
		if (!(pd_opthash = parse_generic_arg(*pdtok, TRUE))) {
			g_critical("Invalid protocol decoder option '%s'.", *pdtok);
			break;
		}

		pd_name = g_strdup(g_hash_table_lookup(pd_opthash, "sigrok_key"));
		g_hash_table_remove(pd_opthash, "sigrok_key");
		if (srd_decoder_load(pd_name) != SRD_OK) {
			g_critical("Failed to load protocol decoder %s.", pd_name);
			ret = 1;
			break;
		}
		dec = srd_decoder_get_by_id(pd_name);

		/* Convert decoder option and probe values to GVariant. */
		if (!opts_to_gvar(dec, pd_opthash, &options)) {
			ret = 1;
			break;
		}
		if (!probes_to_gvar(dec, pd_opthash, &probes)) {
			ret = 1;
			break;
		}
		if (g_hash_table_size(pd_opthash) > 0) {
			leftover = g_hash_table_get_keys(pd_opthash);
			for (l = leftover; l; l = l->next)
				g_critical("Unknown option or probe '%s'", (char *)l->data);
			g_list_free(leftover);
			break;
		}

		if (!(di = srd_inst_new(srd_sess, pd_name, options))) {
			g_critical("Failed to instantiate protocol decoder %s.", pd_name);
			ret = 1;
			break;
		}

		/* If no annotation list was specified, add them all in now.
		 * This will be pared down later to leave only the last PD
		 * in the stack.
		 */
		if (!opt_pd_annotations)
			g_hash_table_insert(pd_ann_visible,
					    g_strdup(di->inst_id), GINT_TO_POINTER(-1));

		/* Remap the probes if needed. */
		if (srd_inst_probe_set_all(di, probes) != SRD_OK) {
			ret = 1;
			break;
		}
	}

	g_strfreev(pdtokens);
	if (pd_opthash)
		g_hash_table_destroy(pd_opthash);
	if (options)
		g_hash_table_destroy(options);
	if (probes)
		g_hash_table_destroy(probes);
	if (pd_name)
		g_free(pd_name);

	return ret;
}

int setup_pd_stack(char *opt_pds, char *opt_pd_stack, char *opt_pd_annotations)
{
	struct srd_decoder_inst *di_from, *di_to;
	int ret, i;
	char **pds, **ids;

	/* Set up the protocol decoder stack. */
	pds = g_strsplit(opt_pds, ",", 0);
	if (g_strv_length(pds) > 1) {
		if (opt_pd_stack) {
			/* A stack setup was specified, use that. */
			g_strfreev(pds);
			pds = g_strsplit(opt_pd_stack, ",", 0);
			if (g_strv_length(pds) < 2) {
				g_strfreev(pds);
				g_critical("Specify at least two protocol decoders to stack.");
				return 1;
			}
		}

		/* First PD goes at the bottom of the stack. */
		ids = g_strsplit(pds[0], ":", 0);
		if (!(di_from = srd_inst_find_by_id(srd_sess, ids[0]))) {
			g_strfreev(ids);
			g_critical("Cannot stack protocol decoder '%s': "
					"instance not found.", pds[0]);
			return 1;
		}
		g_strfreev(ids);

		/* Every subsequent PD goes on top. */
		for (i = 1; pds[i]; i++) {
			ids = g_strsplit(pds[i], ":", 0);
			if (!(di_to = srd_inst_find_by_id(srd_sess, ids[0]))) {
				g_strfreev(ids);
				g_critical("Cannot stack protocol decoder '%s': "
						"instance not found.", pds[i]);
				return 1;
			}
			g_strfreev(ids);
			if ((ret = srd_inst_stack(srd_sess, di_from, di_to)) != SRD_OK)
				return 1;

			/* Don't show annotation from this PD. Only the last PD in
			 * the stack will be left on the annotation list (unless
			 * the annotation list was specifically provided).
			 */
			if (!opt_pd_annotations)
				g_hash_table_remove(pd_ann_visible,
						    di_from->inst_id);

			di_from = di_to;
		}
	}
	g_strfreev(pds);

	return 0;
}

int setup_pd_annotations(char *opt_pd_annotations)
{
	GSList *l;
	struct srd_decoder *dec;
	int ann_class;
	char **pds, **pdtok, **keyval, **ann_descr;

	/* Set up custom list of PDs and annotations to show. */
	pds = g_strsplit(opt_pd_annotations, ",", 0);
	for (pdtok = pds; *pdtok && **pdtok; pdtok++) {
		keyval = g_strsplit(*pdtok, "=", 0);
		if (!(dec = srd_decoder_get_by_id(keyval[0]))) {
			g_critical("Protocol decoder '%s' not found.", keyval[0]);
			return 1;
		}
		if (!dec->annotations) {
			g_critical("Protocol decoder '%s' has no annotations.", keyval[0]);
			return 1;
		}
		ann_class = 0;
		if (g_strv_length(keyval) == 2) {
			for (l = dec->annotations; l; l = l->next, ann_class++) {
				ann_descr = l->data;
				if (!canon_cmp(ann_descr[0], keyval[1]))
					/* Found it. */
					break;
			}
			if (!l) {
				g_critical("Annotation '%s' not found "
						"for protocol decoder '%s'.", keyval[1], keyval[0]);
				return 1;
			}
			g_debug("cli: Showing protocol decoder %s annotation "
					"class %d (%s).", keyval[0], ann_class, ann_descr[0]);
		} else {
			/* No class specified: show all of them. */
			ann_class = -1;
			g_debug("cli: Showing all annotation classes for protocol "
					"decoder %s.", keyval[0]);
		}
		g_hash_table_insert(pd_ann_visible, g_strdup(keyval[0]), GINT_TO_POINTER(ann_class));
		g_strfreev(keyval);
	}
	g_strfreev(pds);

	return 0;
}

int setup_pd_meta(char *opt_pd_meta)
{
	struct srd_decoder *dec;
	char **pds, **pdtok;

	pd_meta_visible = g_hash_table_new_full(g_str_hash, g_int_equal,
			g_free, NULL);
	pds = g_strsplit(opt_pd_meta, ",", 0);
	for (pdtok = pds; *pdtok && **pdtok; pdtok++) {
		if (!(dec = srd_decoder_get_by_id(*pdtok))) {
			g_critical("Protocol decoder '%s' not found.", *pdtok);
			return 1;
		}
		g_debug("cli: Showing protocol decoder meta output from '%s'.", *pdtok);
		g_hash_table_insert(pd_meta_visible, g_strdup(*pdtok), NULL);
	}
	g_strfreev(pds);

	return 0;
}

int setup_pd_binary(char *opt_pd_binary)
{
	GSList *l;
	struct srd_decoder *dec;
	int bin_class;
	char **pds, **pdtok, **keyval, *bin_name;

	pd_binary_visible = g_hash_table_new_full(g_str_hash, g_int_equal,
			g_free, NULL);
	pds = g_strsplit(opt_pd_binary, ",", 0);
	for (pdtok = pds; *pdtok && **pdtok; pdtok++) {
		keyval = g_strsplit(*pdtok, "=", 0);
		if (!(dec = srd_decoder_get_by_id(keyval[0]))) {
			g_critical("Protocol decoder '%s' not found.", keyval[0]);
			return 1;
		}
		if (!dec->binary) {
			g_critical("Protocol decoder '%s' has no binary output.", keyval[0]);
			return 1;
		}
		bin_class = 0;
		if (g_strv_length(keyval) == 2) {
			for (l = dec->binary; l; l = l->next, bin_class++) {
				bin_name = l->data;
				if (!canon_cmp(bin_name, keyval[1]))
					/* Found it. */
					break;
			}
			if (!l) {
				g_critical("binary output '%s' not found "
						"for protocol decoder '%s'.", keyval[1], keyval[0]);
				return 1;
			}
			g_debug("cli: Showing protocol decoder %s binary class "
					"%d (%s).", keyval[0], bin_class, bin_name);
		} else {
			/* No class specified: output all of them. */
			bin_class = -1;
			g_debug("cli: Showing all binary classes for protocol "
					"decoder %s.", keyval[0]);
		}
		g_hash_table_insert(pd_binary_visible, g_strdup(keyval[0]), GINT_TO_POINTER(bin_class));
		g_strfreev(keyval);
	}
	g_strfreev(pds);

	return 0;
}

void show_pd_annotations(struct srd_proto_data *pdata, void *cb_data)
{
	struct srd_proto_data_annotation *pda;
	gpointer ann_format;
	int format;

	/* 'cb_data' is not used in this specific callback. */
	(void)cb_data;

	if (!pd_ann_visible)
		return;

	if (!g_hash_table_lookup_extended(pd_ann_visible, pdata->pdo->di->inst_id,
			NULL, &ann_format))
		/* Not in the list of PDs whose annotations we're showing. */
		return;

	format = GPOINTER_TO_INT(ann_format);
	pda = pdata->data;
	if (format != -1 && pda->ann_format != format)
		/* We don't want this particular format from the PD. */
		return;

	if (opt_loglevel > SR_LOG_WARN)
		printf("%"PRIu64"-%"PRIu64" ", pdata->start_sample, pdata->end_sample);
	printf("%s: ", pdata->pdo->proto_id);
	/* Show only the longest annotation. */
	printf("\"%s\" ", pda->ann_text[0]);
	printf("\n");
	fflush(stdout);
}

void show_pd_meta(struct srd_proto_data *pdata, void *cb_data)
{

	/* 'cb_data' is not used in this specific callback. */
	(void)cb_data;

	if (!g_hash_table_lookup_extended(pd_meta_visible,
			pdata->pdo->di->decoder->id, NULL, NULL))
		/* Not in the list of PDs whose meta output we're showing. */
		return;

	if (opt_loglevel > SR_LOG_WARN)
		printf("%"PRIu64"-%"PRIu64" ", pdata->start_sample, pdata->end_sample);
	printf("%s: ", pdata->pdo->proto_id);
	printf("%s: %s", pdata->pdo->meta_name, g_variant_print(pdata->data, FALSE));
	printf("\n");
	fflush(stdout);
}

void show_pd_binary(struct srd_proto_data *pdata, void *cb_data)
{
	struct srd_proto_data_binary *pdb;
	gpointer classp;
	int class;

	/* 'cb_data' is not used in this specific callback. */
	(void)cb_data;

	if (!g_hash_table_lookup_extended(pd_binary_visible,
			pdata->pdo->di->decoder->id, NULL, (void **)&classp))
		/* Not in the list of PDs whose meta output we're showing. */
		return;

	class = GPOINTER_TO_INT(classp);
	pdb = pdata->data;
	if (class != -1 && class != pdb->bin_class)
		/* Not showing this binary class. */
		return;

	/* Just send the binary output to stdout, no embellishments. */
	fwrite(pdb->data, pdb->size, 1, stdout);
	fflush(stdout);
}
#endif

