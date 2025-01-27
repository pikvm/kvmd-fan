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


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include <sys/stat.h>

#include <iniparser/iniparser.h>

#include "const.h"
#include "logging.h"
#include "temp.h"
#include "fan.h"
#include "server.h"


enum _OPT_VALUES {
	_O_INTERVAL = 'i',

	_O_CONFIG = 'c',

	_O_HELP = 'h',
	_O_VERSION = 'v',

	_O_PWM_PIN = 10000,
	_O_PWM_LOW,
	_O_PWM_HIGH,
	_O_PWM_SOFT,
	_O_HALL_PIN,
	_O_HALL_BIAS,

	_O_TEMP_HYST,
	_O_TEMP_LOW,
	_O_TEMP_HIGH,

	_O_SPEED_IDLE,
	_O_SPEED_LOW,
	_O_SPEED_HIGH,
	_O_SPEED_HEAT,
	_O_SPEED_SPIN_UP,
	_O_SPEED_CONST,

	_O_UNIX,
	_O_UNIX_RM,
	_O_UNIX_MODE,

	_O_VERBOSE,
	_O_DEBUG,
};


static const char *const _SHORT_OPTS = "hvic:";
static const struct option _LONG_OPTS[] = {
	{"pwm-pin",			required_argument,	NULL,	_O_PWM_PIN},
	{"pwm-low",			required_argument,	NULL,	_O_PWM_LOW},
	{"pwm-high",		required_argument,	NULL,	_O_PWM_HIGH},
	{"pwm-soft",		required_argument,	NULL,	_O_PWM_SOFT},
	{"hall-pin",		required_argument,	NULL,	_O_HALL_PIN},
	{"hall-bias",		required_argument,	NULL,	_O_HALL_BIAS},

	{"temp-hyst",		required_argument,	NULL,	_O_TEMP_HYST},
	{"temp-low",		required_argument,	NULL,	_O_TEMP_LOW},
	{"temp-high",		required_argument,	NULL,	_O_TEMP_HIGH},

	{"speed-idle",		required_argument,	NULL,	_O_SPEED_IDLE},
	{"speed-low",		required_argument,	NULL,	_O_SPEED_LOW},
	{"speed-high",		required_argument,	NULL,	_O_SPEED_HIGH},
	{"speed-heat",		required_argument,	NULL,	_O_SPEED_HEAT},
	{"speed-spin-up",	required_argument,	NULL,	_O_SPEED_SPIN_UP},
	{"speed-const",		required_argument,	NULL,	_O_SPEED_CONST},

	{"unix",			required_argument,	NULL,	_O_UNIX},
	{"unix-rm",			no_argument,		NULL,	_O_UNIX_RM},
	{"unix-mode",		required_argument,	NULL,	_O_UNIX_MODE},

	{"interval",		required_argument,	NULL,	_O_INTERVAL},

	{"verbose",			no_argument,		NULL,	_O_VERBOSE},
	{"debug",			no_argument,		NULL,	_O_DEBUG},

	{"help",			no_argument,		NULL,	_O_HELP},
	{"version",			no_argument,		NULL,	_O_VERSION},

	{"config",			required_argument,	NULL,	_O_CONFIG},

	// Compat with version 0.x
	{"temp-min",		required_argument,	NULL,	_O_TEMP_LOW},
	{"temp-max",		required_argument,	NULL,	_O_TEMP_HIGH},
	{"speed-min",		required_argument,	NULL,	_O_SPEED_LOW},
	{"speed-max",		required_argument,	NULL,	_O_SPEED_HIGH},

	{NULL, 0, NULL, 0},
};

static atomic_bool _g_stop = false;
static fan_s *_g_fan = NULL;
static server_s *_g_server = NULL;

static int _g_pwm_pin = 12;
static int _g_pwm_low = 0;
static int _g_pwm_high = 1024;
static int _g_pwm_soft = 0;
static int _g_hall_pin = -1;
static fan_bias_e _g_hall_bias = FAN_BIAS_DISABLED;

static float _g_temp_hyst = 3;
static float _g_temp_low = 45;
static float _g_temp_high = 75;

static float _g_speed_idle = 25;
static float _g_speed_low = 25;
static float _g_speed_high = 75;
static float _g_speed_heat = 100;
static float _g_speed_spin_up = 75;
static float _g_speed_const = -1;

static float _g_interval = 1;

static char *_g_unix_path = NULL;
static bool _g_unix_rm = false;
static mode_t _g_unix_mode = 0;


static int _load_ini(const char *path);

static void _signal_handler(int signum);
static void _install_signal_handlers(void);

static void _stoppable_sleep(unsigned delay);

static int _loop(void);
static void _help(void);


int main(int argc, char *argv[]) {
	int retval = 0;
	LOGGING_INIT;
	assert(_g_unix_path = strdup(""));

#define OPT_NUMBER_BASE(_name, _dest, _min, _max, _base) { \
			errno = 0; char *_end = NULL; int _tmp = strtol(optarg, &_end, _base); \
			if (errno || *_end || _tmp < _min || _tmp > _max) { \
				printf("Invalid value for '%s=%s': min=%d, max=%d\n", _name, optarg, (int)_min, (int)_max); \
				goto error; \
			} \
			_dest = _tmp; \
			break; \
		}

#	define OPT_NUMBER(_name, _dest, _min, _max) OPT_NUMBER_BASE(_name, _dest, _min, _max, 0)

	for (int ch; (ch = getopt_long(argc, argv, _SHORT_OPTS, _LONG_OPTS, NULL)) >= 0;) {
		switch (ch) {
			case _O_PWM_PIN:		OPT_NUMBER("--pwm-pin",			_g_pwm_pin,			0, 256);
			case _O_PWM_LOW:		OPT_NUMBER("--pwm-low",			_g_pwm_low,			0, 1024);
			case _O_PWM_HIGH:		OPT_NUMBER("--pwm-high",		_g_pwm_high,		1, 1024);
			case _O_PWM_SOFT:		OPT_NUMBER("--pwm-soft",		_g_pwm_soft,		50, 100);
			case _O_HALL_PIN:		OPT_NUMBER("--hall-pin",		_g_hall_pin,		-1, 256);
			case _O_HALL_BIAS:		OPT_NUMBER("--hall-bias",		_g_hall_bias,		FAN_BIAS_DISABLED, FAN_BIAS_PULL_UP);

			case _O_TEMP_HYST:		OPT_NUMBER("--temp-hyst",		_g_temp_hyst,		1, 5);
			case _O_TEMP_LOW:		OPT_NUMBER("--temp-low",		_g_temp_low,		0, 85);
			case _O_TEMP_HIGH:		OPT_NUMBER("--temp-high",		_g_temp_high,		0, 85);

			case _O_SPEED_IDLE:		OPT_NUMBER("--speed-idle",		_g_speed_idle,		0, 100);
			case _O_SPEED_LOW:		OPT_NUMBER("--speed-low",		_g_speed_low,		0, 100);
			case _O_SPEED_HIGH:		OPT_NUMBER("--speed-high",		_g_speed_high,		0, 100);
			case _O_SPEED_HEAT:		OPT_NUMBER("--speed-heat",		_g_speed_heat,		0, 100);
			case _O_SPEED_SPIN_UP:	OPT_NUMBER("--speed-spin-up",	_g_speed_spin_up,	0, 100);
			case _O_SPEED_CONST:	OPT_NUMBER("--speed-const",		_g_speed_const,		-1, 100);

			case _O_UNIX:			free(_g_unix_path); assert(_g_unix_path = strdup(optarg)); break;
			case _O_UNIX_RM:		_g_unix_rm = true; break;
			case _O_UNIX_MODE:		OPT_NUMBER_BASE("--unix-mode",	_g_unix_mode, INT_MIN, INT_MAX, 8);

			case _O_INTERVAL:		OPT_NUMBER("--interval",		_g_interval,		1, 10);

			case _O_CONFIG: 		if (_load_ini(optarg) < 0) { goto error; } break;

			case _O_VERBOSE:		log_level = LOG_LEVEL_VERBOSE; break;
			case _O_DEBUG:			log_level = LOG_LEVEL_DEBUG; break;

			case _O_HELP:			_help(); goto ok;
			case _O_VERSION:		puts(VERSION); goto ok;

			case 0: break;
			default: goto error;
		}
	}

#	undef OPT_NUMBER
#	undef OPT_NUMBER_BASE

	if (_g_pwm_low >= _g_pwm_high) {
		puts("Invalid PWM config, chould be: low < high");
		goto error;
	}

	if (!(
		0 <= _g_temp_hyst
		&& _g_temp_hyst < _g_temp_low
		&& _g_temp_low < _g_temp_high
		&& _g_temp_high <= 85
	)) {
		puts("Invalid temp-* config, should be: 0 <= hyst < low < high <= 85");
		goto error;
	}

	if (!(
		0 <= _g_speed_idle
		&& _g_speed_idle <= _g_speed_low
		&& _g_speed_low < _g_speed_high
		&& _g_speed_high <= _g_speed_heat
		&& _g_speed_heat <= 100
	)) {
		puts("Invalid speed-* config, should be: 0 <= idle <= low < high <= heat <= 100");
		goto error;
	}

	_install_signal_handlers();

	if ((_g_fan = fan_init(_g_pwm_pin, _g_pwm_low, _g_pwm_high, _g_pwm_soft, _g_hall_pin, _g_hall_bias)) == NULL) {
		goto error;
	}

	if (_g_unix_path[0] != '\0') {
		if ((_g_server = server_init((_g_hall_pin >= 0), _g_unix_path, _g_unix_rm, _g_unix_mode)) == NULL) {
			goto error;
		}
	}

	if (_loop() < 0) {
		goto error;
	}

	goto ok;
	error:
		retval = 1;
	ok:
		if (_g_server) {
			server_destroy(_g_server);
		}
		if (_g_fan) {
			fan_destroy(_g_fan);
		}
		free(_g_unix_path);
		LOGGING_DESTROY;
		return retval;
}

static int _load_ini(const char *path) {
	dictionary *ini = NULL;
	int retval = 0;

	if (path[0] == '?') {
		path += 1;
		if (access(path, F_OK | R_OK) != 0) {
			LOG_INFO("config", "Optional config is not available: %s", path);
			goto ok;
		}
	}

	LOG_INFO("config", "Reading config '%s' ...", path);
	if ((ini = iniparser_load(path)) == NULL) {
		goto error;
	}

#	define MATCH(_section, _option, _dest, _min, _max, _base) { \
			const char *_value = iniparser_getstring(ini, _section ":" _option, NULL); \
			if (_value != NULL) { \
				errno = 0; char *_end = NULL; int _tmp = strtol(_value, &_end, _base); \
				if (errno || *_end || _tmp < _min || _tmp > _max) { \
					printf("%s: Invalid value for '%s/%s=%s': min=%d, max=%d\n", \
						path, _section, _option, _value, (int)_min, (int)_max); \
					goto error; \
				} \
				_dest = _tmp; \
			} \
		}

	MATCH("main",		"pwm_pin",		_g_pwm_pin,			0, 256,		0)
	MATCH("main",		"pwm_low",		_g_pwm_low,			0, 1024,	0)
	MATCH("main",		"pwm_high",		_g_pwm_high,		1, 1024,	0)
	MATCH("main",		"pwm_soft",		_g_pwm_soft,		50, 100,	0)
	MATCH("main",		"hall_pin",		_g_hall_pin,		-1, 256,	0)
	MATCH("main",		"hall_bias",	_g_hall_bias,		FAN_BIAS_DISABLED, FAN_BIAS_PULL_UP, 0);
	MATCH("main",		"interval",		_g_interval,		1, 10,		0)
	MATCH("temp",		"hyst",			_g_temp_hyst,		1, 5,		0)
	MATCH("temp",		"low",			_g_temp_low,		0, 85,		0)
	MATCH("temp",		"high",			_g_temp_high,		0, 85,		0)
	MATCH("speed",		"idle",			_g_speed_idle,		0, 100,		0)
	MATCH("speed",		"low",			_g_speed_low,		0, 100,		0)
	MATCH("speed",		"high",			_g_speed_high,		0, 100,		0)
	MATCH("speed",		"heat",			_g_speed_heat,		0, 100,		0)
	MATCH("speed",		"spin_up",		_g_speed_spin_up,	0, 100,		0)
	MATCH("speed",		"const",		_g_speed_const,		-1, 100,	0)
	MATCH("server",		"unix_rm",		_g_unix_rm,			0, 1,		0)
	MATCH("server",		"unix_mode",	_g_unix_mode,		INT_MIN, INT_MAX, 8)
	MATCH("logging",	"level",		log_level,			LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, 0);
	{
		const char *value = iniparser_getstring(ini, "server:unix", NULL);
		if (value != NULL) {
			free(_g_unix_path);
			assert(_g_unix_path = strdup(value));
		}
	}

#	undef MATCH

	goto ok;
	error:
		retval = -1;
	ok:
		if (ini) {
			iniparser_freedict(ini);
		}
		return retval;
}

static void _signal_handler(int signum) {
	switch (signum) {
		case SIGTERM:	LOG_INFO_NOLOCK("signal", "===== Stopping by SIGTERM ====="); break;
		case SIGINT:	LOG_INFO_NOLOCK("signal", "===== Stopping by SIGINT ====="); break;
		case SIGPIPE:	LOG_INFO_NOLOCK("signal", "===== Stopping by SIGPIPE ====="); break;
		default:		LOG_INFO_NOLOCK("signal", "===== Stopping by %d =====", signum); break;
	}
	atomic_store(&_g_stop, true);
}

static void _install_signal_handlers(void) {
	struct sigaction sig_act = {0};
	assert(!sigemptyset(&sig_act.sa_mask));
	sig_act.sa_handler = _signal_handler;
	assert(!sigaddset(&sig_act.sa_mask, SIGINT));
	assert(!sigaddset(&sig_act.sa_mask, SIGTERM));
	assert(!sigaddset(&sig_act.sa_mask, SIGPIPE));
	assert(!sigaction(SIGINT, &sig_act, NULL));
	assert(!sigaction(SIGTERM, &sig_act, NULL));
	assert(!sigaction(SIGPIPE, &sig_act, NULL));
}

static void _stoppable_sleep(unsigned delay) {
	delay *= 10;
	for (unsigned count = 0; count < delay && !atomic_load(&_g_stop); ++count) {
		usleep(100000);
	}
}

static int _loop(void) {
	int retval = 0;

	LOG_INFO("loop", "Starting the loop ...");

	float temp_fixed = 0;
	float prev_speed = -1;
	unsigned prev_pwm = 0;
	const char *mode = "???";

	while (!atomic_load(&_g_stop)) {
		float temp = 0;
		if (get_temp(&temp) < 0) {
			goto error;
		}

		bool changed = false;
		if (_g_speed_const < 0) {
			if (fabsf(fabsf(temp_fixed) - fabsf(temp)) >= _g_temp_hyst) {
				LOG_VERBOSE("loop", "Significant temperature change: %.2f°C -> %.2f°C", temp_fixed, temp);
				changed = true;
			}
		}

		if (changed || prev_speed < 0) {
			float speed;
			if (_g_speed_const < 0) {
				if (temp < _g_temp_low) {
					speed = _g_speed_idle;
					mode = "--- IDLE ---";
				} else if (temp > _g_temp_high) {
					speed = _g_speed_heat;
					mode = "!!! HEAT !!!";
				} else {
					speed = remap(temp, _g_temp_low, _g_temp_high, _g_speed_low, _g_speed_high);
					mode = "= IN-RANGE =";
				}
			} else {
				speed = _g_speed_const;
				mode = "= CONST =";
			}

			if ((prev_speed < _g_speed_idle || prev_speed <= 0) && speed > 0) {
				unsigned pwm = fan_set_speed_percent(_g_fan, _g_speed_spin_up);
				LOG_VERBOSE("loop", "Spinning up the fan: speed=%.2f%% (pwm=%u) ...", _g_speed_spin_up, pwm)
				_stoppable_sleep(2);
			}

			prev_pwm = fan_set_speed_percent(_g_fan, speed);
			temp_fixed = temp;
			prev_speed = speed;
			changed = true;
		}

		int rpm = 0;
		bool fan_ok = true;
		if (_g_hall_pin >= 0) {
			rpm = fan_get_hall_rpm(_g_fan);
			fan_ok = !(prev_speed > 0 && rpm <= 0);
		}

		if (_g_server) {
			server_set_state(_g_server, temp, temp_fixed, prev_speed, prev_pwm, rpm, fan_ok);
		}
#		define SAY(_log, _prefix) \
			_log("loop", _prefix " [%s] temp=%.2f°C, speed=%.2f%% (pwm=%u), rpm=%d", \
				mode, temp, prev_speed, prev_pwm, rpm);
		if (changed) {
			SAY(LOG_VERBOSE, "Changed:");
		} else {
			SAY(LOG_DEBUG, " . . . .");
		}
#		undef SAY

		if (!fan_ok) {
			LOG_ERROR("loop", "!!! Fan is not spinning !!!");
			while (!atomic_load(&_g_stop)) {
				fan_set_speed_percent(_g_fan, 100);
				_stoppable_sleep(2);
				if (fan_get_hall_rpm(_g_fan) > 0) {
					LOG_INFO("loop", "+++ Fan is spinning again +++");
					fan_set_speed_percent(_g_fan, prev_speed);
					break;
				}
			}
		}

		_stoppable_sleep(_g_interval);
	}

	goto ok;
	error:
		retval = -1;
	ok:
		LOG_VERBOSE("loop", "Full throttle on the fan!");
		fan_set_speed_percent(_g_fan, 100);
		LOG_INFO("loop", "Bye-bye");
		return retval;;
}

static void _help(void) {
#	define SAY(_msg, ...) printf(_msg "\n", ##__VA_ARGS__)
	SAY("\nKVMD-FAN - A small fan controller daemon for PiKVM");
	SAY("══════════════════════════════════════════════════");
	SAY("Version: %s; license: GPLv3", VERSION);
	SAY("Copyright (C) 2018-2023 Maxim Devaev <mdevaev@gmail.com>\n");
	SAY("Hardware options:");
	SAY("═════════════════");
	SAY("    --pwm-pin <N>  ─── GPIO pin for PWM. Default: %d.\n", _g_pwm_pin);
	SAY("    --pwm-low <N>  ─── PWM low level. Default: %d.\n", _g_pwm_low);
	SAY("    --pwm-high <N>  ── PWM high level. Default: %d.\n", _g_pwm_high);
	SAY("    --pwm-soft <N>  ── Use software PWM with specified range 0...N. Default: disabled.\n");
	SAY("    --hall-pin <N>  ── GPIO pin for the Hall sensor. Default: disabled.\n");
	SAY("    --hall-bias <N>  ─ Hall pin bias: 0 = disabled, 1 = pull-down, 2 = pull-up. Default: %d.\n", _g_hall_bias);
	SAY("Fan control options:");
	SAY("════════════════════");
	SAY("    --temp-hyst <T>  ───── Temperature hysteresis. Default: %.2f°C.\n", _g_temp_hyst);
	SAY("    --temp-low <T>  ────── Lower temperature range limit. Default: %.2f°C.\n", _g_temp_low);
	SAY("    --temp-high <T>  ───── Upper temperature range limit. Default: %.2f°C.\n", _g_temp_high);
	SAY("    --speed-idle <N>  ──── Fan speed below of the range. Default: %.2f%%.\n", _g_speed_idle);
	SAY("    --speed-low <N>  ───── Lower fan speed range limit. Default: %.2f%%.\n", _g_speed_low);
	SAY("    --speed-high <N>  ──── Upper fan speed range limit. Default: %.2f%%.\n", _g_speed_high);
	SAY("    --speed-heat <N>  ──── Fan speed on overheating. Default: %.2f%%.\n", _g_speed_heat);
	SAY("    --speed-spin-up <N>  ─ Fan speed for spin-up. Default: %.2f%%.\n", _g_speed_spin_up);
	SAY("    --speed-const <N>  ─── Override the entire logic and set the constant speed. Default: disabled.\n");
	SAY("    -i|--interval <sec>  ─ Iterations delay. Default: %.2f.\n", _g_interval);
	SAY("HTTP server options:");
	SAY("════════════════════");
	SAY("    --unix <path> ─────── Path to UNIX socket for the /state request. Default: disabled.\n");
	SAY("    --unix-rm  ────────── Try to remove old UNIX socket file before binding. Default: disabled.\n");
	SAY("    --unix-mode <mode>  ─ Set UNIX socket file permissions (like 777). Default: disabled.\n");
	SAY("Config options:");
	SAY("═══════════════");
	SAY("    -c|--config <path>  ─ Path to the INI config file. Default: disabled.\n");
	SAY("Logging options:");
	SAY("════════════════");
	SAY("    --verbose  ─ Enable verbose messages. Default: disabled.\n");
	SAY("    --debug  ─── Enable verbose and debug messages. Default: disabled.\n");
	SAY("Help options:");
	SAY("═════════════");
	SAY("    -h|--help  ──── Print this text and exit.\n");
	SAY("    -v|--version  ─ Print version and exit.\n");
#	undef SAY
}
