/*
 * wave_core.h -- OpenWave ADC sampling core.
 *
 * M2 milestone: opens the ADC character device and maintains a
 * 512-sample ring buffer.  Raw samples and millivolt conversions are
 * exposed here for the DSP layer added in M4.
 */

#ifndef __APPS_OPENWAVE_WAVE_CORE_H
#define __APPS_OPENWAVE_WAVE_CORE_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "wave_dsp.h"

/* Default ADC device path (may be overridden on the command line). */
#define WAVE_ADC_DEVPATH   "/dev/adc0"

/* Ring-buffer size; power of two so indexing uses a mask. */
#define WAVE_ADC_BUFSIZE   512

/* Maximum number of samples requested from one read(). */
#define WAVE_ADC_BATCH     8

/* Hardware reference: STM32H7A3 16-bit ADC, VDDA = 3.3 V. */
#define WAVE_ADC_FULLSCALE 65535
#define WAVE_ADC_VREF_MV   3300

/* One acquired sample. */
struct wave_sample_s
{
  uint8_t ch;    /* ADC channel number */
  int32_t raw;   /* raw 16-bit sample */
};

/* Acquisition context; one instance per application. */
struct wave_core_s
{
  int  fd;        /* open file descriptor for the ADC device */
  bool opened;    /* true once the device is open */
  bool swtrig;    /* issue ANIOC_TRIGGER before each read */
  int  nchannels; /* channel count from the driver (0 = unknown) */

  /* Circular sample buffer (raw + channel per entry). */
  int32_t  raw[WAVE_ADC_BUFSIZE];
  uint8_t  ch[WAVE_ADC_BUFSIZE];
  uint16_t head;  /* index of the oldest sample */
  uint16_t count; /* number of valid samples in the buffer */
};

/* Open the ADC device.  devpath may be NULL to use WAVE_ADC_DEVPATH.
 * Returns 0 on success or a negative errno value.
 */
int wave_core_init(struct wave_core_s *core, const char *devpath);

/* Close the ADC device. */
void wave_core_deinit(struct wave_core_s *core);

/* Read up to max_samples samples.  Samples are stored in the internal
 * ring buffer and, when samples != NULL, also copied to the caller
 * array.  Returns the number of samples read, 0 when no data is
 * currently available, or a negative errno value on failure.
 */
int wave_core_read(struct wave_core_s *core,
                   struct wave_sample_s *samples, int max_samples);

/* Convert a raw sample to millivolts. */
uint32_t wave_core_to_mv(int32_t raw);

/* Convert a raw value to volts (double precision). */
double wave_core_raw_to_volt(double raw);

/* Block until nsamples of the requested channel have been collected
 * into buf (caller-provided, capacity >= nsamples).  Also estimates the
 * per-channel sample rate in Hz.  Returns the number of samples
 * collected or a negative errno value.
 */
int wave_core_collect_channel(struct wave_core_s *core, uint8_t channel,
                              int32_t *buf, int nsamples,
                              double *channel_rate_hz);

/* Compute statistics for one window of samples. */
int wave_core_analyze(const int32_t *buf, int nsamples,
                      double channel_rate, struct wave_stats_s *stats);

#endif /* __APPS_OPENWAVE_WAVE_CORE_H */
