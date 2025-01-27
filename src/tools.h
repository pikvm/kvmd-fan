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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <time.h>
#include <assert.h>

#include <pthread.h>


#ifdef NDEBUG
#	error WTF dude? Asserts are good things!
#endif

#if CHAR_BIT != 8
#	error There are not 8 bits in a char!
#endif


#define INLINE inline __attribute__((always_inline))
#define UNUSED __attribute__((unused))

#define A_CALLOC(_dest, _nmemb)			assert((_dest = calloc(_nmemb, sizeof(*(_dest)))))
#define A_ASPRINTF(_dest, _fmt, ...)	assert(asprintf(&(_dest), _fmt, ##__VA_ARGS__) >= 0)

#define A_THREAD_CREATE(_tid, _func, _arg)	assert(!pthread_create(_tid, NULL, _func, _arg))
#define A_THREAD_JOIN(_tid)					assert(!pthread_join(_tid, NULL))
#define A_MUTEX_INIT(_mutex)				assert(!pthread_mutex_init(_mutex, NULL))
#define A_MUTEX_DESTROY(_mutex)				assert(!pthread_mutex_destroy(_mutex))
#define A_MUTEX_LOCK(_mutex)				assert(!pthread_mutex_lock(_mutex))
#define A_MUTEX_UNLOCK(_mutex)				assert(!pthread_mutex_unlock(_mutex))


INLINE long double get_now_monotonic(void) {
	struct timespec ts;
	assert(!clock_gettime(
#		if defined(CLOCK_MONOTONIC_RAW)
		CLOCK_MONOTONIC_RAW,
#		elif defined(CLOCK_MONOTONIC_FAST)
		CLOCK_MONOTONIC_FAST,
#		else
		CLOCK_MONOTONIC,
#		endif
		&ts));

	time_t sec = ts.tv_sec;
	long msec = round(ts.tv_nsec / 1.0e6);
	if (msec > 999) {
		sec += 1;
		msec = 0;
	}
	return (long double)sec + ((long double)msec) / 1000;
}

INLINE char *errno_to_string(int error, char *buf, size_t size) {
	assert(buf);
	assert(size > 0);
	locale_t locale = newlocale(LC_MESSAGES_MASK, "C", NULL);
	const char *const str = "!!! newlocale() error !!!";
	strncpy(buf, (locale ? strerror_l(error, locale) : str), size - 1);
	buf[size - 1] = '\0';
	if (locale) {
		freelocale(locale);
	}
	return buf;
}

INLINE float remap(float value, float in_min, float in_max, float out_min, float out_max) {
	value = fminf(fmaxf(value, in_min), in_max);
	return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
