all : bgr565torgb888 rgb888tobgr565

CFLAGS_TINY:=-Os

ifeq ($(OS),Windows_NT)
	CFLAGS_TINY:=-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -fwhole-program -s
else
	OS_NAME := $(shell uname -s | tr A-Z a-z)
	ifeq ($(OS_NAME),linux)
		CFLAGS_TINY:=-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -fwhole-program -s
	endif
	ifeq ($(OS_NAME),darwin)
		CFLAGS_TINY:=-Os -ffunction-sections -ffunction-sections -fdata-sections -Wl,-dead_strip
	endif
endif


bgr565torgb888 : bgr565torgb888.c 
	gcc -o $@ $< -g -O2 -Wall -lSDL2

rgb888tobgr565 : rgb888tobgr565.c 
	gcc -o $@ $< -g -O2 -Wall -lSDL2

clean:
	rm -rf rgb888tobgr565 bgr565torgb888

