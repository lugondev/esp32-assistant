idf_component_register(
    SRCS "voice.c"
    INCLUDE_DIRS "include"
    REQUIRES audio
    EMBED_FILES "assets/voice_setup.pcm" "assets/voice_connecting.pcm" "assets/voice_connected.pcm")
