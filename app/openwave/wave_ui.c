/*
 * wave_ui.c -- OpenWave SSD1306 OLED drawing implementation.
 *
 * Draws into a 1-KB shadow frame buffer and flushes it to the display
 * through the /dev/lcd0 character driver (LCDDEVIO_PUTAREA).
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/video/fb.h>

#include "wave_ui.h"

/* Classic 5x7 font, characters 0x20..0x5F, column-major, LSB = top row. */
static const uint8_t g_wave_font[64][5] =
{
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
  {0x00, 0x00, 0x5f, 0x00, 0x00}, /* ! */
  {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
  {0x14, 0x7f, 0x14, 0x7f, 0x14}, /* # */
  {0x24, 0x2a, 0x7f, 0x2a, 0x12}, /* $ */
  {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
  {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
  {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
  {0x00, 0x1c, 0x22, 0x41, 0x00}, /* ( */
  {0x00, 0x41, 0x22, 0x1c, 0x00}, /* ) */
  {0x08, 0x2a, 0x1c, 0x2a, 0x08}, /* * */
  {0x08, 0x08, 0x3e, 0x08, 0x08}, /* + */
  {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
  {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
  {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
  {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
  {0x3e, 0x51, 0x49, 0x45, 0x3e}, /* 0 */
  {0x00, 0x42, 0x7f, 0x40, 0x00}, /* 1 */
  {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
  {0x21, 0x41, 0x45, 0x4b, 0x31}, /* 3 */
  {0x18, 0x14, 0x12, 0x7f, 0x10}, /* 4 */
  {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
  {0x3c, 0x4a, 0x49, 0x49, 0x30}, /* 6 */
  {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
  {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
  {0x06, 0x49, 0x49, 0x29, 0x1e}, /* 9 */
  {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
  {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
  {0x00, 0x08, 0x14, 0x22, 0x41}, /* < */
  {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
  {0x41, 0x22, 0x14, 0x08, 0x00}, /* > */
  {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
  {0x32, 0x49, 0x79, 0x41, 0x3e}, /* @ */
  {0x7e, 0x11, 0x11, 0x11, 0x7e}, /* A */
  {0x7f, 0x49, 0x49, 0x49, 0x36}, /* B */
  {0x3e, 0x41, 0x41, 0x41, 0x22}, /* C */
  {0x7f, 0x41, 0x41, 0x22, 0x1c}, /* D */
  {0x7f, 0x49, 0x49, 0x49, 0x41}, /* E */
  {0x7f, 0x09, 0x09, 0x01, 0x01}, /* F */
  {0x3e, 0x41, 0x41, 0x51, 0x32}, /* G */
  {0x7f, 0x08, 0x08, 0x08, 0x7f}, /* H */
  {0x00, 0x41, 0x7f, 0x41, 0x00}, /* I */
  {0x20, 0x40, 0x41, 0x3f, 0x01}, /* J */
  {0x7f, 0x08, 0x14, 0x22, 0x41}, /* K */
  {0x7f, 0x40, 0x40, 0x40, 0x40}, /* L */
  {0x7f, 0x02, 0x0c, 0x02, 0x7f}, /* M */
  {0x7f, 0x04, 0x08, 0x10, 0x7f}, /* N */
  {0x3e, 0x41, 0x41, 0x41, 0x3e}, /* O */
  {0x7f, 0x09, 0x09, 0x09, 0x06}, /* P */
  {0x3e, 0x41, 0x51, 0x21, 0x5e}, /* Q */
  {0x7f, 0x09, 0x19, 0x29, 0x46}, /* R */
  {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
  {0x01, 0x01, 0x7f, 0x01, 0x01}, /* T */
  {0x3f, 0x40, 0x40, 0x40, 0x3f}, /* U */
  {0x1f, 0x20, 0x40, 0x20, 0x1f}, /* V */
  {0x3f, 0x40, 0x38, 0x40, 0x3f}, /* W */
  {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
  {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
  {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
  {0x00, 0x7f, 0x41, 0x41, 0x00}, /* [ */
  {0x02, 0x04, 0x08, 0x10, 0x20}, /* backslash */
  {0x00, 0x41, 0x41, 0x7f, 0x00}, /* ] */
  {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
  {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
};

int wave_ui_init(struct wave_ui_s *ui)
{
  struct fb_videoinfo_s vinfo;

  if (ui == NULL)
    {
      return -EINVAL;
    }

  memset(ui, 0, sizeof(*ui));
  ui->fd = -1;

  ui->fd = open(WAVE_UI_DEVPATH, O_RDWR);
  if (ui->fd < 0)
    {
      return -errno;
    }

  if (ioctl(ui->fd, LCDDEVIO_GETVIDEOINFO,
            (unsigned long)(uintptr_t)&vinfo) < 0)
    {
      close(ui->fd);
      ui->fd = -1;
      return -errno;
    }

  ui->xres = vinfo.xres;
  ui->yres = vinfo.yres;
  ui->opened = true;

  /* Turn the display on and clear it. */
  ioctl(ui->fd, LCDDEVIO_SETPOWER, 1);
  wave_ui_clear(ui);
  wave_ui_flush(ui);
  return 0;
}

void wave_ui_deinit(struct wave_ui_s *ui)
{
  if (ui != NULL && ui->opened)
    {
      ioctl(ui->fd, LCDDEVIO_SETPOWER, 0);
      close(ui->fd);
      ui->fd = -1;
      ui->opened = false;
    }
}

void wave_ui_clear(struct wave_ui_s *ui)
{
  if (ui != NULL)
    {
      memset(ui->fb, 0, sizeof(ui->fb));
    }
}

void wave_ui_pixel(struct wave_ui_s *ui, int x, int y, bool on)
{
  uint8_t mask;
  int byte;
  int bit;

  if (ui == NULL || x < 0 || x >= WAVE_UI_WIDTH ||
      y < 0 || y >= WAVE_UI_HEIGHT)
    {
      return;
    }

  byte = y * WAVE_UI_STRIDE + (x >> 3);
  bit = 7 - (x & 7); /* MSB-first: bit 7 is the leftmost pixel */
  mask = (uint8_t)(1u << bit);

  if (on)
    {
      ui->fb[byte] |= mask;
    }
  else
    {
      ui->fb[byte] &= (uint8_t)~mask;
    }
}

void wave_ui_line(struct wave_ui_s *ui, int x0, int y0, int x1, int y1,
                  bool on)
{
  int dx;
  int dy;
  int sx;
  int sy;
  int err;
  int e2;

  dx = x1 > x0 ? x1 - x0 : x0 - x1;
  dy = y1 > y0 ? y1 - y0 : y0 - y1;
  sx = x0 < x1 ? 1 : -1;
  sy = y0 < y1 ? 1 : -1;
  err = dx - dy;

  for (; ; )
    {
      wave_ui_pixel(ui, x0, y0, on);
      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      e2 = 2 * err;
      if (e2 > -dy)
        {
          err -= dy;
          x0 += sx;
        }

      if (e2 < dx)
        {
          err += dx;
          y0 += sy;
        }
    }
}

void wave_ui_text(struct wave_ui_s *ui, int x, int y, const char *s)
{
  int col;
  int row;

  if (ui == NULL || s == NULL)
    {
      return;
    }

  for (; *s != '\0' && x < WAVE_UI_WIDTH; s++)
    {
      char c = *s;
      int idx;

      if (c >= 'a' && c <= 'z')
        {
          c = (char)(c - 'a' + 'A');
        }

      idx = c - 0x20;
      if (idx < 0 || idx > 63)
        {
          x += 6; /* unsupported character: advance */
          continue;
        }

      for (col = 0; col < 5 && x + col < WAVE_UI_WIDTH; col++)
        {
          uint8_t colbits = g_wave_font[idx][col];

          for (row = 0; row < 7; row++)
            {
              if ((colbits & (1u << row)) != 0)
                {
                  wave_ui_pixel(ui, x + col, y + row, true);
                }
            }
        }

      x += 6;
    }
}

void wave_ui_waveform(struct wave_ui_s *ui, const int32_t *data, int n,
                      int x0, int x1, int y0, int y1)
{
  int32_t vmin;
  int32_t vmax;
  int span;
  int px;
  int prev_y = -1;

  if (ui == NULL || data == NULL || n <= 0 || x1 <= x0 || y1 <= y0)
    {
      return;
    }

  vmin = data[0];
  vmax = data[0];
  for (px = 1; px < n; px++)
    {
      if (data[px] < vmin)
        {
          vmin = data[px];
        }

      if (data[px] > vmax)
        {
          vmax = data[px];
        }
    }

  span = (int)(vmax - vmin);
  if (span < 1)
    {
      span = 1;
    }

  for (px = x0; px <= x1; px++)
    {
      int si = (px - x0) * (n - 1) / (x1 - x0);
      int32_t v = data[si];
      int y;

      y = y1 - (int)((int64_t)(v - vmin) * (y1 - y0) / span);
      if (y < y0)
        {
          y = y0;
        }
      else if (y > y1)
        {
          y = y1;
        }

      if (prev_y >= y0)
        {
          wave_ui_line(ui, px - 1, prev_y, px, y, true);
        }
      else
        {
          wave_ui_pixel(ui, px, y, true);
        }

      prev_y = y;
    }
}

int wave_ui_flush(struct wave_ui_s *ui)
{
  struct lcddev_area_s area;

  if (ui == NULL || !ui->opened)
    {
      return -EINVAL;
    }

  area.row_start = 0;
  area.row_end = WAVE_UI_HEIGHT - 1;
  area.col_start = 0;
  area.col_end = WAVE_UI_WIDTH - 1;
  area.stride = WAVE_UI_STRIDE;
  area.data = ui->fb;

  if (ioctl(ui->fd, LCDDEVIO_PUTAREA,
            (unsigned long)(uintptr_t)&area) < 0)
    {
      return -errno;
    }

  return 0;
}
