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

#include <config.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
#include <string.h>
#include <glib.h>
#include "sigrok-cli.h"

#ifdef _WIN32
static HANDLE stdin_handle;
static DWORD stdin_mode;
#else
static struct termios term_orig;
#endif
static unsigned int watch_id = 0;

static gboolean received_anykey(GIOChannel *source,
		GIOCondition condition, void *data)
{
	struct sr_session *session;

	(void)source;
	(void)condition;
	session = data;

	watch_id = 0;
	sr_session_stop(session);

	return G_SOURCE_REMOVE;
}

/* Turn off buffering on stdin and watch for input.
 */
void add_anykey(struct sr_session *session)
{
	GIOChannel *channel;

#ifdef _WIN32
	stdin_handle = GetStdHandle(STD_INPUT_HANDLE);

	if (!GetConsoleMode(stdin_handle, &stdin_mode)) {
		/* TODO: Error handling. */
	}
	SetConsoleMode(stdin_handle, 0);

	channel = g_io_channel_win32_new_fd(0);
#else
	struct termios term;

	tcgetattr(STDIN_FILENO, &term);
	memcpy(&term_orig, &term, sizeof(struct termios));
	term.c_lflag &= ~(ECHO | ICANON | ISIG);
	tcsetattr(STDIN_FILENO, TCSADRAIN, &term);

	channel = g_io_channel_unix_new(STDIN_FILENO);
#endif
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);

	watch_id = g_io_add_watch(channel, G_IO_IN, &received_anykey, session);
	g_io_channel_unref(channel);

	g_message("Press any key to stop acquisition.");
}

/* Remove the event watch and restore stdin attributes.
 */
void clear_anykey(void)
{
	if (watch_id != 0) {
		g_source_remove(watch_id);
		watch_id = 0;
	}
#ifdef _WIN32
	SetConsoleMode(stdin_handle, stdin_mode);
#else
	tcflush(STDIN_FILENO, TCIFLUSH);
	tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
#endif
}
