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

#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <math.h>

#include <pthread.h>
#ifndef WITH_WIRINGPI_STUB
#	include <wiringPi.h>
#	include <softPwm.h>
#endif
#include <gpiod.h>

#include "const.h"
#include "tools.h"
#include "logging.h"


typedef enum {
	FAN_BIAS_DISABLED = 0,
	FAN_BIAS_PULL_DOWN = 1,
	FAN_BIAS_PULL_UP = 2,
} fan_bias_e;

typedef struct {
	unsigned	pwm_pin;
	unsigned	pwm_low;
	unsigned	pwm_high;
	unsigned	pwm_soft;

	// Hall sensor
	struct gpiod_chip		*chip;
	struct gpiod_line		*line;
	struct gpiod_line_event	events[16];

	pthread_t	tid;
	atomic_int	rpm;
	atomic_bool	stop;
} fan_s;


fan_s *fan_init(unsigned pwm_pin, unsigned pwm_low, unsigned pwm_high, unsigned pwm_soft, int hall_pin, fan_bias_e hall_bias);
void fan_destroy(fan_s *fan);

unsigned fan_set_speed_percent(fan_s *fan, float speed);
int fan_get_hall_rpm(fan_s *fan);
