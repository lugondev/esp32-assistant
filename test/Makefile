CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -g -O0 -I../components/ws_protocol/include \
         -I../components/provisioning/include -I../components/wifi/include \
         -I../components/display/include
SRC_WS_PROTOCOL = ../components/ws_protocol/ws_protocol.c
SRC_PROVISIONING_SSID = ../components/provisioning/provisioning_ssid.c
SRC_PROVISIONING_FORM = ../components/provisioning/provisioning_form.c
SRC_DISPLAY_FONT = ../components/display/display_font.c

.PHONY: test
test: test_ws_protocol test_provisioning_ssid test_provisioning_form test_display_font
	./test_ws_protocol
	./test_provisioning_ssid
	./test_provisioning_form
	./test_display_font

test_ws_protocol: test_ws_protocol.c $(SRC_WS_PROTOCOL)
	$(CC) $(CFLAGS) -o $@ $^

test_provisioning_ssid: test_provisioning_ssid.c $(SRC_PROVISIONING_SSID)
	$(CC) $(CFLAGS) -o $@ $^

test_provisioning_form: test_provisioning_form.c $(SRC_PROVISIONING_FORM)
	$(CC) $(CFLAGS) -o $@ $^

test_display_font: test_display_font.c $(SRC_DISPLAY_FONT)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf test_ws_protocol test_ws_protocol.dSYM \
	       test_provisioning_ssid test_provisioning_ssid.dSYM \
	       test_provisioning_form test_provisioning_form.dSYM \
	       test_display_font test_display_font.dSYM
