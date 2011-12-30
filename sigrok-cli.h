/*
 * This file is part of the sigrok project.
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

#ifndef SIGROK_SIGROK_CLI_H
#define SIGROK_SIGROK_CLI_H

/* sigrok-cli.c */
int num_real_devices(void);

/* parsers.c */
char **parse_probestring(int max_probes, const char *probestring);
char **sr_parse_triggerstring(struct sr_device *device, const char *triggerstring);
GHashTable *parse_generic_arg(const char *arg);
struct sr_device *parse_devicestring(const char *devicestring);
uint64_t sr_parse_timestring(const char *timestring);

/* anykey.c */
void add_anykey(void);
void clear_anykey(void);

#endif
