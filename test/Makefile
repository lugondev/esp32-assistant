CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -g -O0 -I../components/ws_protocol/include
SRC = ../components/ws_protocol/ws_protocol.c
.PHONY: test
test: test_ws_protocol
	./test_ws_protocol
test_ws_protocol: test_ws_protocol.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
clean:
	rm -rf test_ws_protocol test_ws_protocol.dSYM
