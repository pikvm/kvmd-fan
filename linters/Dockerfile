FROM archlinux/archlinux:base-devel

RUN mkdir -p /etc/pacman.d/hooks \
	&& ln -s /dev/null /etc/pacman.d/hooks/30-systemd-tmpfiles.hook

RUN echo "Server = http://mirror.yandex.ru/archlinux/\$repo/os/\$arch" > /etc/pacman.d/mirrorlist

RUN pacman -Syu --noconfirm \
	&& pacman -S --needed --noconfirm \
		vim \
		git \
		python \
		python-pip \
		python-tox \
		cppcheck \
	&& (pacman -Sc --noconfirm || true) \
	&& rm -rf /var/cache/pacman/pkg/*

CMD /bin/bash
