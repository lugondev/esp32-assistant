# NOTE: CONFIGURE_DEPENDS is intentionally omitted. ESP-IDF parses this file in
# CMake "script mode" during its requirements-extraction pass, and script mode
# rejects file(GLOB ... CONFIGURE_DEPENDS). The plain glob still auto-discovers
# every components/boards/<name>/board_def.c; adding a new board folder just
# needs a one-time `idf.py reconfigure` (or `fullclean`) to re-run the glob.
file(GLOB BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*/board_def.c")
idf_component_register(
    SRCS ${BOARD_SRCS}
    REQUIRES board audio display buttons battery
    LDFRAGMENTS "linker.lf"
    WHOLE_ARCHIVE)

# WHOLE_ARCHIVE is required, not optional. Each board_def.c registers its board
# purely through a static `board_desc`-section pointer (LUGO_BOARD_REGISTER) that
# nothing references by name. Without WHOLE_ARCHIVE the linker never pulls the
# board_def.c object out of libboards.a (archive-member selection happens before
# --gc-sections / KEEP()), so the board_desc section stays empty and
# board_detect_and_select() sees registered=0. Forcing the whole archive in
# makes the objects present; the linker.lf KEEP() then protects them from GC.
