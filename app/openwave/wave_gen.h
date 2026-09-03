/*
 * wave_gen.h -- OpenWave test-signal generator (TIM1 PWM).
 *
 * M3 milestone: controls /dev/pwm0 to generate a square-wave test
 * signal that can be looped back into the ADC for self-closed-loop
 * verification.
 */

#ifndef __APPS_OPENWAVE_WAVE_GEN_H
#define __APPS_OPENWAVE_WAVE_GEN_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* Default PWM device path (may be overridden on the command line). */
#define WAVE_PWM_DEVPATH   "/dev/pwm0"

struct wave_gen_s
{
  int  fd;        /* open file descriptor for the PWM device */
  bool opened;    /* true once the device is open */
  bool running;   /* true while the pulse train is running */
  uint32_t freq;  /* current frequency in Hz */
  uint32_t duty;  /* current duty cycle in percent */
};

/* Open the PWM device.  devpath may be NULL to use WAVE_PWM_DEVPATH.
 * Returns 0 on success or a negative errno value.
 */
int wave_gen_init(struct wave_gen_s *gen, const char *devpath);

/* Close the PWM device (stops the output first if it is running). */
void wave_gen_deinit(struct wave_gen_s *gen);

/* Start a pulse train.  freq_hz must be > 0; duty_pct is clamped to
 * [0, 100].  Returns 0 on success or a negative errno value.
 */
int wave_gen_start(struct wave_gen_s *gen, uint32_t freq_hz,
                   uint32_t duty_pct);

/* Stop the pulse train.  Returns 0 on success or a negative errno. */
int wave_gen_stop(struct wave_gen_s *gen);

#endif /* __APPS_OPENWAVE_WAVE_GEN_H */
