CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
LDLIBS ?= -lsqlite3 -lcrypto
inventory: src/main.c src/store.c src/views.c src/legacy.c src/terminal.c src/app.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c src/store.c src/views.c src/legacy.c src/terminal.c -o $@ $(LDLIBS)
test: inventory
	python3 -m unittest discover -s tests -v
clean:
	rm -f inventory
.PHONY: test clean
