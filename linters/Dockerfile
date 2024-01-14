FROM archlinux/archlinux:base

RUN mkdir -p /etc/pacman.d/hooks \
	&& ln -s /dev/null /etc/pacman.d/hooks/30-systemd-tmpfiles.hook

RUN echo 'Server = https://mirror.rackspace.com/archlinux/$repo/os/$arch' > /etc/pacman.d/mirrorlist \
	&& pacman-key --init \
	&& pacman-key --populate archlinux

RUN pacman --noconfirm --ask=4 -Syy \
	&& pacman --needed --noconfirm --ask=4 -S \
		glibc \
		pacman \
		openssl \
		openssl-1.1 \
	&& pacman-db-upgrade \
	&& pacman --noconfirm --ask=4 -Syu \
	&& pacman --needed --noconfirm --ask=4 -S \
		p11-kit \
		archlinux-keyring \
		ca-certificates \
		ca-certificates-mozilla \
		ca-certificates-utils \
	&& pacman -Syu --noconfirm --ask=4 \
	&& pacman -S --needed --noconfirm --ask=4 \
		vim \
		git \
		python \
		python-pip \
		python-tox \
		cppcheck \
	&& (pacman -Sc --noconfirm || true) \
	&& rm -rf /var/cache/pacman/pkg/*

RUN ln -s /usr/lib/libtinyxml2.so.10 /usr/lib/libtinyxml2.so.9

CMD /bin/bash
