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
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "sigrok-cli.h"

#ifdef HAVE_SRD
static GHashTable *pd_ann_visible = NULL;
static GHashTable *pd_meta_visible = NULL;
static GHashTable *pd_binary_visible = NULL;
static GHashTable *pd_channel_maps = NULL;

extern struct srd_session *srd_sess;

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

static int move_hash_element(GHashTable *src, GHashTable *dest, void *key)
{
	void *orig_key, *value;

	if (!g_hash_table_lookup_extended(src, key, &orig_key, &value))
		/* Not specified. */
		return FALSE;
	g_hash_table_steal(src, orig_key);
	g_hash_table_insert(dest, orig_key, value);

	return TRUE;
}

static GHashTable *extract_channel_map(struct srd_decoder *dec, GHashTable *hash)
{
	GHashTable *channel_map;
	struct srd_channel *pdch;
	GSList *l;

	channel_map = g_hash_table_new_full(g_str_hash, g_str_equal,
					  g_free, g_free);

	for (l = dec->channels; l; l = l->next) {
		pdch = l->data;
		move_hash_element(hash, channel_map, pdch->id);
	}
	for (l = dec->opt_channels; l; l = l->next) {
		pdch = l->data;
		move_hash_element(hash, channel_map, pdch->id);
	}

	return channel_map;
}

/*
 * Register the given PDs for this session.
 * Accepts a string of the form: "spi:sck=3:sdata=4,spi:sck=3:sdata=5"
 * That will instantiate two SPI decoders on the clock but different data
 * lines.
 */
int register_pds(const char *opt_pds, char *opt_pd_annotations)
{
	struct srd_decoder *dec;
	GHashTable *pd_opthash, *options, *channels;
	GList *leftover, *l;
	struct srd_decoder_inst *di;
	int ret;
	char **pdtokens, **pdtok, *pd_name;

	pd_ann_visible = g_hash_table_new_full(g_str_hash, g_str_equal,
					       g_free, NULL);
	ret = 0;
	pd_name = NULL;
	pd_opthash = options = channels = pd_channel_maps = NULL;
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
		if (!(dec = srd_decoder_get_by_id(pd_name))) {
			g_critical("Failed to get decoder %s by id.", pd_name);
			ret = 1;
			break;
		}

		/* Convert decoder option and channel values to GVariant. */
		if (!opts_to_gvar(dec, pd_opthash, &options)) {
			ret = 1;
			break;
		}
		channels = extract_channel_map(dec, pd_opthash);

		if (g_hash_table_size(pd_opthash) > 0) {
			leftover = g_hash_table_get_keys(pd_opthash);
			for (l = leftover; l; l = l->next)
				g_critical("Unknown option or channel '%s'", (char *)l->data);
			g_list_free(leftover);
			break;
		}

		if (!(di = srd_inst_new(srd_sess, pd_name, options))) {
			g_critical("Failed to instantiate protocol decoder %s.", pd_name);
			ret = 1;
			break;
		}

		if (pdtok == pdtokens) {
			/*
			 * Save the channel setup for later, but only on the
			 * first decoder (stacked decoders don't get channels).
			 */
			pd_channel_maps = g_hash_table_new_full(g_str_hash,
					g_str_equal, g_free, (GDestroyNotify)g_hash_table_destroy);
			g_hash_table_insert(pd_channel_maps, g_strdup(di->inst_id), channels);
			channels = NULL;
		}

		/*
		 * If no annotation list was specified, add them all in now.
		 * This will be pared down later to leave only the last PD
		 * in the stack.
		 */
		if (!opt_pd_annotations)
			g_hash_table_insert(pd_ann_visible, g_strdup(di->inst_id),
					g_slist_append(NULL, GINT_TO_POINTER(-1)));
	}

	g_strfreev(pdtokens);
	if (pd_opthash)
		g_hash_table_destroy(pd_opthash);
	if (options)
		g_hash_table_destroy(options);
	if (channels)
		g_hash_table_destroy(channels);
	g_free(pd_name);

	return ret;
}

static void map_pd_inst_channels(void *key, void *value, void *user_data)
{
	GHashTable *channel_map;
	GHashTable *channel_indices;
	GSList *channel_list;
	struct srd_decoder_inst *di;
	GVariant *var;
	void *channel_id;
	void *channel_target;
	struct sr_channel *ch;
	GHashTableIter iter;

	channel_map = value;
	channel_list = user_data;

	di = srd_inst_find_by_id(srd_sess, key);
	if (!di) {
		g_critical("Protocol decoder instance \"%s\" not found.",
			   (char *)key);
		return;
	}
	channel_indices = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					      (GDestroyNotify)g_variant_unref);

	g_hash_table_iter_init(&iter, channel_map);
	while (g_hash_table_iter_next(&iter, &channel_id, &channel_target)) {
		ch = find_channel(channel_list, channel_target);
		if (!ch) {
			g_printerr("cli: No channel with name \"%s\" found.\n",
				   (char *)channel_target);
			continue;
		}
		if (!ch->enabled)
			g_printerr("cli: Target channel \"%s\" not enabled.\n",
				   (char *)channel_target);

		var = g_variant_new_int32(ch->index);
		g_variant_ref_sink(var);
		g_hash_table_insert(channel_indices, g_strdup(channel_id), var);
	}

	srd_inst_channel_set_all(di, channel_indices);
}

void map_pd_channels(struct sr_dev_inst *sdi)
{
	GSList *channels;

	channels = sr_dev_inst_channels_get(sdi);

	if (pd_channel_maps) {
		g_hash_table_foreach(pd_channel_maps, &map_pd_inst_channels,
				     channels);
		g_hash_table_destroy(pd_channel_maps);
		pd_channel_maps = NULL;
	}
}

int setup_pd_stack(char *opt_pds, char *opt_pd_annotations)
{
	struct srd_decoder_inst *di_from, *di_to;
	int ret, i;
	char **pds, **ids;

	/* Set up the protocol decoder stack. */
	pds = g_strsplit(opt_pds, ",", 0);
	if (g_strv_length(pds) > 1) {
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

			/*
			 * Don't show annotation from this PD. Only the last PD in
			 * the stack will be left on the annotation list (unless
			 * the annotation list was specifically provided).
			 */
			if (!opt_pd_annotations)
				g_hash_table_remove(pd_ann_visible, di_from->inst_id);

			di_from = di_to;
		}
	}
	g_strfreev(pds);

	return 0;
}

int setup_pd_annotations(char *opt_pd_annotations)
{
	GSList *l, *l_ann;
	struct srd_decoder *dec;
	int ann_class;
	char **pds, **pdtok, **keyval, **annlist, **ann, **ann_descr;

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
		if (g_strv_length(keyval) == 2 && keyval[1][0] != '\0') {
			annlist = g_strsplit(keyval[1], ":", 0);
			for (ann = annlist; *ann && **ann; ann++) {
				ann_class = 0;
				for (l = dec->annotations; l; l = l->next, ann_class++) {
					ann_descr = l->data;
					if (!canon_cmp(ann_descr[0], *ann))
						/* Found it. */
						break;
				}
				if (!l) {
					g_critical("Annotation '%s' not found "
							"for protocol decoder '%s'.", *ann, keyval[0]);
					return 1;
				}
				l_ann = g_hash_table_lookup(pd_ann_visible, keyval[0]);
				l_ann = g_slist_append(l_ann, GINT_TO_POINTER(ann_class));
				g_hash_table_replace(pd_ann_visible, g_strdup(keyval[0]), l_ann);
				g_debug("cli: Showing protocol decoder %s annotation "
						"class %d (%s).", keyval[0], ann_class, ann_descr[0]);
			}
		} else {
			/* No class specified: show all of them. */
				g_hash_table_insert(pd_ann_visible, g_strdup(keyval[0]),
						g_slist_append(NULL, GINT_TO_POINTER(-1)));
			g_debug("cli: Showing all annotation classes for protocol "
					"decoder %s.", keyval[0]);
		}
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
	char **pds, **pdtok, **keyval, **bin_name;

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
				if (!strcmp(bin_name[0], keyval[1]))
					/* Found it. */
					break;
			}
			if (!l) {
				g_critical("binary output '%s' not found "
						"for protocol decoder '%s'.", keyval[1], keyval[0]);
				return 1;
			}
			g_debug("cli: Showing protocol decoder %s binary class "
					"%d (%s).", keyval[0], bin_class, bin_name[0]);
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
	struct srd_decoder *dec;
	struct srd_proto_data_annotation *pda;
	GSList *ann_list, *l;
	int i;
	char **ann_descr;
	gboolean show;

	(void)cb_data;

	if (!pd_ann_visible)
		return;

	if (!g_hash_table_lookup_extended(pd_ann_visible, pdata->pdo->di->inst_id,
			NULL, (void **)&ann_list))
		/* Not in the list of PDs whose annotations we're showing. */
		return;

	dec = pdata->pdo->di->decoder;
	pda = pdata->data;
	show = FALSE;
	for (l = ann_list; l; l = l->next) {
		if (GPOINTER_TO_INT(l->data) == -1
				|| GPOINTER_TO_INT(l->data) == pda->ann_class) {
			show = TRUE;
			break;
		}
	}
	if (!show)
		return;

	if (opt_loglevel <= SR_LOG_WARN) {
		/* Show only the longest annotation. */
		printf("%s", pda->ann_text[0]);
	} else if (opt_loglevel >= SR_LOG_INFO) {
		/* Sample numbers and quotes around the longest annotation. */
		printf("%"PRIu64"-%"PRIu64"", pdata->start_sample, pdata->end_sample);
		if (opt_loglevel == SR_LOG_INFO) {
			printf(" \"%s\"", pda->ann_text[0]);
		} else {
			/* Protocol decoder id, annotation class,
			 * all annotation strings. */
			ann_descr = g_slist_nth_data(dec->annotations, pda->ann_class);
			printf(" %s: %s:", pdata->pdo->proto_id, ann_descr[0]);
			for (i = 0; pda->ann_text[i]; i++)
				printf(" \"%s\"", pda->ann_text[i]);
		}
	}
	printf("\n");
	fflush(stdout);
}

void show_pd_meta(struct srd_proto_data *pdata, void *cb_data)
{
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
