include $(APPDIR)/Make.defs

# OpenWave built-in application
PROGNAME  = $(CONFIG_OPENWAVE_PROGNAME)
PRIORITY  = $(CONFIG_OPENWAVE_PRIORITY)
STACKSIZE = $(CONFIG_OPENWAVE_STACKSIZE)
MODULE    = $(CONFIG_OPENWAVE)

CSRCS   = wave_core.c wave_dsp.c wave_gen.c wave_ui.c wave_input.c wave_store.c
MAINSRC = openwave_main.c

include $(APPDIR)/Application.mk
