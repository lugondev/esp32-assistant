idf_component_register(
    SRCS "audio.c" "drivers/i2s_mic.c" "drivers/i2s_speaker.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board)
