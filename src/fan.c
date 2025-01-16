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
		// Use mark-space mode to avoid "smoothing" the pulses, and adding noise
		pwmSetMode(PWM_MODE_MS);
		// Explicitly set the PWM Range, even though 1024 is the default
		// match Range with value of "--pwm-high=" in /etc/conf.d/kvmd-fan
		pwmSetRange( 1024 );
		// Set the PWM clock divisor to get a PWM frequency as close to 25KHz as possible.
		// 25KHz is the industry standard for small fans. 
		// https://noctua.at/pub/media/wysiwyg/Noctua_PWM_specifications_white_paper.pdf

		// The passed clock divisor for a RPi-4b is converted by wiringPi code to account for 
		// the different oscillator frequencies in the 4b v.s. the 3b families (54MHz v.s. 19.2MHz).
		//     cat /sys/kernel/debug/clk/osc/clk_rate
		// The conversion is done using 'C' integer division, and all fractional remainders are lost.
		// For RPi-4b, munged_divisor = 540*divisor/192. For clock==1, munged_divisor==2.
		// 4b_pwm_freq = osc_freq/munged_divisor/Range = 54MHz/2/1024 = 26.367KHz - verified on 'scope
		// 3b_pwm_freq = osc_freq/divisor/Range = 19.2MHz/1/1024 = 18.750KHz - a little slow, but ok
		pwmSetClock( 1 );
	}
#	endif

	atomic_init(&fan->stop, true);
	atomic_init(&fan->rpm, 0);
	if (hall_pin >= 0) {
		LOG_INFO("fan.hall", "Using pin=%d for the Hall sensor", hall_pin);

#		ifdef HAVE_GPIOD2
		struct gpiod_chip *chip;
		if ((chip = gpiod_chip_open("/dev/gpiochip0")) == NULL) {
			LOG_PERROR("fan.hall", "Can't open GPIO chip");
			goto error;
		}

		struct gpiod_line_settings *line_settings;
		assert(line_settings = gpiod_line_settings_new());
		assert(!gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_INPUT));
		assert(!gpiod_line_settings_set_edge_detection(line_settings, GPIOD_LINE_EDGE_FALLING));
		assert(!gpiod_line_settings_set_bias(line_settings,
			hall_bias == FAN_BIAS_PULL_DOWN ? GPIOD_LINE_BIAS_PULL_DOWN
			: hall_bias == FAN_BIAS_PULL_UP ? GPIOD_LINE_BIAS_PULL_UP
			: GPIOD_LINE_BIAS_DISABLED
		));

		struct gpiod_line_config *line_config;
		assert(line_config = gpiod_line_config_new());
		const unsigned offset = hall_pin;
		assert(!gpiod_line_config_add_line_settings(line_config, &offset, 1, line_settings));

		struct gpiod_request_config *request_config;
		assert(request_config = gpiod_request_config_new());
		gpiod_request_config_set_consumer(request_config, "kvmd-fan::hall");

		if ((fan->line = gpiod_chip_request_lines(chip, request_config, line_config)) == NULL) {
			LOG_PERROR("fan.hall", "Can't request GPIO notification");
		}

		gpiod_request_config_free(request_config);
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(line_settings);
		gpiod_chip_close(chip);

		if (fan->line == NULL) {
			goto error;
		}

#		else

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
#		endif

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
#	ifdef HAVE_GPIOD2
	if (fan->line) {
		gpiod_line_request_release(fan->line);
	}
#	else
	if (fan->line) {
		gpiod_line_release(fan->line);
	}
	if (fan->chip) {
		gpiod_chip_close(fan->chip);
	}
#	endif
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
#	define _MAX_EVENTS 16

	fan_s *fan = (fan_s *)v_fan;
	long double next_ts = get_now_monotonic() + 1;
	unsigned pulses = 0;

#	ifdef HAVE_GPIOD2
	struct gpiod_edge_event_buffer *events;
	assert(events = gpiod_edge_event_buffer_new(_MAX_EVENTS));
#	else
	struct gpiod_line_event	events[_MAX_EVENTS];
#	endif

	while (!atomic_load(&fan->stop)) {
#		ifdef HAVE_GPIOD2
		int retval = gpiod_line_request_wait_edge_events(fan->line, 100000000);
#		else
		const struct timespec timeout = {0, 100000000};
		int retval = gpiod_line_event_wait(fan->line, &timeout);
#		endif
		if (retval < 0) {
			LOG_PERROR("fan.hall", "Can't wait events");
			atomic_store(&fan->rpm, -1);
			break;
		} else if (retval > 0) {
#			ifdef HAVE_GPIOD2
			retval = gpiod_line_request_read_edge_events(fan->line, events, _MAX_EVENTS);
#			else
			retval = gpiod_line_event_read_multiple(fan->line, events, _MAX_EVENTS);
#			endif
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

#	ifdef HAVE_GPIOD2
	gpiod_edge_event_buffer_free(events);
#	endif
	return NULL;

#	undef _MAX_EVENTS
}
