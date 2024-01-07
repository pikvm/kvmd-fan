-include config.mk

DESTDIR ?=
PREFIX ?= /usr/local

CC ?= gcc
CFLAGS ?= -O3
LDFLAGS ?=

_APP = kvmd-fan
_CFLAGS = -MD -c -std=c17 -Wall -Wextra -D_GNU_SOURCE $(shell pkg-config --atleast-version=2 libgpiod 2> /dev/null && echo -DHAVE_GPIOD2)
_LDFLAGS = $(LDFLAGS) -lm -lpthread -liniparser -lmicrohttpd -lgpiod
_SRCS = $(shell ls src/*.c)
_BUILD = build

_LINTERS_IMAGE ?= kvmd-fan-linters


# =====
define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef


ifneq ($(call optbool,$(WITH_WIRINGPI_STUB)),)
override _CFLAGS += -DWITH_WIRINGPI_STUB
else
override _LDFLAGS += -lwiringPi
endif


# =====
all: $(_APP)


install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m755 $(_APP) $(DESTDIR)$(PREFIX)/bin/$(_APP)


install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/$(_APP)


$(_APP): $(_SRCS:%.c=$(_BUILD)/%.o)
	$(info == LD $@)
	@ $(CC) $^ -o $@ $(_LDFLAGS) $(_LIBS)


$(_BUILD)/%.o: %.c
	$(info -- CC $<)
	@ mkdir -p $(dir $@) || true
	@ $(CC) $< -o $@ $(_CFLAGS)


release:
	$(MAKE) clean
	$(MAKE) tox
	$(MAKE) push
	$(MAKE) bump V=$(V)
	$(MAKE) push
	$(MAKE) clean


tox: linters
	time docker run --rm \
			--volume `pwd`:/src:ro \
			--volume `pwd`/linters:/src/linters:rw \
		-t $(_LINTERS_IMAGE) bash -c " \
			cd /src \
			&& tox -q -c linters/tox.ini $(if $(E),-e $(E),-p auto) \
		"


linters:
	docker build \
			$(if $(call optbool,$(NC)),--no-cache,) \
			--rm \
			--tag $(_LINTERS_IMAGE) \
		-f linters/Dockerfile linters


bump:
	bumpversion $(if $(V),$(V),minor)


push:
	git push
	git push --tags


clean:
	rm -rf $(_APP) $(_BUILD) *.sock


clean-all: clean
	sudo rm -rf linters/.tox


_OBJS = $(_SRCS:%.c=$(_BUILD)/%.o)
-include $(_OBJS:%.o=%.d)


.PHONY: linters
