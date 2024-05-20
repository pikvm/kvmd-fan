# KVMD-FAN
[![CI](https://github.com/pikvm/kvmd-fan/workflows/CI/badge.svg)](https://github.com/pikvm/kvmd-fan/actions?query=workflow%3ACI)
[![Discord](https://img.shields.io/discord/580094191938437144?logo=discord)](https://discord.gg/bpmXfz5)

This repository contains the configuration and code of KVMD-FAN, a small fan controller daemon for PiKVM.
If your request does not relate directly to this codebase, please send it to issues of the [PiKVM](https://github.com/pikvm/pikvm/issues) repository.

# Configuration

```ini
; /etc/kvmd/fan.ini
[main]
pwm_pin = 12
hall_pin = 6

[speed]
idle = 10
low = 33

[temp]
low = 35

[server]
unix = /run/kvmd/fan.sock
unix_rm = 1
unix_mode = 666

[logging]
level = 1
```

Same with args (check out `kvmd-fan --help`):

```bash
# vim /etc/conf.d/kvmd-fan
KVMD_FAN_ARGS="--verbose --pwm-pin 12 --hall-pin 6 --speed-idle 10 --speed-low 33 --temp-low 35 --unix /run/kvmd/fan.sock --unix-rm --unix-mode 666"
```
