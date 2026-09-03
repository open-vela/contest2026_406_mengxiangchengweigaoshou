# .config entries are loaded into the CMake environment, so the
# CONFIG_OPENWAVE_* variables below are available directly.

if(CONFIG_OPENWAVE)
  nuttx_add_application(
    NAME
    ${CONFIG_OPENWAVE_PROGNAME}
    SRCS
    openwave_main.c
    wave_core.c
    wave_dsp.c
    wave_gen.c
    wave_ui.c
    wave_input.c
    wave_store.c
    STACKSIZE
    ${CONFIG_OPENWAVE_STACKSIZE}
    PRIORITY
    ${CONFIG_OPENWAVE_PRIORITY})
endif()
