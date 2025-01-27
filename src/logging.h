/*****************************************************************************
#                                                                            #
#    KVMD-FAN - A small fan controller daemon for PiKVM.                     #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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

#include <stdio.h>
#include <errno.h>

#include <pthread.h>

#include "tools.h"


typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_VERBOSE,
    LOG_LEVEL_DEBUG,
} log_level_e;


extern log_level_e log_level;

extern pthread_mutex_t log_mutex;


#define LOGGING_INIT { \
		log_level = LOG_LEVEL_INFO; \
		A_MUTEX_INIT(&log_mutex); \
	}

#define LOGGING_DESTROY A_MUTEX_DESTROY(&log_mutex)


#define LOG_PRINTF_NOLOCK(_label, _class, _msg, ...) { \
		fprintf(stderr, "-- " _label " [%.03Lf %9s] -- " _msg, \
			get_now_monotonic(), _class, ##__VA_ARGS__); \
		fputc('\n', stderr); \
		fflush(stderr); \
	}

#define LOG_PRINTF(_label, _class, _msg, ...) { \
		A_MUTEX_LOCK(&log_mutex); \
		LOG_PRINTF_NOLOCK(_label, _class, _msg, ##__VA_ARGS__); \
		A_MUTEX_UNLOCK(&log_mutex); \
	}

#define LOG_ERROR(_class, _msg, ...) { \
		LOG_PRINTF("ERROR", _class, _msg, ##__VA_ARGS__); \
	}

#define LOG_PERROR(_class, _msg, ...) { \
		char _perror_buf[1024] = {0}; \
		const char *const _perror_ptr = errno_to_string(errno, _perror_buf, 1024); \
		LOG_ERROR(_class, _msg ": %s", ##__VA_ARGS__, _perror_ptr); \
	}

#define LOG_INFO(_class, _msg, ...) { \
		LOG_PRINTF("INFO ", _class, _msg, ##__VA_ARGS__); \
	}

#define LOG_INFO_NOLOCK(_class, _msg, ...) { \
		LOG_PRINTF_NOLOCK("INFO ", _class, _msg, ##__VA_ARGS__); \
	}

#define LOG_VERBOSE(_class, _msg, ...) { \
		if (log_level >= LOG_LEVEL_VERBOSE) { \
			LOG_PRINTF("VERB ", _class, _msg, ##__VA_ARGS__); \
		} \
	}

#define LOG_DEBUG(_class, _msg, ...) { \
		if (log_level >= LOG_LEVEL_DEBUG) { \
			LOG_PRINTF("DEBUG", _class, _msg, ##__VA_ARGS__); \
		} \
	}
