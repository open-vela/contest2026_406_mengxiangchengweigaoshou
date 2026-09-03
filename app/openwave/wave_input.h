/****************************************************************************
 * wave_input.h -- OpenWave button input module (M6)
 *
 * Reads /dev/buttons and converts presses into short/long-press events.
 ****************************************************************************/

#ifndef __OPENWAVE_WAVE_INPUT_H
#define __OPENWAVE_WAVE_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Button event types reported to the caller. */

enum wave_input_event_e
{
  WAVE_INPUT_SHORT = 0,   /* press shorter than WAVE_INPUT_LONG_MS */
  WAVE_INPUT_LONG         /* press at least WAVE_INPUT_LONG_MS */
};

struct wave_input_s
{
  int             fd;          /* /dev/buttons descriptor */
  bool            opened;
  uint32_t        supported;   /* buttons reported by BTNIOC_SUPPORTED */
  uint32_t        pressed;     /* bitmask of buttons currently pressed */
  struct timespec t0[32];      /* press timestamp per button */
};

/* Open /dev/buttons.  Returns 0 on success, negative errno otherwise. */

int wave_input_init(struct wave_input_s *in);

void wave_input_deinit(struct wave_input_s *in);

/* Poll for one completed press (non-blocking).
 *
 * Returns:
 *   1  an event is ready: *event and *button are set
 *   0  no event yet (or input not opened)
 *  <0  negative errno on failure
 */

int wave_input_poll(struct wave_input_s *in,
                    enum wave_input_event_e *event, int *button);

#endif /* __OPENWAVE_WAVE_INPUT_H */
