/*
 * wave_ui.h -- OpenWave SSD1306 OLED drawing (via /dev/lcd0).
 *
 * M5 milestone: draws the captured waveform and measured parameters on
 * the 128x64 monochrome OLED through the NuttX LCD character driver.
 */

#ifndef __APPS_OPENWAVE_WAVE_UI_H
#define __APPS_OPENWAVE_WAVE_UI_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* Display geometry (SSD1306 128x64). */
#define WAVE_UI_WIDTH   128
#define WAVE_UI_HEIGHT  64
#define WAVE_UI_STRIDE  (WAVE_UI_WIDTH / 8)
#define WAVE_UI_FBSIZE  (WAVE_UI_STRIDE * WAVE_UI_HEIGHT)

#define WAVE_UI_DEVPATH "/dev/lcd0"

struct wave_ui_s
{
  int  fd;       /* open file descriptor for /dev/lcd0 */
  bool opened;   /* true once the device is open */
  uint16_t xres; /* reported horizontal resolution */
  uint16_t yres; /* reported vertical resolution */

  /* 1-bpp shadow frame buffer (bit 7 = leftmost pixel of a byte). */
  uint8_t fb[WAVE_UI_FBSIZE];
};

/* Open /dev/lcd0, query the video info and power the display on.
 * Returns 0 on success or a negative errno value.
 */
int wave_ui_init(struct wave_ui_s *ui);

/* Power the display off and close it. */
void wave_ui_deinit(struct wave_ui_s *ui);

/* Clear the shadow frame buffer (does not touch the display). */
void wave_ui_clear(struct wave_ui_s *ui);

/* Set one pixel in the shadow frame buffer. */
void wave_ui_pixel(struct wave_ui_s *ui, int x, int y, bool on);

/* Draw a line (Bresenham) in the shadow frame buffer. */
void wave_ui_line(struct wave_ui_s *ui, int x0, int y0, int x1, int y1,
                  bool on);

/* Draw a null-terminated string with the built-in 5x7 font.  Lowercase
 * letters are drawn as uppercase.  Only ASCII 0x20..0x5F are supported.
 */
void wave_ui_text(struct wave_ui_s *ui, int x, int y, const char *s);

/* Plot n samples scaled into the rectangle [x0..x1] x [y0..y1]. */
void wave_ui_waveform(struct wave_ui_s *ui, const int32_t *data, int n,
                      int x0, int x1, int y0, int y1);

/* Push the whole shadow frame buffer to the display via LCDDEVIO_PUTAREA.
 * Returns 0 on success or a negative errno value.
 */
int wave_ui_flush(struct wave_ui_s *ui);

#endif /* __APPS_OPENWAVE_WAVE_UI_H */
