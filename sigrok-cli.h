/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#ifndef SIGROK_CLI_SIGROK_CLI_H
#define SIGROK_CLI_SIGROK_CLI_H

#ifdef HAVE_SRD
/* First, so we avoid a _POSIX_C_SOURCE warning. */
#include <libsigrokdecode/libsigrokdecode.h>
#endif
#include <libsigrok/libsigrok.h>

#define DEFAULT_OUTPUT_FORMAT_FILE "srzip"
#define DEFAULT_OUTPUT_FORMAT_NOFILE "bits:width=64"

/* main.c */
extern struct sr_context *sr_ctx;
int select_channels(struct sr_dev_inst *sdi);
int maybe_config_get(struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi, struct sr_channel_group *cg,
		uint32_t key, GVariant **gvar);
int maybe_config_get_and_show(struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi, struct sr_channel_group *cg,
		uint32_t key, GVariant **gvar);
int maybe_config_set(struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi, struct sr_channel_group *cg,
		uint32_t key, GVariant *gvar);
int maybe_config_list(struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi, struct sr_channel_group *cg,
		uint32_t key, GVariant **gvar);

/* show.c */
void show_version(void);
void show_supported(void);
void show_dev_list(void);
void show_dev_detail(void);
void show_pd_detail(void);
void show_input(void);
void show_output(void);
void show_transform(void);

/* device.c */
GSList *device_scan(void);
struct sr_channel_group *select_channel_group(struct sr_dev_inst *sdi);

/* session.c */
void datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data);
int opt_to_gvar(char *key, char *value, struct sr_config *src);
int set_dev_options(struct sr_dev_inst *sdi, GHashTable *args);
void run_session(void);

/* input.c */
void load_input_file(void);

/* decode.c */
#ifdef HAVE_SRD
int register_pds(gchar **all_pds, char *opt_pd_annotations);
int setup_pd_annotations(char *opt_pd_annotations);
int setup_pd_meta(char *opt_pd_meta);
int setup_pd_binary(char *opt_pd_binary);
void show_pd_annotations(struct srd_proto_data *pdata, void *cb_data);
void show_pd_meta(struct srd_proto_data *pdata, void *cb_data);
void show_pd_binary(struct srd_proto_data *pdata, void *cb_data);
void map_pd_channels(struct sr_dev_inst *sdi);
#endif

/* parsers.c */
struct sr_channel *find_channel(GSList *channellist, const char *channelname);
GSList *parse_channelstring(struct sr_dev_inst *sdi, const char *channelstring);
int parse_triggerstring(const struct sr_dev_inst *sdi, const char *s,
		struct sr_trigger **trigger);
GHashTable *parse_generic_arg(const char *arg, gboolean sep_first);
GHashTable *generic_arg_to_opt(const struct sr_option **opts, GHashTable *genargs);
int canon_cmp(const char *str1, const char *str2);
int parse_driver(char *arg, struct sr_dev_driver **driver, GSList **drvopts);

/* anykey.c */
void add_anykey(struct sr_session *session);
void clear_anykey(void);

/* options.c */
extern gboolean opt_version;
extern gboolean opt_list_supported;
extern gint opt_loglevel;
extern gboolean opt_scan_devs;
extern gboolean opt_wait_trigger;
extern gchar *opt_input_file;
extern gchar *opt_output_file;
extern gchar *opt_drv;
extern gchar *opt_config;
extern gchar *opt_channels;
extern gchar *opt_channel_group;
extern gchar *opt_triggers;
extern gchar **opt_pds;
#ifdef HAVE_SRD
extern gchar *opt_pd_annotations;
extern gchar *opt_pd_meta;
extern gchar *opt_pd_binary;
extern gboolean opt_pd_samplenum;
#endif
extern gchar *opt_input_format;
extern gchar *opt_output_format;
extern gchar *opt_transform_module;
extern gboolean opt_show;
extern gchar *opt_time;
extern gchar *opt_samples;
extern gchar *opt_frames;
extern gboolean opt_continuous;
extern gchar *opt_get;
extern gboolean opt_set;
int parse_options(int argc, char **argv);
void show_help(void);

#endif
