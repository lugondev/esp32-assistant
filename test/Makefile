CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
         -I../components/provisioning/include -I../components/wifi/include \
         -I../components/display/include -I../components/lugo_protocol/include
SRC_PROVISIONING_SSID = ../components/provisioning/provisioning_ssid.c
SRC_PROVISIONING_FORM = ../components/provisioning/provisioning_form.c
SRC_DISPLAY_FONT = ../components/display/display_font.c
SRC_LUGO_PROTOCOL = ../components/lugo_protocol/lugo_protocol.c

MCP_CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
             -I../components/mcp_server/include -I../components/lugo_protocol/include
SRC_MCP_SERVER = ../components/mcp_server/mcp_server.c ../components/lugo_protocol/lugo_protocol.c

BOARD_CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
               -I../components/board/include -Ishims
SRC_BOARD_SEL = ../components/board/board_select.c ../components/board/board_active.c

FACADE_CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
                -I../components/board/include -I../components/audio/include \
                -I../components/display/include -I../components/buttons/include -Ishims
SRC_FACADES = ../components/board/board_active.c ../components/audio/audio.c ../components/display/display.c ../components/buttons/buttons.c

GFX_CFLAGS = -std=c11 -Wall -Wextra -g -O0 -I../components/gfx/include
SRC_GFX = ../components/gfx/gfx.c

ROBOT_EYES_CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
                     -I../components/robot_eyes/include -I../components/gfx/include \
                     -I../components/display/include
SRC_ROBOT_EYES = ../components/robot_eyes/robot_eyes.c ../components/gfx/gfx.c ../components/display/display_font.c

STATUSBAR_CFLAGS = -std=c11 -Wall -Wextra -g -O0 \
                    -I../components/statusbar/include -I../components/gfx/include \
                    -I../components/display/include
SRC_STATUSBAR = ../components/statusbar/statusbar.c ../components/gfx/gfx.c ../components/display/display_font.c

.PHONY: test
test: test_provisioning_ssid test_provisioning_form test_display_font test_lugo_protocol test_mcp_server test_board_select test_board_facades test_gfx test_robot_eyes test_statusbar
	./test_provisioning_ssid
	./test_provisioning_form
	./test_display_font
	./test_lugo_protocol
	./test_mcp_server
	./test_board_select
	./test_board_facades
	./test_gfx
	./test_robot_eyes
	./test_statusbar

test_provisioning_ssid: test_provisioning_ssid.c $(SRC_PROVISIONING_SSID)
	$(CC) $(CFLAGS) -o $@ $^

test_provisioning_form: test_provisioning_form.c $(SRC_PROVISIONING_FORM)
	$(CC) $(CFLAGS) -o $@ $^

test_display_font: test_display_font.c $(SRC_DISPLAY_FONT)
	$(CC) $(CFLAGS) -o $@ $^

test_lugo_protocol: test_lugo_protocol.c $(SRC_LUGO_PROTOCOL)
	$(CC) $(CFLAGS) -o $@ $^

test_mcp_server: test_mcp_server.c $(SRC_MCP_SERVER)
	$(CC) $(MCP_CFLAGS) -o $@ $^

test_board_select: test_board_select.c $(SRC_BOARD_SEL)
	$(CC) $(BOARD_CFLAGS) -o $@ $^

test_board_facades: test_board_facades.c $(SRC_FACADES)
	$(CC) $(FACADE_CFLAGS) -o $@ $^

test_gfx: test_gfx.c $(SRC_GFX)
	$(CC) $(GFX_CFLAGS) -o $@ $^

test_robot_eyes: test_robot_eyes.c $(SRC_ROBOT_EYES)
	$(CC) $(ROBOT_EYES_CFLAGS) -o $@ $^

test_statusbar: test_statusbar.c $(SRC_STATUSBAR)
	$(CC) $(STATUSBAR_CFLAGS) -o $@ $^

clean:
	rm -rf test_provisioning_ssid test_provisioning_ssid.dSYM \
	       test_provisioning_form test_provisioning_form.dSYM \
	       test_display_font test_display_font.dSYM \
	       test_lugo_protocol test_lugo_protocol.dSYM \
	       test_mcp_server test_mcp_server.dSYM \
	       test_board_select test_board_select.dSYM \
	       test_board_facades test_board_facades.dSYM \
	       test_gfx test_gfx.dSYM \
	       test_robot_eyes test_robot_eyes.dSYM \
	       test_statusbar test_statusbar.dSYM
