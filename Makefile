all : virtualconsole

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


virtualconsole : main.c 
	# for debug
	gcc -o $@ $< -g -O2 -Wall -lSDL2
	gcc -o $@.tiny $< $(CFLAGS_TINY) -lSDL2

clean:
	rm -rf virtualconsole virtualconsole.tiny

