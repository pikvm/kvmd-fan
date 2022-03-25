/*****************************************************************************
#                                                                            #
#    KVMD-FAN - A small fan controller daemon for PiKVM.                     #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <pthread.h>
#include <microhttpd.h>

#include "config.h"
#include "tools.h"
#include "logging.h"


typedef struct {
	float			s_temp;
	float			s_speed;
	unsigned		s_pwm;
	unsigned		s_rpm;
	bool			s_ok;
	long double		s_last_fail_ts;
	pthread_mutex_t	s_mutex;

	bool				has_hall;
	int					fd;
	struct MHD_Daemon	*mhd;
} server_s;


server_s *server_init(bool has_hall, const char *path, bool rm, mode_t mode);
void server_destroy(server_s *server);

void server_set_state(server_s *server, float temp, float speed, unsigned pwm, unsigned rpm, bool ok);
