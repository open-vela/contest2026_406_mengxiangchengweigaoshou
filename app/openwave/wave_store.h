/****************************************************************************
 * wave_store.h -- OpenWave snapshot persistence (M6)
 *
 * Stores small snapshots (statistics + RTC timestamp) into the last erase
 * block of the internal program flash (/dev/progmem0).  If the MTD
 * device is unavailable, snapshots are printed to the console only.
 ****************************************************************************/

#ifndef __OPENWAVE_WAVE_STORE_H
#define __OPENWAVE_WAVE_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define WAVE_SNAPSHOT_MAGIC  0x4f57534eu  /* "OWSN" */
#define WAVE_SNAPSHOT_SIZE   32

/* The STM32H7 progmem driver programs one aligned 32-byte flash word per
 * write, so the snapshot is packed into exactly 32 bytes: the whole
 * record fits into a single program operation and neighbouring slots
 * are never touched.
 */

struct wave_snapshot_s
{
  uint32_t magic;      /* WAVE_SNAPSHOT_MAGIC (filled by wave_store_save) */
  uint32_t seq;        /* snapshot sequence number */
  uint8_t  channel;    /* ADC channel */
  uint8_t  flags;      /* reserved */
  uint16_t tm_year;    /* years since 1900 */
  uint8_t  tm_mon;     /* month, 0-11 */
  uint8_t  tm_mday;    /* day of month, 1-31 */
  uint8_t  tm_hour;    /* hour, 0-23 */
  uint8_t  tm_min;     /* minute, 0-59 */
  uint8_t  tm_sec;     /* second, 0-59 */
  uint8_t  pad[3];
  uint16_t mean_mv;    /* mean voltage, millivolts */
  uint16_t rms_mv;     /* RMS voltage, millivolts */
  uint16_t vpp_mv;     /* peak-to-peak voltage, millivolts */
  uint16_t freq_x10;   /* zero-crossing frequency, 0.1 Hz */
  uint16_t nsamples;   /* samples used */
  uint16_t crc16;      /* checksum (filled by wave_store_save) */
};

struct wave_store_s
{
  int      fd;             /* /dev/progmem0 descriptor */
  bool     opened;
  bool     geo_ok;         /* geometry known and safe to use */
  uint32_t erasesize;      /* erase block size (bytes) */
  uint32_t neraseblocks;   /* number of erase blocks */
  off_t    last_offset;    /* byte offset of the last erase block */
  uint32_t seq;            /* next snapshot sequence number */
};

/* Open /dev/progmem0 and read its geometry.  Returns 0 on success,
 * negative errno otherwise.
 */

int  wave_store_init(struct wave_store_s *st);

void wave_store_deinit(struct wave_store_s *st);

/* Save one snapshot: print it to the console and, when flash storage is
 * available, erase the last erase block and write the snapshot there.
 * Returns 0 on success, negative errno on failure, -ENODEV when running
 * in console-only mode (storage unavailable).  On success st->seq is
 * incremented by the caller.
 */

int  wave_store_save(struct wave_store_s *st,
                     const struct wave_snapshot_s *snap);

#endif /* __OPENWAVE_WAVE_STORE_H */
