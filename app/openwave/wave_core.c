/*
 * wave_core.c -- OpenWave ADC sampling core implementation.
 *
 * M2 milestone: opens the ADC character device, reads batches of
 * samples with read(), and stores them in a 512-sample ring buffer for
 * the DSP layer added in M4.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>

#include "wave_core.h"

/* CLOCK_MONOTONIC needs CONFIG_CLOCK_MONOTONIC on NuttX; fall back to
 * the always-available realtime clock otherwise.
 */
#if defined(CLOCK_MONOTONIC)
#  define WAVE_CLOCK_ID CLOCK_MONOTONIC
#else
#  define WAVE_CLOCK_ID CLOCK_REALTIME
#endif

/* Push one sample into the ring buffer, discarding the oldest entry
 * when the buffer is full.
 */
static void wave_core_push(struct wave_core_s *core, uint8_t ch,
                           int32_t raw)
{
  uint16_t tail;

  tail = (core->head + core->count) & (WAVE_ADC_BUFSIZE - 1);
  core->ch[tail] = ch;
  core->raw[tail] = raw;

  if (core->count < WAVE_ADC_BUFSIZE)
    {
      core->count++;
    }
  else
    {
      core->head = (core->head + 1) & (WAVE_ADC_BUFSIZE - 1);
    }
}

int wave_core_init(struct wave_core_s *core, const char *devpath)
{
  int nchannels = 0;

  if (core == NULL)
    {
      return -EINVAL;
    }

  memset(core, 0, sizeof(*core));
  core->fd = -1;

  if (devpath == NULL)
    {
      devpath = WAVE_ADC_DEVPATH;
    }

  core->fd = open(devpath, O_RDONLY);
  if (core->fd < 0)
    {
      return -errno;
    }

  core->opened = true;

  /* Channel count is informational only; not every driver implements
   * this ioctl.
   */
  if (ioctl(core->fd, ANIOC_GET_NCHANNELS,
            (unsigned long)(uintptr_t)&nchannels) == 0)
    {
      core->nchannels = nchannels;
    }

  return 0;
}

void wave_core_deinit(struct wave_core_s *core)
{
  if (core != NULL && core->opened)
    {
      close(core->fd);
      core->fd = -1;
      core->opened = false;
    }
}

int wave_core_read(struct wave_core_s *core,
                   struct wave_sample_s *samples, int max_samples)
{
  struct adc_msg_s msgbuf[WAVE_ADC_BATCH];
  size_t maxread;
  ssize_t nbytes;
  int nsamples;
  int i;

  if (core == NULL || !core->opened)
    {
      return -EINVAL;
    }

  if (max_samples <= 0 || max_samples > WAVE_ADC_BATCH)
    {
      max_samples = WAVE_ADC_BATCH;
    }

  if (core->swtrig)
    {
      /* Some boards need a software trigger to start a conversion;
       * ignore errors because free-running drivers return ENOTTY here.
       */
      ioctl(core->fd, ANIOC_TRIGGER, 0);
    }

  maxread = (size_t)max_samples * sizeof(struct adc_msg_s);
  nbytes = read(core->fd, msgbuf, maxread);
  if (nbytes < 0)
    {
      if (errno == EINTR)
        {
          return 0;
        }

      return -errno;
    }

  nsamples = nbytes / sizeof(struct adc_msg_s);
  for (i = 0; i < nsamples; i++)
    {
      wave_core_push(core, msgbuf[i].am_channel, msgbuf[i].am_data);

      if (samples != NULL)
        {
          samples[i].ch = msgbuf[i].am_channel;
          samples[i].raw = msgbuf[i].am_data;
        }
    }

  return nsamples;
}

uint32_t wave_core_to_mv(int32_t raw)
{
  if (raw < 0)
    {
      raw = 0;
    }

  return (uint32_t)(((uint64_t)raw * WAVE_ADC_VREF_MV) /
                    WAVE_ADC_FULLSCALE);
}

double wave_core_raw_to_volt(double raw)
{
  if (raw < 0.0)
    {
      raw = 0.0;
    }

  return raw * WAVE_ADC_VREF_MV / (WAVE_ADC_FULLSCALE * 1000.0);
}

int wave_core_collect_channel(struct wave_core_s *core, uint8_t channel,
                              int32_t *buf, int nsamples,
                              double *channel_rate_hz)
{
  struct timespec t0;
  struct timespec t1;
  struct wave_sample_s s;
  uint32_t total = 0;
  uint32_t chmask = 0;
  double elapsed;
  int nch_seen;
  int collected = 0;
  int i;
  int n;

  if (core == NULL || !core->opened || buf == NULL || nsamples <= 0)
    {
      return -EINVAL;
    }

  clock_gettime(WAVE_CLOCK_ID, &t0);
  while (collected < nsamples)
    {
      n = wave_core_read(core, &s, 1);
      if (n < 0)
        {
          return n;
        }

      if (n == 0)
        {
          continue; /* no data yet; keep waiting */
        }

      total++;
      if (s.ch < 32)
        {
          chmask |= (1u << s.ch);
        }

      if (s.ch == channel)
        {
          buf[collected++] = s.raw;
        }
    }

  clock_gettime(WAVE_CLOCK_ID, &t1);

  nch_seen = 0;
  for (i = 0; i < 32; i++)
    {
      if ((chmask & (1u << i)) != 0)
        {
          nch_seen++;
        }
    }

  elapsed = (double)(t1.tv_sec - t0.tv_sec) +
            (double)(t1.tv_nsec - t0.tv_nsec) / 1000000000.0;
  if (elapsed <= 0.0)
    {
      elapsed = 1e-6;
    }

  if (channel_rate_hz != NULL)
    {
      /* Channels are sampled in sequence, so the per-channel rate is
       * the total rate divided by the number of channels observed.
       */
      *channel_rate_hz = nch_seen > 0 ? (double)total / elapsed / nch_seen
                                      : (double)total / elapsed;
    }

  return collected;
}

int wave_core_analyze(const int32_t *buf, int nsamples,
                      double channel_rate, struct wave_stats_s *stats)
{
  if (buf == NULL || stats == NULL || nsamples <= 0)
    {
      return -EINVAL;
    }

  return wave_dsp_analyze(buf, nsamples, channel_rate, stats);
}
