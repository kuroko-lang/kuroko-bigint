CFLAGS ?= -g -O2 -Wno-unused-parameter
CFLAGS += -I../../kuroko/src

all: bigint bigint.so

bigint: bigint.c

bigint.so: module_bigint.c bigint.c
	${CC} ${CFLAGS} -fPIC -shared -o $@ $<

.PHONY: test
test:
	python3 test.py > /tmp/bigint_test_python_result
	~/Projects/kuroko/kuroko test.py > /tmp/bigint_test_kuroko_result
	diff /tmp/bigint_test_python_result /tmp/bigint_test_kuroko_result
