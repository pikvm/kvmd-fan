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


#include "fan.h"


static void *_hall_thread(void *v_fan);


fan_s *fan_init(unsigned pwm_pin, unsigned pwm_low, unsigned pwm_high, unsigned pwm_soft, int hall_pin, fan_bias_e hall_bias) {
	assert(pwm_low < pwm_high);
	assert(pwm_high <= 1024);

	fan_s *fan;
	A_CALLOC(fan, 1);
	fan->pwm_pin = pwm_pin;
	fan->pwm_low = pwm_low;
	fan->pwm_high = pwm_high;
	fan->pwm_soft = pwm_soft;

	LOG_INFO("fan.pwm", "Using pin=%u for PWM range %u...%u", pwm_pin, pwm_low, pwm_high);
#	ifndef WITH_WIRINGPI_STUB
	wiringPiSetupGpio();
	if (pwm_soft) {
		softPwmCreate(pwm_pin, 0, pwm_soft);
	} else {
		pinMode(pwm_pin, PWM_OUTPUT);
	}
#	endif

	atomic_init(&fan->stop, true);
	atomic_init(&fan->rpm, 0);
	if (hall_pin >= 0) {
		LOG_INFO("fan.hall", "Using pin=%d for the Hall sensor", hall_pin);
		if ((fan->chip = gpiod_chip_open_by_number(0)) == NULL) {
			LOG_PERROR("fan.hall", "Can't open GPIO chip");
			goto error;
		}
		if ((fan->line = gpiod_chip_get_line(fan->chip, hall_pin)) == NULL) {
			LOG_PERROR("fan.hall", "Can't get GPIO line");
			goto error;
		}
		int flags;
		switch (hall_bias) {
			case FAN_BIAS_PULL_DOWN: flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN; break;
			case FAN_BIAS_PULL_UP: flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP; break;
			default: flags = GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
		}
		if (gpiod_line_request_falling_edge_events_flags(fan->line, "kvmd-fan::hall", flags) < 0) {
			LOG_PERROR("fan.hall", "Can't request GPIO notification");
			goto error;
		}

		atomic_store(&fan->stop, false);
		A_THREAD_CREATE(&fan->tid, _hall_thread, fan);
	}

	return fan;
	error:
		fan_destroy(fan);
		return NULL;
}

void fan_destroy(fan_s *fan) {
	if (!atomic_load(&fan->stop)) {
		atomic_store(&fan->stop, true);
		A_THREAD_JOIN(fan->tid);
	}
	if (fan->line) {
		gpiod_line_release(fan->line);
	}
	if (fan->chip) {
		gpiod_chip_close(fan->chip);
	}
	free(fan);
}

unsigned fan_set_speed_percent(fan_s *fan, float speed) {
	unsigned pwm;
	if (speed == 0) {
		pwm = 0;
	} else if (speed == 100) {
		pwm = 1024;
	} else {
		pwm = roundf(remap(speed, 0, 100, fan->pwm_low, fan->pwm_high));
	}
#	ifndef WITH_WIRINGPI_STUB
	if (fan->pwm_soft) {
		softPwmWrite(fan->pwm_pin, pwm / 1024.0 * fan->pwm_soft);
	} else {
		pwmWrite(fan->pwm_pin, pwm);
	}
#	endif
	return pwm;
}

int fan_get_hall_rpm(fan_s *fan) {
	return atomic_load(&fan->rpm);
}

static void *_hall_thread(void *v_fan) {
	fan_s *fan = (fan_s *)v_fan;
	const struct timespec timeout = {0, 100000000};
	long double next_ts = get_now_monotonic() + 1;
	unsigned pulses = 0;

	while (!atomic_load(&fan->stop)) {
		int retval = gpiod_line_event_wait(fan->line, &timeout);
		if (retval < 0) {
			LOG_PERROR("fan.hall", "Can't wait events");
			atomic_store(&fan->rpm, -1);
			break;
		} else if (retval > 0) {
			retval = gpiod_line_event_read_multiple(fan->line, fan->events, ARRAY_LEN(fan->events));
			if (retval < 0) {
				LOG_PERROR("fan.hall", "Can't read events");
				atomic_store(&fan->rpm, -1);
				break;
			}
		} // retval == 0 for zero new events

		long double now_ts = get_now_monotonic();
		if (now_ts > next_ts) {
			atomic_store(&fan->rpm, pulses * 30);
			pulses = 0;
			next_ts = now_ts + 1;
		} else {
			pulses += retval;
		}

		usleep(10000);
	}
	return NULL;
}
