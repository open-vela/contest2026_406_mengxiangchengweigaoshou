/*
 * wave_dsp.h -- OpenWave signal-processing primitives.
 *
 * M4 milestone: mean / RMS / peak-to-peak / zero-crossing frequency and
 * a radix-2 FFT.  This module is deliberately free of NuttX dependencies
 * so the algorithms can be unit-tested on a host machine.
 */

#ifndef __APPS_OPENWAVE_WAVE_DSP_H
#define __APPS_OPENWAVE_WAVE_DSP_H

#include <stdint.h>

/* Maximum FFT size supported (256/512-point windows). */
#define WAVE_DSP_MAX_FFT 512

/* Result of one analysis window. */
struct wave_stats_s
{
  uint16_t nsamples; /* number of samples analyzed */
  double   mean_raw; /* arithmetic mean, raw ADC units */
  double   rms_raw;  /* root-mean-square, raw ADC units */
  int32_t  vmin_raw; /* minimum sample, raw ADC units */
  int32_t  vmax_raw; /* maximum sample, raw ADC units */
  double   vpp_raw;  /* peak-to-peak, raw ADC units */
  double   freq_zc;  /* frequency via zero crossings, Hz (0 if none) */
};

double  wave_dsp_mean(const int32_t *data, int n);
double  wave_dsp_rms(const int32_t *data, int n);
int32_t wave_dsp_min(const int32_t *data, int n);
int32_t wave_dsp_max(const int32_t *data, int n);

/* Count rising/falling crossings of the AC-coupled signal (mean
 * removed) with a small hysteresis and convert them to Hz.
 */
double wave_dsp_zero_cross_freq(const int32_t *data, int n,
                                double sample_rate);

/* Compute all scalar statistics for one window.  Returns 0 on success
 * or a negative error code.
 */
int wave_dsp_analyze(const int32_t *data, int n, double sample_rate,
                     struct wave_stats_s *stats);

/* In-place iterative radix-2 FFT (n must be a power of two). */
void wave_dsp_fft(double *re, double *im, int n);

/* AC-couple, window, FFT, and report the dominant bin frequency.
 * re/im must be caller-provided buffers of at least n doubles.
 * Returns 0 on success or a negative error code.
 */
int wave_dsp_fft_freq(const int32_t *data, int n, double sample_rate,
                      double *re, double *im, double *freq_hz,
                      double *peak_mag);

#endif /* __APPS_OPENWAVE_WAVE_DSP_H */
