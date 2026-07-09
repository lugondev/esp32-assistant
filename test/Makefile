CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
         -I../components/provisioning/include -I../components/wifi/include \
         -I../components/display/include -I../components/lugo_protocol/include
SRC_PROVISIONING_SSID = ../components/provisioning/provisioning_ssid.c
SRC_PROVISIONING_FORM = ../components/provisioning/provisioning_form.c
SRC_DISPLAY_FONT = ../components/display/display_font.c
SRC_LUGO_PROTOCOL = ../components/lugo_protocol/lugo_protocol.c

BOARD_CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
               -I../components/board/include -Ishims
SRC_BOARD_SEL = ../components/board/board_select.c ../components/board/board_active.c

.PHONY: test
test: test_provisioning_ssid test_provisioning_form test_display_font test_lugo_protocol test_board_select
	./test_provisioning_ssid
	./test_provisioning_form
	./test_display_font
	./test_lugo_protocol
	./test_board_select

test_provisioning_ssid: test_provisioning_ssid.c $(SRC_PROVISIONING_SSID)
	$(CC) $(CFLAGS) -o $@ $^

test_provisioning_form: test_provisioning_form.c $(SRC_PROVISIONING_FORM)
	$(CC) $(CFLAGS) -o $@ $^

test_display_font: test_display_font.c $(SRC_DISPLAY_FONT)
	$(CC) $(CFLAGS) -o $@ $^

test_lugo_protocol: test_lugo_protocol.c $(SRC_LUGO_PROTOCOL)
	$(CC) $(CFLAGS) -o $@ $^

test_board_select: test_board_select.c $(SRC_BOARD_SEL)
	$(CC) $(BOARD_CFLAGS) -o $@ $^

clean:
	rm -rf test_provisioning_ssid test_provisioning_ssid.dSYM \
	       test_provisioning_form test_provisioning_form.dSYM \
	       test_display_font test_display_font.dSYM \
	       test_lugo_protocol test_lugo_protocol.dSYM \
	       test_board_select test_board_select.dSYM
