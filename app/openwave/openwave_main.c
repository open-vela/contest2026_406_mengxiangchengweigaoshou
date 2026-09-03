/*
 * openwave_main.c -- OpenWave application entry point.
 *
 * M6 milestone: adds button interaction (short = switch channel,
 * long = save snapshot), LED threshold alarm, RTC timestamps and
 * flash snapshot storage on top of the M5 OLED waveform display.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/leds/userled.h>
#include <nuttx/timers/rtc.h>

#include "wave_core.h"
#include "wave_gen.h"
#include "wave_input.h"
#include "wave_store.h"
#include "wave_ui.h"

#define OPENWAVE_NAME    "OpenWave"
#define OPENWAVE_VERSION "0.6.0"

#ifndef CONFIG_OPENWAVE_ADC_DEVPATH
#  define CONFIG_OPENWAVE_ADC_DEVPATH WAVE_ADC_DEVPATH
#endif

#ifndef CONFIG_OPENWAVE_ADC_NSAMPLES
#  define CONFIG_OPENWAVE_ADC_NSAMPLES 64
#endif

#ifndef CONFIG_OPENWAVE_PWM_DEVPATH
#  define CONFIG_OPENWAVE_PWM_DEVPATH WAVE_PWM_DEVPATH
#endif

#ifndef CONFIG_OPENWAVE_PWM_DEFAULT_FREQ
#  define CONFIG_OPENWAVE_PWM_DEFAULT_FREQ 0
#endif

#ifndef CONFIG_OPENWAVE_PWM_DEFAULT_DUTY
#  define CONFIG_OPENWAVE_PWM_DEFAULT_DUTY 50
#endif

#ifndef CONFIG_OPENWAVE_ANALYZE_WINDOW
#  define CONFIG_OPENWAVE_ANALYZE_WINDOW 256
#endif

#ifndef CONFIG_OPENWAVE_ANALYZE_CHANNEL
#  define CONFIG_OPENWAVE_ANALYZE_CHANNEL 5
#endif

#ifndef CONFIG_OPENWAVE_ALARM_VPP_MV
#  define CONFIG_OPENWAVE_ALARM_VPP_MV 2500
#endif

#ifndef CONFIG_OPENWAVE_LED_DEVPATH
#  define CONFIG_OPENWAVE_LED_DEVPATH "/dev/userleds"
#endif

#ifndef CONFIG_OPENWAVE_RTC_DEVPATH
#  define CONFIG_OPENWAVE_RTC_DEVPATH "/dev/rtc0"
#endif

/* ADC channels selectable by a short button press. */

static const uint8_t g_channels[] = { 5, 10, 12, 13, 15 };

/* FFT scratch buffers are static so they do not eat task stack space. */

static double s_fft_re[WAVE_DSP_MAX_FFT];
static double s_fft_im[WAVE_DSP_MAX_FFT];

static void show_usage(const char *progname)
{
  printf("Usage: %s [OPTIONS]\n", progname);
  printf("  -T           Run the wiring self-test (OLED + PWM/ADC) and exit\n");
  printf("  -d <device>  ADC device path (default: %s)\n",
         CONFIG_OPENWAVE_ADC_DEVPATH);
  printf("  -n <samples> Number of samples to print; 0 = run forever"
         " (default: %d)\n", CONFIG_OPENWAVE_ADC_NSAMPLES);
  printf("  -t           Issue ANIOC_TRIGGER before every read\n");
  printf("  -p <device>  PWM device path (default: %s)\n",
         CONFIG_OPENWAVE_PWM_DEVPATH);
  printf("  -f <freq>    Start PWM test signal at freq Hz"
         " (default: %d = off)\n", CONFIG_OPENWAVE_PWM_DEFAULT_FREQ);
  printf("  -D <duty>    PWM duty in percent, 0-100 (default: %d)\n",
         CONFIG_OPENWAVE_PWM_DEFAULT_DUTY);
  printf("  -w <window>  Samples per stats window; 0 = raw sample mode"
         " (default: %d)\n", CONFIG_OPENWAVE_ANALYZE_WINDOW);
  printf("  -c <channel> ADC channel to analyze (default: %d)\n",
         CONFIG_OPENWAVE_ANALYZE_CHANNEL);
  printf("  -h           Show this help and exit\n");
  printf("M6 notes: short button press switches channel; long press saves\n");
  printf("          a snapshot; LED lights when Vpp exceeds %d mV.\n",
         CONFIG_OPENWAVE_ALARM_VPP_MV);
}

static void run_selftest(void)
{
  struct wave_ui_s ui;
  struct wave_gen_s gen;
  struct wave_core_s core;
  struct wave_sample_s s;
  int32_t vmin = INT32_MAX;
  int32_t vmax = INT32_MIN;
  int i;
  int ret;

  printf("== OpenWave wiring self-test ==\n");

  /* --- Part 1: OLED --- */

  ret = wave_ui_init(&ui);
  if (ret < 0)
    {
      printf("[FAIL] OLED init: %d (is /dev/lcd0 present? check LCD config)\n",
             -ret);
    }
  else
    {
      printf("[OK] OLED driver ready (%u x %u)\n", ui.xres, ui.yres);

      memset(ui.fb, 0xff, sizeof(ui.fb));
      wave_ui_flush(&ui);
      printf("[..] All-white screen (1 s)...\n");
      sleep(1);

      wave_ui_clear(&ui);
      wave_ui_flush(&ui);
      printf("[..] All-black screen (1 s)...\n");
      sleep(1);

      wave_ui_clear(&ui);
      wave_ui_line(&ui, 0, 0, WAVE_UI_WIDTH - 1, 0, true);
      wave_ui_line(&ui, 0, WAVE_UI_HEIGHT - 1,
                   WAVE_UI_WIDTH - 1, WAVE_UI_HEIGHT - 1, true);
      wave_ui_line(&ui, 0, 0, 0, WAVE_UI_HEIGHT - 1, true);
      wave_ui_line(&ui, WAVE_UI_WIDTH - 1, 0,
                   WAVE_UI_WIDTH - 1, WAVE_UI_HEIGHT - 1, true);
      wave_ui_text(&ui, 6, 6, "OPENWAVE TEST");
      wave_ui_text(&ui, 6, 20, "SCK PB3 MOSI PB5");
      wave_ui_text(&ui, 6, 34, "CS PE11 DC PE12");
      wave_ui_text(&ui, 6, 48, "RES PE13");
      wave_ui_flush(&ui);
      printf("[..] Border + text (2 s)...\n");
      sleep(2);

      for (i = 0; i < 4; i++)
        {
          memset(ui.fb, (i & 1) != 0 ? 0xff : 0x00, sizeof(ui.fb));
          wave_ui_flush(&ui);
          usleep(300000);
        }

      wave_ui_deinit(&ui);
      printf("[?] OLED: expect white -> black -> border+text -> 4 flashes.\n");
      printf("    Blank screen: check VCC/GND/RES/CS. "
             "Garbage: check SCK/MOSI/DC.\n");
    }

  /* --- Part 2: PWM -> ADC loopback (jumper PE9 -> PB1) --- */

  ret = wave_gen_init(&gen, NULL);
  if (ret < 0)
    {
      printf("[FAIL] PWM open: %d\n", -ret);
      return;
    }

  ret = wave_core_init(&core, NULL);
  if (ret < 0)
    {
      printf("[FAIL] ADC open: %d\n", -ret);
      wave_gen_deinit(&gen);
      return;
    }

  ret = wave_gen_start(&gen, 1000, 50);
  if (ret < 0)
    {
      printf("[FAIL] PWM start: %d\n", -ret);
      wave_core_deinit(&core);
      wave_gen_deinit(&gen);
      return;
    }

  printf("[..] PWM 1 kHz / 50 %% running. Reading channel 5...\n");
  for (i = 0; i < 32; )
    {
      ret = wave_core_read(&core, &s, 1);
      if (ret <= 0)
        {
          break;
        }

      if (s.ch != 5)
        {
          continue; /* only count channel-5 samples */
        }

      if (s.raw < vmin)
        {
          vmin = s.raw;
        }

      if (s.raw > vmax)
        {
          vmax = s.raw;
        }

      printf("  ch%u raw=%" PRId32 " (%" PRIu32 " mV)\n",
             s.ch, s.raw, wave_core_to_mv(s.raw));
      i++;
    }

  wave_gen_stop(&gen);
  wave_core_deinit(&core);
  wave_gen_deinit(&gen);

  printf("[?] Loopback: expect raw values near 0 and near 65535.\n");
  printf("    vmin=%" PRId32 " vmax=%" PRId32 "\n", vmin, vmax);
  if (vmin != INT32_MAX && (vmax - vmin) > 30000)
    {
      printf("[OK] PWM loopback looks good (jumper PE9->PB1 OK).\n");
    }
  else
    {
      printf("[!] No big swing: check jumper PE9->PB1 and ADC wiring.\n");
    }
}

static int wave_main_read_rtc(int rtc_fd, struct wave_snapshot_s *snap)
{
  struct rtc_time rt;

  if (rtc_fd < 0)
    {
      return -ENODEV;
    }

  if (ioctl(rtc_fd, RTC_RD_TIME, (unsigned long)(uintptr_t)&rt) < 0)
    {
      return -errno;
    }

  snap->tm_year = (uint16_t)rt.tm_year;
  snap->tm_mon  = (uint8_t)rt.tm_mon;
  snap->tm_mday = (uint8_t)rt.tm_mday;
  snap->tm_hour = (uint8_t)rt.tm_hour;
  snap->tm_min  = (uint8_t)rt.tm_min;
  snap->tm_sec  = (uint8_t)rt.tm_sec;
  return 0;
}

static uint16_t wave_main_clamp_mv(double volts)
{
  double mv = volts * 1000.0;

  if (mv < 0.0)
    {
      mv = 0.0;
    }
  else if (mv > 65535.0)
    {
      mv = 65535.0;
    }

  return (uint16_t)(mv + 0.5);
}

static uint16_t wave_main_clamp_freq(double hz)
{
  double tenths = hz * 10.0;

  if (tenths < 0.0)
    {
      tenths = 0.0;
    }
  else if (tenths > 65535.0)
    {
      tenths = 65535.0;
    }

  return (uint16_t)(tenths + 0.5);
}

int main(int argc, char *argv[])
{
  const char *adc_devpath = CONFIG_OPENWAVE_ADC_DEVPATH;
  const char *pwm_devpath = CONFIG_OPENWAVE_PWM_DEVPATH;
  long remaining = CONFIG_OPENWAVE_ADC_NSAMPLES;
  long pwm_freq = CONFIG_OPENWAVE_PWM_DEFAULT_FREQ;
  long pwm_duty = CONFIG_OPENWAVE_PWM_DEFAULT_DUTY;
  long window = CONFIG_OPENWAVE_ANALYZE_WINDOW;
  long channel = CONFIG_OPENWAVE_ANALYZE_CHANNEL;
  bool infinite;
  bool swtrig = true;   /* H7A3 board ADC needs ANIOC_TRIGGER to convert */
  struct wave_core_s core;
  struct wave_gen_s gen;
  struct wave_sample_s samples[WAVE_ADC_BATCH];
  struct wave_ui_s ui;
  struct wave_input_s input;
  struct wave_store_s store;
  int32_t win[WAVE_DSP_MAX_FFT];
  struct wave_stats_s stats;
  double rate;
  double ffreq;
  bool pwm_on = false;
  bool ui_on = false;
  int led_fd = -1;
  int rtc_fd = -1;
  uint32_t ch_index = 0;
  unsigned int ch_count;
  char line[32];
  int argi;
  int ret;
  int n;
  int i;

  ch_count = (unsigned int)(sizeof(g_channels) / sizeof(g_channels[0]));

  for (argi = 1; argi < argc; argi++)
    {
      if (strcmp(argv[argi], "-T") == 0)
        {
          run_selftest();
          return 0;
        }
      else if (strcmp(argv[argi], "-d") == 0 && argi + 1 < argc)
        {
          adc_devpath = argv[++argi];
        }
      else if (strcmp(argv[argi], "-n") == 0 && argi + 1 < argc)
        {
          remaining = strtol(argv[++argi], NULL, 10);
          if (remaining < 0)
            {
              remaining = 0;
            }
        }
      else if (strcmp(argv[argi], "-t") == 0)
        {
          swtrig = true;
        }
      else if (strcmp(argv[argi], "-p") == 0 && argi + 1 < argc)
        {
          pwm_devpath = argv[++argi];
        }
      else if (strcmp(argv[argi], "-f") == 0 && argi + 1 < argc)
        {
          pwm_freq = strtol(argv[++argi], NULL, 10);
          if (pwm_freq < 0)
            {
              pwm_freq = 0;
            }
        }
      else if (strcmp(argv[argi], "-D") == 0 && argi + 1 < argc)
        {
          pwm_duty = strtol(argv[++argi], NULL, 10);
          if (pwm_duty < 0)
            {
              pwm_duty = 0;
            }
          else if (pwm_duty > 100)
            {
              pwm_duty = 100;
            }
        }
      else if (strcmp(argv[argi], "-w") == 0 && argi + 1 < argc)
        {
          window = strtol(argv[++argi], NULL, 10);
          if (window < 0)
            {
              window = 0;
            }
          else if (window > WAVE_DSP_MAX_FFT)
            {
              window = WAVE_DSP_MAX_FFT;
            }
        }
      else if (strcmp(argv[argi], "-c") == 0 && argi + 1 < argc)
        {
          channel = strtol(argv[++argi], NULL, 10);
          if (channel < 0 || channel > 255)
            {
              channel = CONFIG_OPENWAVE_ANALYZE_CHANNEL;
            }
        }
      else if (strcmp(argv[argi], "-h") == 0)
        {
          show_usage(argv[0]);
          return 0;
        }
      else
        {
          printf("Unknown option: %s\n", argv[argi]);
          show_usage(argv[0]);
          return 1;
        }
    }

  printf("%s v%s\n", OPENWAVE_NAME, OPENWAVE_VERSION);
  printf("M6: acquisition + DSP + OLED + PWM + buttons -- adc %s, "
         "%ld %s (%s)\n",
         adc_devpath, remaining,
         window > 0 ? "window(s)" : "samples",
         remaining == 0 ? "unlimited" : "then exit");

  /* Optional test-signal generator (PWM -> jumper -> ADC). */

  if (pwm_freq > 0)
    {
      ret = wave_gen_init(&gen, pwm_devpath);
      if (ret < 0)
        {
          printf("open %s failed: %d\n", pwm_devpath, -ret);
          return 2;
        }

      ret = wave_gen_start(&gen, (uint32_t)pwm_freq, (uint32_t)pwm_duty);
      if (ret < 0)
        {
          printf("PWM start failed: %d\n", -ret);
          wave_gen_deinit(&gen);
          return 2;
        }

      pwm_on = true;
      printf("PWM test signal: %s, %ld Hz, %ld %%\n",
             pwm_devpath, pwm_freq, pwm_duty);
    }

  ret = wave_core_init(&core, adc_devpath);
  if (ret < 0)
    {
      printf("open %s failed: %d\n", adc_devpath, -ret);
      if (pwm_on)
        {
          wave_gen_deinit(&gen);
        }

      return 2;
    }

  core.swtrig = swtrig;
  printf("ADC ready: %d configured channel(s)\n", core.nchannels);

  /* Try to bring up the OLED; not fatal if it is absent. */

  if (wave_ui_init(&ui) == 0)
    {
      ui_on = true;
      printf("OLED ready: %u x %u\n", ui.xres, ui.yres);
    }

  /* M6: buttons, LEDs, RTC and snapshot store (all optional). */

  if (wave_input_init(&input) < 0)
    {
      printf("input: /dev/buttons unavailable (button control disabled)\n");
    }
  else
    {
      printf("input: buttons ready\n");
    }

  if (wave_store_init(&store) < 0)
    {
      printf("store: flash unavailable (console-only snapshots)\n");
    }
  else
    {
      printf("store: flash ready (last block at offset 0x%lx, "
             "%u bytes)\n",
             (unsigned long)store.last_offset, (unsigned)store.erasesize);
    }

  led_fd = open(CONFIG_OPENWAVE_LED_DEVPATH, O_WRONLY);
  if (led_fd < 0)
    {
      printf("led: /dev/userleds unavailable (alarm LED disabled)\n");
    }

  rtc_fd = open(CONFIG_OPENWAVE_RTC_DEVPATH, O_RDONLY);
  if (rtc_fd < 0)
    {
      printf("rtc: /dev/rtc0 unavailable (snapshots without timestamp)\n");
    }

  /* Map the requested channel to the short-press cycle list. */

  for (i = 0; i < (int)ch_count; i++)
    {
      if (g_channels[i] == (uint8_t)channel)
        {
          ch_index = (uint32_t)i;
          break;
        }
    }

  infinite = (remaining == 0);
  if (window > 0)
    {
      printf("Analysis mode: channel %ld, %ld samples/window\n",
             channel, window);

      while (infinite || remaining > 0)
        {
          struct timespec t0;
          struct timespec t1;
          double elapsed;
          int pending_ev = -1;
          int pending_btn = -1;
          int filled = 0;

          /* Collect the window in small batches and poll the button
           * after every read, otherwise short presses (< 500 ms) are
           * missed between the long analysis windows.
           */

          clock_gettime(CLOCK_REALTIME, &t0);

          while (filled < (int)window)
            {
              enum wave_input_event_e ev;
              int btn;

              n = wave_core_read(&core, samples, WAVE_ADC_BATCH);
              if (n < 0)
                {
                  printf("ADC read failed: %d\n", -n);
                  goto analysis_done;
                }

              for (i = 0; i < n && filled < (int)window; i++)
                {
                  if (samples[i].ch == (uint8_t)channel)
                    {
                      win[filled++] = samples[i].raw;
                    }
                }

              if (input.opened &&
                  wave_input_poll(&input, &ev, &btn) == 1 &&
                  pending_ev < 0)
                {
                  pending_ev = (int)ev;
                  pending_btn = btn;
                }
            }

          clock_gettime(CLOCK_REALTIME, &t1);
          elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                    (double)(t1.tv_nsec - t0.tv_nsec) / 1000000000.0;
          rate = (elapsed > 0.0) ? (double)filled / elapsed : 0.0;

          ret = wave_core_analyze(win, (int)window, rate, &stats);
          if (ret < 0)
            {
              printf("analyze failed: %d\n", -ret);
              break;
            }

          printf("[stats] ch=%ld n=%u rate=%.0f Hz | "
                 "mean=%.3f V rms=%.3f V vpp=%.3f V f=%.1f Hz",
                 channel, stats.nsamples, rate,
                 wave_core_raw_to_volt(stats.mean_raw),
                 wave_core_raw_to_volt(stats.rms_raw),
                 wave_core_raw_to_volt(stats.vpp_raw),
                 stats.freq_zc);

          if (wave_dsp_fft_freq(win, (int)window, rate,
                                s_fft_re, s_fft_im, &ffreq, NULL) == 0)
            {
              printf(" | f_fft=%.1f Hz", ffreq);
            }

          printf("\n");
          fflush(stdout);

          if (ui_on)
            {
              wave_ui_clear(&ui);
              snprintf(line, sizeof(line), "CH=%ld RATE=%.0f HZ",
                       channel, rate);
              wave_ui_text(&ui, 0, 0, line);
              wave_ui_waveform(&ui, win, (int)window, 0, 127, 8, 55);
              snprintf(line, sizeof(line), "RMS=%.2f P=%.2f F=%.0f",
                       wave_core_raw_to_volt(stats.rms_raw),
                       wave_core_raw_to_volt(stats.vpp_raw),
                       stats.freq_zc);
              wave_ui_text(&ui, 0, 56, line);
              wave_ui_flush(&ui);
            }

          /* M6: handle the pending button event (uses current stats). */

          if (pending_ev >= 0)
            {
              if (pending_ev == WAVE_INPUT_SHORT)
                {
                  ch_index = (ch_index + 1) % ch_count;
                  channel = g_channels[ch_index];
                  printf("[input] short: channel -> %ld\n", channel);
                }
              else
                {
                  struct wave_snapshot_s snap;

                  memset(&snap, 0, sizeof(snap));
                  wave_main_read_rtc(rtc_fd, &snap);
                  snap.seq = store.seq;
                  snap.channel = (uint8_t)channel;
                  snap.mean_mv = wave_main_clamp_mv(
                      wave_core_raw_to_volt(stats.mean_raw));
                  snap.rms_mv = wave_main_clamp_mv(
                      wave_core_raw_to_volt(stats.rms_raw));
                  snap.vpp_mv = wave_main_clamp_mv(
                      wave_core_raw_to_volt(stats.vpp_raw));
                  snap.freq_x10 = wave_main_clamp_freq(stats.freq_zc);
                  snap.nsamples = stats.nsamples > 65535u ?
                                  65535u : (uint16_t)stats.nsamples;

                  ret = wave_store_save(&store, &snap);
                  if (ret == 0)
                    {
                      store.seq++;
                    }

                  printf("[input] long press: snapshot #%" PRIu32
                         " (btn %d)\n",
                         snap.seq, pending_btn);

                  /* Blink all LEDs to confirm the snapshot. */

                  if (led_fd >= 0)
                    {
                      ioctl(led_fd, ULEDIOC_SETALL, 0x7);
                      usleep(200000);
                      ioctl(led_fd, ULEDIOC_SETALL, 0x0);
                    }
                }
            }

          /* M6: threshold alarm -- light LED0 when Vpp is too high. */

          if (led_fd >= 0)
            {
              bool alarm = (wave_core_raw_to_volt(stats.vpp_raw) * 1000.0) >
                           CONFIG_OPENWAVE_ALARM_VPP_MV;

              ioctl(led_fd, ULEDIOC_SETALL,
                    (unsigned long)(alarm ? 0x1 : 0x0));
            }

          if (!infinite && --remaining == 0)
            {
              break;
            }
        }

analysis_done:
      ;
    }
  else
    {
      /* M2/M3 raw-sample printing mode. */

      while (infinite || remaining > 0)
        {
          n = wave_core_read(&core, samples, WAVE_ADC_BATCH);
          if (n < 0)
            {
              printf("ADC read failed: %d\n", -n);
              break;
            }

          for (i = 0; i < n; i++)
            {
              printf("ch %u: raw %" PRId32 "  %" PRIu32 " mV\n",
                     samples[i].ch, samples[i].raw,
                     wave_core_to_mv(samples[i].raw));

              if (!infinite && --remaining == 0)
                {
                  break;
                }
            }

          fflush(stdout);
        }
    }

  if (led_fd >= 0)
    {
      ioctl(led_fd, ULEDIOC_SETALL, 0x0);
      close(led_fd);
    }

  if (rtc_fd >= 0)
    {
      close(rtc_fd);
    }

  wave_input_deinit(&input);
  wave_store_deinit(&store);
  wave_core_deinit(&core);
  if (ui_on)
    {
      wave_ui_deinit(&ui);
    }

  if (pwm_on)
    {
      wave_gen_deinit(&gen);
    }

  printf("OpenWave stopped.\n");
  return 0;
}
