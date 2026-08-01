idf_component_register(
    SRCS "audio.c" "i2s_pcm.c" "drivers/i2s_mic.c" "drivers/i2s_speaker.c" "drivers/i2s_fd.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board)
