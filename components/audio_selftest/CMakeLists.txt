idf_component_register(
    SRCS "audio_selftest.c" "loopback_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES driver audio display)
