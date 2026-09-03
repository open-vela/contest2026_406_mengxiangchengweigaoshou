/****************************************************************************
 * wave_input.c -- OpenWave button input module (M6)
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/buttons.h>

#include "wave_input.h"

#ifndef CONFIG_OPENWAVE_BUTTON_DEVPATH
#  define CONFIG_OPENWAVE_BUTTON_DEVPATH "/dev/buttons"
#endif

/* Long-press threshold in milliseconds. */

#define WAVE_INPUT_LONG_MS  500

/* Use the monotonic clock when available, like wave_core does. */

#if defined(CLOCK_MONOTONIC)
#  define WAVE_INPUT_CLOCK CLOCK_MONOTONIC
#else
#  define WAVE_INPUT_CLOCK CLOCK_REALTIME
#endif

int wave_input_init(struct wave_input_s *in)
{
  if (in == NULL)
    {
      return -EINVAL;
    }

  memset(in, 0, sizeof(*in));
  in->fd = -1;

  in->fd = open(CONFIG_OPENWAVE_BUTTON_DEVPATH, O_RDONLY);
  if (in->fd < 0)
    {
      return -errno;
    }

  /* Which buttons does the hardware report?  Fall back to button 0. */

  if (ioctl(in->fd, BTNIOC_SUPPORTED,
            (unsigned long)(uintptr_t)&in->supported) < 0 ||
      in->supported == 0)
    {
      in->supported = 1;
    }

  in->opened = true;
  return 0;
}

void wave_input_deinit(struct wave_input_s *in)
{
  if (in != NULL && in->opened)
    {
      close(in->fd);
      in->fd = -1;
      in->opened = false;
    }
}

int wave_input_poll(struct wave_input_s *in,
                    enum wave_input_event_e *event, int *button)
{
  struct pollfd fds;
  struct timespec now;
  btn_buttonset_t cur;
  int64_t elapsed_ms;
  ssize_t nread;
  uint32_t bit;
  int ret;

  if (in == NULL || event == NULL || button == NULL || !in->opened)
    {
      return 0;
    }

  fds.fd = in->fd;
  fds.events = POLLIN;
  fds.revents = 0;
  ret = poll(&fds, 1, 0);
  if (ret <= 0)
    {
      return 0;   /* nothing pending */
    }

  nread = read(in->fd, &cur, sizeof(cur));
  if (nread != (ssize_t)sizeof(cur))
    {
      return 0;
    }

  /* Detect rising/falling edges for every supported button. */

  for (bit = 0; bit < 32; bit++)
    {
      uint32_t mask = 1u << bit;

      if ((in->supported & mask) == 0)
        {
          continue;
        }

      if ((cur & mask) != 0 && (in->pressed & mask) == 0)
        {
          /* Pressed: remember the press time. */

          clock_gettime(WAVE_INPUT_CLOCK, &in->t0[bit]);
          in->pressed |= mask;
        }
      else if ((cur & mask) == 0 && (in->pressed & mask) != 0)
        {
          /* Released: classify the press duration. */

          clock_gettime(WAVE_INPUT_CLOCK, &now);
          elapsed_ms = (int64_t)(now.tv_sec - in->t0[bit].tv_sec) * 1000 +
                       (now.tv_nsec - in->t0[bit].tv_nsec) / 1000000;
          in->pressed &= ~mask;
          *button = (int)bit;
          *event = (elapsed_ms >= WAVE_INPUT_LONG_MS) ?
                   WAVE_INPUT_LONG : WAVE_INPUT_SHORT;
          return 1;
        }
    }

  return 0;
}
