CFLAGS ?= -g -O2 -Wno-unused-parameter
CFLAGS += -I../../kuroko/src

all: bigint bigint.so

bigint: bigint.c

bigint.so: module_bigint.c bigint.c
	${CC} ${CFLAGS} -fPIC -shared -o $@ $<
