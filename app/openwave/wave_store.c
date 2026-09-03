/****************************************************************************
 * wave_store.c -- OpenWave snapshot persistence (M6)
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/mtd/mtd.h>

#include "wave_store.h"

#ifndef CONFIG_OPENWAVE_STORE_DEVPATH
#  define CONFIG_OPENWAVE_STORE_DEVPATH "/dev/progmem0"
#endif

/* Safety: never erase below this offset.  The firmware image lives at
 * the start of flash; snapshots are stored in the very last block.
 */

#define WAVE_STORE_MIN_SAFE_OFFSET (1024u * 1024u)

/* Set after the first flash write failure: further snapshots are
 * console-only so the user is not spammed with the same error.
 */

static bool s_flash_broken;

static uint16_t wave_store_crc(const void *data, size_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0x811c9dc5u;
  size_t i;

  for (i = 0; i < len; i++)
    {
      crc ^= p[i];
      crc *= 0x01000193u;
    }

  return (uint16_t)(crc & 0xffffu);
}

int wave_store_init(struct wave_store_s *st)
{
  struct mtd_geometry_s geo;

  if (sizeof(struct wave_snapshot_s) != WAVE_SNAPSHOT_SIZE)
    {
      return -EINVAL;
    }

  if (st == NULL)
    {
      return -EINVAL;
    }

  memset(st, 0, sizeof(*st));
  st->fd = -1;

  st->fd = open(CONFIG_OPENWAVE_STORE_DEVPATH, O_RDWR);
  if (st->fd < 0)
    {
      return -errno;
    }

  st->opened = true;

  if (ioctl(st->fd, MTDIOC_GEOMETRY,
            (unsigned long)(uintptr_t)&geo) == 0 &&
      geo.erasesize > 0 && geo.neraseblocks > 0)
    {
      st->erasesize = geo.erasesize;
      st->neraseblocks = geo.neraseblocks;
      st->last_offset = (off_t)(geo.neraseblocks - 1) * geo.erasesize;

      /* Only use the tail of the flash, far away from the firmware. */

      st->geo_ok = (st->last_offset >= WAVE_STORE_MIN_SAFE_OFFSET);
    }

  return 0;
}

void wave_store_deinit(struct wave_store_s *st)
{
  if (st != NULL && st->opened)
    {
      close(st->fd);
      st->fd = -1;
      st->opened = false;
    }
}

int wave_store_save(struct wave_store_s *st,
                    const struct wave_snapshot_s *snap)
{
  struct wave_snapshot_s out;
  struct wave_snapshot_s check;
  struct wave_snapshot_s probe;
  uint32_t nslots;
  uint32_t slot;
  off_t offset;
  ssize_t nbytes;

  if (st == NULL || snap == NULL)
    {
      return -EINVAL;
    }

  /* Console record: always available. */

  printf("[snapshot] #%u ch=%u %04u-%02u-%02u %02u:%02u:%02u "
         "mean=%u.%03uV rms=%u.%03uV vpp=%u.%03uV f=%.1fHz n=%u\n",
         snap->seq, snap->channel,
         snap->tm_year + 1900, snap->tm_mon + 1, snap->tm_mday,
         snap->tm_hour, snap->tm_min, snap->tm_sec,
         snap->mean_mv / 1000, snap->mean_mv % 1000,
         snap->rms_mv / 1000, snap->rms_mv % 1000,
         snap->vpp_mv / 1000, snap->vpp_mv % 1000,
         (double)snap->freq_x10 / 10.0,
         snap->nsamples);

  if (!st->opened || !st->geo_ok || s_flash_broken)
    {
      return -ENODEV;
    }

  /* The progmem driver has no partial-erase ioctl, so we use a
   * write-once log in the last erase block: find the first slot whose
   * magic word is still 0xffffffff (erased state) and write there.
   * Re-programming a used slot is not possible, so once the block is
   * full the store degrades to console-only.
   */

  nslots = st->erasesize / (uint32_t)sizeof(struct wave_snapshot_s);
  if (nslots == 0)
    {
      printf("[store] snapshot too large for erase block\n");
      return -EINVAL;
    }

  offset = -1;
  for (slot = 0; slot < nslots; slot++)
    {
      off_t slot_off = st->last_offset + (off_t)slot *
                       (off_t)sizeof(struct wave_snapshot_s);

      memset(&probe, 0, sizeof(probe));
      nbytes = pread(st->fd, &probe, sizeof(probe), slot_off);
      if (nbytes == (ssize_t)sizeof(probe) &&
          probe.magic == 0xffffffffu)
        {
          offset = slot_off;
          break;
        }
    }

  if (offset < 0)
    {
      s_flash_broken = true;
      printf("[store] flash log full; console-only snapshots from now\n");
      return -ENOSPC;
    }

  /* Fill in magic and checksum. */

  out = *snap;
  out.magic = WAVE_SNAPSHOT_MAGIC;
  out.crc16 = 0;
  out.crc16 = wave_store_crc(&out, sizeof(out));

  nbytes = pwrite(st->fd, &out, sizeof(out), offset);
  if (nbytes != (ssize_t)sizeof(out))
    {
      /* Some progmem drivers report an error even when the data has
       * actually been programmed, so keep going: the read-back below is
       * the source of truth.
       */

      printf("[store] write returned %d at offset 0x%lx (verifying...)\n",
             nbytes < 0 ? -errno : -EIO, (unsigned long)offset);
    }

  /* Read it back and verify. */

  memset(&check, 0, sizeof(check));
  nbytes = pread(st->fd, &check, sizeof(check), offset);
  if (nbytes == (ssize_t)sizeof(check) &&
      check.magic == WAVE_SNAPSHOT_MAGIC)
    {
      uint16_t saved_crc = check.crc16;

      check.crc16 = 0;
      if (saved_crc == wave_store_crc(&check, sizeof(check)))
        {
          printf("[store] OK: slot %u at offset 0x%lx, %u bytes\n",
                 (unsigned)(offset - st->last_offset) /
                 (unsigned)sizeof(struct wave_snapshot_s),
                 (unsigned long)offset, (unsigned)sizeof(check));
          return 0;
        }
    }

  s_flash_broken = true;
  printf("[store] flash write/verify failed; console-only snapshots from "
         "now\n");
  return -EIO;
}
