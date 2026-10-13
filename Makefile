NAME=bx-proxy
CC=gcc
PKG_CONFIG_LIBDIR=/usr/lib/x86_64-linux-gnu/pkgconfig
LWS_CFLAGS=$(shell PKG_CONFIG_LIBDIR=$(PKG_CONFIG_LIBDIR) pkg-config --cflags libwebsockets)
LWS_LIBS=$(shell PKG_CONFIG_LIBDIR=$(PKG_CONFIG_LIBDIR) pkg-config --libs libwebsockets)
BX_CFLAGS=$(shell pkg-config --cflags libcurl jansson libxxhash mariadb liboauth2 libcrypto)
BX_LIBS=$(shell pkg-config --libs libcurl jansson libxxhash mariadb liboauth2 libcrypto) -lpthread -lrt
CFLAGS=-Wall -Wno-unused-parameter -ggdb -pthread -I/usr/include $(LWS_CFLAGS) $(BX_CFLAGS) -Ivendor/bx-sync/src -Ivendor/bx-sync/src/include -Isrc/include
LIBS=-L/usr/lib/x86_64-linux-gnu -Wl,-rpath,/usr/lib/x86_64-linux-gnu -lwebsockets -lcap $(BX_LIBS) -pthread

VENDOR_SRC=$(filter-out vendor/bx-sync/src/main.c,$(wildcard vendor/bx-sync/src/*.c vendor/bx-sync/src/*/*.c))
PROXY_SRC=$(wildcard src/*.c)
SRC=$(PROXY_SRC) $(VENDOR_SRC)
OBJ=$(patsubst %.c,build/%.o,$(SRC))

.PHONY: all test clean

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $^ -o $@ $(LIBS)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/test_route: test/route.c src/route.c src/include/route.h
	@mkdir -p build
	$(CC) $(CFLAGS) test/route.c src/route.c -o $@

build/test_json: test/json_row.c src/read_json.c src/include/read_json.h
	@mkdir -p build
	$(CC) $(CFLAGS) test/json_row.c src/read_json.c -o $@ $(shell pkg-config --libs jansson)

build/test_fresh: test/fresh.c src/fresh.c src/include/fresh.h
	@mkdir -p build
	$(CC) $(CFLAGS) test/fresh.c src/fresh.c -o $@

test: build/test_route build/test_json build/test_fresh
	./build/test_route
	./build/test_json
	./build/test_fresh

clean:
	rm -rf build $(NAME)
