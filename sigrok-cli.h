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

#include "config.h"
#ifdef HAVE_SRD
/* First, so we avoid a _POSIX_C_SOURCE warning. */
#include <libsigrokdecode/libsigrokdecode.h>
#endif
#include <libsigrok/libsigrok.h>

#define DEFAULT_OUTPUT_FORMAT "bits:width=64"
#define SAVE_CHUNK_SIZE 524288

/* main.c */
int select_channels(struct sr_dev_inst *sdi);

/* show.c */
void show_version(void);
void show_dev_list(void);
void show_dev_detail(void);
void show_pd_detail(void);

/* device.c */
GSList *device_scan(void);
struct sr_channel_group *select_channel_group(struct sr_dev_inst *sdi);

/* session.c */
void datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data);
int opt_to_gvar(char *key, char *value, struct sr_config *src);
int set_dev_options(struct sr_dev_inst *sdi, GHashTable *args);
void run_session(void);
void save_chunk_logic(uint8_t *data, uint64_t data_len, int unitsize);

/* input.c */
void load_input_file(void);

/* decode.c */
#ifdef HAVE_SRD
int register_pds(const char *opt_pds, char *opt_pd_annotations);
int setup_pd_stack(char *opt_pds, char *opt_pd_stack, char *opt_pd_annotations);
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
GHashTable *parse_generic_arg(const char *arg, gboolean sep_first);
int canon_cmp(const char *str1, const char *str2);

/* anykey.c */
void add_anykey(void);
void clear_anykey(void);

#endif
