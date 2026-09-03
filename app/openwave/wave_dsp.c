/*
 * wave_dsp.c -- OpenWave signal-processing primitives implementation.
 *
 * Pure C99 math, no OS dependencies, so it can be verified on a host
 * machine with synthetic signals before running on the target.
 */

#include <math.h>
#include <string.h>

#include "wave_dsp.h"

double wave_dsp_mean(const int32_t *data, int n)
{
  double sum = 0.0;
  int i;

  if (data == NULL || n <= 0)
    {
      return 0.0;
    }

  for (i = 0; i < n; i++)
    {
      sum += data[i];
    }

  return sum / n;
}

double wave_dsp_rms(const int32_t *data, int n)
{
  double sum = 0.0;
  double v;
  int i;

  if (data == NULL || n <= 0)
    {
      return 0.0;
    }

  for (i = 0; i < n; i++)
    {
      v = (double)data[i];
      sum += v * v;
    }

  return sqrt(sum / n);
}

int32_t wave_dsp_min(const int32_t *data, int n)
{
  int32_t vmin = data != NULL && n > 0 ? data[0] : 0;
  int i;

  for (i = 1; i < n; i++)
    {
      if (data[i] < vmin)
        {
          vmin = data[i];
        }
    }

  return vmin;
}

int32_t wave_dsp_max(const int32_t *data, int n)
{
  int32_t vmax = data != NULL && n > 0 ? data[0] : 0;
  int i;

  for (i = 1; i < n; i++)
    {
      if (data[i] > vmax)
        {
          vmax = data[i];
        }
    }

  return vmax;
}

double wave_dsp_zero_cross_freq(const int32_t *data, int n,
                                double sample_rate)
{
  /* Hysteresis in raw ADC units (~1.5% of 16-bit full scale) keeps
   * quantization noise from producing false crossings.
   */
  const double hyst = 1000.0;
  double mean;
  double crossings = 0.0;
  int prev_pos = 0;
  int i;

  if (data == NULL || n < 2 || sample_rate <= 0.0)
    {
      return 0.0;
    }

  mean = wave_dsp_mean(data, n);

  for (i = 0; i < n; i++)
    {
      double v = (double)data[i] - mean;
      int pos;

      if (v > hyst)
        {
          pos = 1;
        }
      else if (v < -hyst)
        {
          pos = -1;
        }
      else
        {
          continue; /* inside the dead zone; ignore */
        }

      if (prev_pos != 0 && pos != prev_pos)
        {
          crossings += 1.0;
        }

      prev_pos = pos;
    }

  /* One cycle produces two zero crossings.  The window spans n-1
   * sampling intervals.
   */
  return crossings * sample_rate / (2.0 * (n - 1));
}

int wave_dsp_analyze(const int32_t *data, int n, double sample_rate,
                     struct wave_stats_s *stats)
{
  if (data == NULL || stats == NULL || n <= 0)
    {
      return -1;
    }

  memset(stats, 0, sizeof(*stats));
  stats->nsamples = (uint16_t)n;
  stats->vmin_raw = wave_dsp_min(data, n);
  stats->vmax_raw = wave_dsp_max(data, n);
  stats->mean_raw = wave_dsp_mean(data, n);
  stats->rms_raw = wave_dsp_rms(data, n);
  stats->vpp_raw = (double)stats->vmax_raw - (double)stats->vmin_raw;
  stats->freq_zc = wave_dsp_zero_cross_freq(data, n, sample_rate);
  return 0;
}

void wave_dsp_fft(double *re, double *im, int n)
{
  int i;
  int j;
  int k;
  int m;
  int step;
  int half;
  double angle;
  double w_re;
  double w_im;
  double wr;
  double wi;
  double tr;
  double ti;

  if (re == NULL || im == NULL || n < 2 || (n & (n - 1)) != 0)
    {
      return;
    }

  /* Bit-reversal permutation. */
  for (i = 1, j = 0; i < n; i++)
    {
      int bit = n >> 1;

      for (; (j & bit) != 0; bit >>= 1)
        {
          j ^= bit;
        }

      j ^= bit;

      if (i < j)
        {
          tr = re[i]; re[i] = re[j]; re[j] = tr;
          ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

  /* Iterative butterflies. */
  for (step = 2; step <= n; step <<= 1)
    {
      half = step >> 1;
      angle = -2.0 * M_PI / step;
      w_re = cos(angle);
      w_im = sin(angle);
      wr = 1.0;
      wi = 0.0;

      for (m = 0; m < half; m++)
        {
          for (k = m; k < n; k += step)
            {
              j = k + half;
              tr = wr * re[j] - wi * im[j];
              ti = wr * im[j] + wi * re[j];
              re[j] = re[k] - tr;
              im[j] = im[k] - ti;
              re[k] += tr;
              im[k] += ti;
            }

          tr = wr * w_re - wi * w_im;
          ti = wr * w_im + wi * w_re;
          wr = tr;
          wi = ti;
        }
    }
}

int wave_dsp_fft_freq(const int32_t *data, int n, double sample_rate,
                      double *re, double *im, double *freq_hz,
                      double *peak_mag)
{
  double mean;
  double maxmag = 0.0;
  int peak = -1;
  int i;

  if (data == NULL || re == NULL || im == NULL || freq_hz == NULL ||
      n < 2 || n > WAVE_DSP_MAX_FFT || (n & (n - 1)) != 0 ||
      sample_rate <= 0.0)
    {
      return -1;
    }

  /* AC-couple the window, then transform. */
  mean = wave_dsp_mean(data, n);
  for (i = 0; i < n; i++)
    {
      re[i] = (double)data[i] - mean;
      im[i] = 0.0;
    }

  wave_dsp_fft(re, im, n);

  /* Find the largest bin in [1, n/2) (skip DC). */
  for (i = 1; i < n / 2; i++)
    {
      double mag = sqrt(re[i] * re[i] + im[i] * im[i]);

      if (mag > maxmag)
        {
          maxmag = mag;
          peak = i;
        }
    }

  if (peak <= 0)
    {
      return -1;
    }

  *freq_hz = (double)peak * sample_rate / n;
  if (peak_mag != NULL)
    {
      *peak_mag = maxmag;
    }

  return 0;
}
