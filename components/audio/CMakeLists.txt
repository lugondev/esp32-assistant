idf_component_register(
    SRCS "audio.c" "drivers/i2s_std.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board)
