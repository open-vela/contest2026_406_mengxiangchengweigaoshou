/*
 * wave_gen.c -- OpenWave test-signal generator implementation.
 *
 * Uses the standard NuttX PWM character driver interface:
 *   open() -> ioctl(PWMIOC_SETCHARACTERISTICS) -> ioctl(PWMIOC_START)
 *   -> ioctl(PWMIOC_STOP) -> close()
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <fixedmath.h>
#include <nuttx/timers/pwm.h>

#include "wave_gen.h"

int wave_gen_init(struct wave_gen_s *gen, const char *devpath)
{
  if (gen == NULL)
    {
      return -EINVAL;
    }

  memset(gen, 0, sizeof(*gen));
  gen->fd = -1;

  if (devpath == NULL)
    {
      devpath = WAVE_PWM_DEVPATH;
    }

  gen->fd = open(devpath, O_RDONLY);
  if (gen->fd < 0)
    {
      return -errno;
    }

  gen->opened = true;
  return 0;
}

void wave_gen_deinit(struct wave_gen_s *gen)
{
  if (gen != NULL && gen->opened)
    {
      if (gen->running)
        {
          wave_gen_stop(gen);
        }

      close(gen->fd);
      gen->fd = -1;
      gen->opened = false;
    }
}

int wave_gen_start(struct wave_gen_s *gen, uint32_t freq_hz,
                   uint32_t duty_pct)
{
  struct pwm_info_s info;
  int ret;

  if (gen == NULL || !gen->opened || freq_hz == 0)
    {
      return -EINVAL;
    }

  if (duty_pct > 100)
    {
      duty_pct = 100;
    }

  memset(&info, 0, sizeof(info));
  info.frequency = freq_hz;

  /* Convert a percentage to ub16_t duty as in apps/examples/pwm. */
  info.duty = duty_pct != 0 ? b16divi(uitoub16(duty_pct) - 1, 100) : 0;

  ret = ioctl(gen->fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)(uintptr_t)&info);
  if (ret < 0)
    {
      return -errno;
    }

  ret = ioctl(gen->fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      return -errno;
    }

  gen->freq = freq_hz;
  gen->duty = duty_pct;
  gen->running = true;
  return 0;
}

int wave_gen_stop(struct wave_gen_s *gen)
{
  int ret;

  if (gen == NULL || !gen->opened)
    {
      return -EINVAL;
    }

  ret = ioctl(gen->fd, PWMIOC_STOP, 0);
  if (ret < 0)
    {
      return -errno;
    }

  gen->running = false;
  return 0;
}
