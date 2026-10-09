# Test della logica pura del firmware, compilati con il gcc del PC.
# Eseguire in una shell SENZA l'ambiente ESP-IDF attivato: make -C firmware/test_host

IDF_PATH ?= $(HOME)/.espressif/v5.5.2/esp-idf
UNITY := $(IDF_PATH)/components/unity/unity/src
CJSON := $(IDF_PATH)/components/json/cJSON
CORE  := ../components/pager_core

CC := /usr/bin/gcc
CFLAGS := -std=gnu11 -Wall -Wextra -g -I$(CORE)/include -I$(UNITY) -I$(CJSON)

TESTS := test_pager_proto test_pager_app test_pager_led_mono test_pager_netguard

all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./build/$$t || exit 1; done

build:
	mkdir -p build

test_pager_proto: | build
	$(CC) $(CFLAGS) -o build/$@ $@.c $(CORE)/pager_proto.c $(UNITY)/unity.c $(CJSON)/cJSON.c

test_pager_app: | build
	$(CC) $(CFLAGS) -o build/$@ $@.c $(CORE)/pager_app.c $(UNITY)/unity.c

test_pager_led_mono: | build
	$(CC) $(CFLAGS) -o build/$@ $@.c $(CORE)/pager_led_mono.c $(UNITY)/unity.c

test_pager_netguard: | build
	$(CC) $(CFLAGS) -o build/$@ $@.c $(CORE)/pager_netguard.c $(UNITY)/unity.c

clean:
	rm -rf build

.PHONY: all clean $(TESTS)
