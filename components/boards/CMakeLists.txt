# Only the selected board is compiled. The directory name is CONFIG_AA_BOARD_NAME
# with dashes swapped for underscores ("lugo-s3-nx" -> "lugo_s3_nx"), so a board
# name with no matching directory fails here, at configure time, naming the path
# it could not find — rather than building fine and failing at boot.
#
# The else branch is for ESP-IDF's requirements-extraction pass, which parses
# this file in CMake script mode where no CONFIG_* is defined yet. SRCS is
# ignored in that pass; the glob just gives it something well-formed.
#
# It also fires in the real pass whenever AA_BOARD_NAME resolves to "" — which
# happens on a target no entry of `choice AA_BOARD` is visible on. That case is
# not benign: you get a glob build that compiles every board, registers none
# (see WHOLE_ARCHIVE below) and fails at boot with registered=0, i.e. exactly
# the late failure this if/else was written to move to configure time. Keeping
# AA_BOARD_CUSTOM visible on every target is what stops that from happening —
# see the comment on it in main/Kconfig.projbuild.
if(CONFIG_AA_BOARD_NAME)
    string(REPLACE "-" "_" _board_dir "${CONFIG_AA_BOARD_NAME}")
    set(BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/${_board_dir}/board_def.c")
else()
    file(GLOB BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*/board_def.c")
endif()
idf_component_register(
    SRCS ${BOARD_SRCS}
    # board_common.h lives here, next to the per-board subdirectories that
    # include it; a board_def.c's own directory is searched first, so this is
    # what makes `#include "board_common.h"` resolve from a subdirectory.
    PRIV_INCLUDE_DIRS "."
    REQUIRES board audio display buttons battery
    LDFRAGMENTS "linker.lf"
    WHOLE_ARCHIVE)

# WHOLE_ARCHIVE is required, not optional. Each board_def.c registers its board
# purely through a static `board_desc`-section pointer (LUGO_BOARD_REGISTER) that
# nothing references by name. Without WHOLE_ARCHIVE the linker never pulls the
# board_def.c object out of libboards.a (archive-member selection happens before
# --gc-sections / KEEP()), so the board_desc section stays empty and
# board_select_configured() sees registered=0. Forcing the whole archive in
# makes the objects present; the linker.lf KEEP() then protects them from GC.
