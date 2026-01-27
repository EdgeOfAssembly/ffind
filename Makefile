# GNU Makefile for ffind

CXX = g++
CXXFLAGS = -std=c++20 -O3 -pthread -Wall -Wextra
TARGETS = ffind-daemon ffind

all: $(TARGETS)

ffind-daemon: ffind-daemon.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

ffind: ffind.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

install: $(TARGETS)
	install -Dm755 ffind-daemon /usr/local/bin/ffind-daemon
	install -Dm755 ffind /usr/local/bin/ffind
	install -Dm644 ffind.1 /usr/local/share/man/man1/ffind.1
	install -Dm644 ffind-daemon.8 /usr/local/share/man/man8/ffind-daemon.8

clean:
	rm -f $(TARGETS)

.PHONY: all install clean