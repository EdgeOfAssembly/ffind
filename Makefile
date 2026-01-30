# GNU Makefile for ffind
# 
# Targets:
#   make                  - Build ffind and ffind-daemon
#   make install          - Install binaries, man pages, and example config
#   make install-openrc   - Install OpenRC service files (Gentoo/Alpine)
#   make install-systemd  - Install systemd service files (most distros)
#   make uninstall        - Remove installed files
#   make clean            - Remove build artifacts
#
# Installation Paths (can be overridden):
#   PREFIX          = /usr/local
#   BINDIR          = $(PREFIX)/bin
#   MANDIR          = $(PREFIX)/share/man
#   SYSCONFDIR      = /etc

CXX = g++
CXXFLAGS = -std=c++20 -O3 -pthread -Wall -Wextra

# Installation directories (can be overridden: make PREFIX=/usr install)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
SYSCONFDIR ?= /etc

# Service directories
OPENRC_INITDIR ?= $(SYSCONFDIR)/init.d
OPENRC_CONFDIR ?= $(SYSCONFDIR)/conf.d
SYSTEMD_UNITDIR ?= /usr/lib/systemd/system

TARGETS = ffind-daemon ffind

all: $(TARGETS)

ffind-daemon: ffind-daemon.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ -lsqlite3 -lre2

ffind: ffind.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ -lre2

install: $(TARGETS)
	@echo "Installing ffind to $(DESTDIR)$(BINDIR)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 ffind-daemon $(DESTDIR)$(BINDIR)/ffind-daemon
	install -m 755 ffind $(DESTDIR)$(BINDIR)/ffind
	@echo "Installing man pages to $(DESTDIR)$(MANDIR)..."
	install -d $(DESTDIR)$(MANDIR)/man1
	install -d $(DESTDIR)$(MANDIR)/man8
	install -m 644 ffind.1 $(DESTDIR)$(MANDIR)/man1/ffind.1
	install -m 644 ffind-daemon.8 $(DESTDIR)$(MANDIR)/man8/ffind-daemon.8
	@echo "Installing example config to $(DESTDIR)$(SYSCONFDIR)/ffind/..."
	install -d $(DESTDIR)$(SYSCONFDIR)/ffind
	@if [ ! -f $(DESTDIR)$(SYSCONFDIR)/ffind/config.yaml ]; then \
		install -m 644 config.yaml.example $(DESTDIR)$(SYSCONFDIR)/ffind/config.yaml; \
		echo "Installed example config to $(DESTDIR)$(SYSCONFDIR)/ffind/config.yaml"; \
	else \
		echo "Config file already exists, not overwriting"; \
		install -m 644 config.yaml.example $(DESTDIR)$(SYSCONFDIR)/ffind/config.yaml.example; \
	fi
	@echo ""
	@echo "Installation complete!"
	@echo "  Binaries:  $(DESTDIR)$(BINDIR)/ffind, $(DESTDIR)$(BINDIR)/ffind-daemon"
	@echo "  Man pages: $(DESTDIR)$(MANDIR)/man1/ffind.1, $(DESTDIR)$(MANDIR)/man8/ffind-daemon.8"
	@echo "  Config:    $(DESTDIR)$(SYSCONFDIR)/ffind/config.yaml"
	@echo ""
	@echo "To install service files:"
	@echo "  OpenRC (Gentoo/Alpine):  sudo make install-openrc"
	@echo "  systemd (most distros):  sudo make install-systemd"

install-openrc:
	@echo "Installing OpenRC service files..."
	install -d $(DESTDIR)$(OPENRC_INITDIR)
	install -d $(DESTDIR)$(OPENRC_CONFDIR)
	install -m 755 ffind-daemon.openrc $(DESTDIR)$(OPENRC_INITDIR)/ffind-daemon
	@if [ ! -f $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon ]; then \
		install -m 644 etc-conf.d-ffind-daemon.example $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon; \
		echo "Installed config to $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon"; \
	else \
		echo "Config file already exists at $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon, not overwriting"; \
	fi
	@echo ""
	@echo "OpenRC installation complete!"
	@echo "  Init script: $(DESTDIR)$(OPENRC_INITDIR)/ffind-daemon"
	@echo "  Config:      $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Edit $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon to set FFIND_ROOTS"
	@echo "  2. Start:  sudo rc-service ffind-daemon start"
	@echo "  3. Enable: sudo rc-update add ffind-daemon default"

install-systemd:
	@echo "Installing systemd service files..."
	install -d $(DESTDIR)$(SYSTEMD_UNITDIR)
	install -m 644 ffind-daemon.service $(DESTDIR)$(SYSTEMD_UNITDIR)/ffind-daemon.service
	@echo ""
	@echo "systemd installation complete!"
	@echo "  Unit file: $(DESTDIR)$(SYSTEMD_UNITDIR)/ffind-daemon.service"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Edit $(DESTDIR)$(SYSCONFDIR)/ffind/config.yaml to configure"
	@echo "  2. Reload: sudo systemctl daemon-reload"
	@echo "  3. Start:  sudo systemctl start ffind-daemon"
	@echo "  4. Enable: sudo systemctl enable ffind-daemon"

uninstall:
	@echo "Removing ffind installation..."
	rm -f $(DESTDIR)$(BINDIR)/ffind
	rm -f $(DESTDIR)$(BINDIR)/ffind-daemon
	rm -f $(DESTDIR)$(MANDIR)/man1/ffind.1
	rm -f $(DESTDIR)$(MANDIR)/man8/ffind-daemon.8
	@echo "Note: Config files in $(DESTDIR)$(SYSCONFDIR)/ffind/ were NOT removed"
	@echo "Note: Service files were NOT removed (use uninstall-openrc or uninstall-systemd)"

uninstall-openrc:
	@echo "Removing OpenRC service files..."
	rm -f $(DESTDIR)$(OPENRC_INITDIR)/ffind-daemon
	@echo "Note: Config file $(DESTDIR)$(OPENRC_CONFDIR)/ffind-daemon was NOT removed"

uninstall-systemd:
	@echo "Removing systemd service files..."
	rm -f $(DESTDIR)$(SYSTEMD_UNITDIR)/ffind-daemon.service
	@echo "Run 'sudo systemctl daemon-reload' to update systemd"

clean:
	rm -f $(TARGETS)

help:
	@echo "ffind Makefile targets:"
	@echo ""
	@echo "  make              - Build ffind and ffind-daemon"
	@echo "  make install      - Install binaries, man pages, and example config"
	@echo "  make install-openrc   - Install OpenRC service files (Gentoo/Alpine)"
	@echo "  make install-systemd  - Install systemd service files"
	@echo "  make uninstall    - Remove installed binaries and man pages"
	@echo "  make uninstall-openrc - Remove OpenRC service files"
	@echo "  make uninstall-systemd - Remove systemd service files"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Installation paths (override with make VAR=value):"
	@echo "  PREFIX=$(PREFIX)"
	@echo "  BINDIR=$(BINDIR)"
	@echo "  MANDIR=$(MANDIR)"
	@echo "  SYSCONFDIR=$(SYSCONFDIR)"
	@echo ""
	@echo "Examples:"
	@echo "  make PREFIX=/usr install          # Install to /usr instead of /usr/local"
	@echo "  make DESTDIR=/tmp/pkg install     # Stage for packaging"

.PHONY: all install install-openrc install-systemd uninstall uninstall-openrc uninstall-systemd clean help