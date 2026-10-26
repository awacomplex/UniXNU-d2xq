// SPDX-License-Identifier: BSD-2-Clause-Patent
// Samsung sec_ts (Y771_D) touch driver for d2xq - skeleton.
//
// Protocol extracted from the vendor kernel (y771_d/sec_ts.c, sec_ts.h).
// The board supplies i2c/irq/delay hooks; this file owns the command flow and
// the 16-byte event decoding.

#include "NativeSecTs.h"

static const sec_ts_platform *gPlat;
static int gReady;

static int ts_write_cmd(uint8_t cmd)
{
    if (gPlat == NULL || gPlat->i2c_write == NULL) {
        return -1;
    }
    return gPlat->i2c_write(SEC_TS_I2C_ADDR, &cmd, 1);
}

static int ts_read_cmd(uint8_t cmd, uint8_t *buf, uint32_t len)
{
    if (gPlat == NULL || gPlat->i2c_read == NULL) {
        return -1;
    }
    return gPlat->i2c_read(SEC_TS_I2C_ADDR, cmd, buf, len);
}

int sec_ts_init(const sec_ts_platform *plat)
{
    if (plat == NULL || plat->i2c_write == NULL || plat->i2c_read == NULL) {
        return -1;
    }
    gPlat = plat;

    /* TODO(P3): power on s2dos05-ldo2/ldo3 first (SPMI), then release reset. */
    if (gPlat->delay_ms != NULL) {
        gPlat->delay_ms(120);
    }
    if (ts_write_cmd(SEC_TS_CMD_SW_RESET) < 0) {
        return -2;
    }
    if (gPlat->delay_ms != NULL) {
        gPlat->delay_ms(300);
    }
    if (ts_write_cmd(SEC_TS_CMD_SENSE_ON) < 0) {
        return -3;
    }
    gReady = 1;
    return 0;
}

int sec_ts_sense_on(void)
{
    return ts_write_cmd(SEC_TS_CMD_SENSE_ON);
}

int sec_ts_sense_off(void)
{
    return ts_write_cmd(SEC_TS_CMD_SENSE_OFF);
}

int sec_ts_fod_info(uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    return ts_read_cmd(SEC_TS_CMD_FOD_INFO, data, len);
}

static void decode_coordinate(const uint8_t *b, sec_ts_event *ev)
{
    ev->point.id = (uint8_t)(((b[0] >> 2) & 0xF) - 1u);
    ev->point.action = (uint8_t)(b[0] >> 6);
    ev->point.x = (uint16_t)(((uint16_t)b[1] << 4) | (b[3] >> 4));
    ev->point.y = (uint16_t)(((uint16_t)b[2] << 4) | (b[3] & 0xF));
    ev->point.major = b[4];
    ev->point.minor = b[5];
    ev->point.pressure = (uint8_t)(b[6] & 0x3F);
    ev->point.ttype = (uint8_t)(((b[6] >> 6) << 2) | (b[7] >> 6));
}

int sec_ts_read_events(sec_ts_event *out, uint32_t max, uint32_t *count)
{
    uint8_t first[SEC_TS_EVENT_SIZE];
    uint8_t batch[SEC_TS_MAX_EVENTS][SEC_TS_EVENT_SIZE];
    uint32_t left;
    uint32_t i;

    if (!gReady || out == NULL || count == NULL || max == 0) {
        return -1;
    }
    *count = 0;

    if (ts_read_cmd(SEC_TS_CMD_READ_ONE_EVENT, first, sizeof(first)) < 0) {
        return -2;
    }
    left = first[7] & 0x1F;
    if (left > SEC_TS_MAX_EVENTS - 1) {
        /* Event stack overflow: clear it and report no events this round. */
        ts_write_cmd(SEC_TS_CMD_CLEAR_EVENTS);
        return 0;
    }

    if (left > 0) {
        if (ts_read_cmd(SEC_TS_CMD_READ_ALL_EVENT, (uint8_t *)batch,
                        left * SEC_TS_EVENT_SIZE) < 0) {
            return -3;
        }
    }

    for (i = 0; i < left + 1 && *count < max; i++) {
        const uint8_t *b = (i == 0) ? first : batch[i - 1];
        sec_ts_event *ev = &out[*count];

        ev->event_id = (uint8_t)(b[0] & 0x3);
        ev->left = (uint8_t)(b[7] & 0x1F);
        for (uint32_t k = 0; k < SEC_TS_EVENT_SIZE; k++) {
            ev->raw[k] = b[k];
        }
        if (ev->event_id == 0) {
            decode_coordinate(b, ev);
        }
        (*count)++;
    }
    return 0;
}
