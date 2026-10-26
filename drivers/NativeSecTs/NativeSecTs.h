// SPDX-License-Identifier: BSD-2-Clause-Patent
// Samsung sec_ts (Y771_D) touch controller interface for d2xq.

#ifndef NATIVE_SEC_TS_H
#define NATIVE_SEC_TS_H

#include <stdint.h>
#include <stddef.h>

#define SEC_TS_I2C_ADDR            0x48
#define SEC_TS_EVENT_SIZE          16
#define SEC_TS_MAX_EVENTS          31
#define SEC_TS_MAX_TOUCH           10

/* Commands */
#define SEC_TS_CMD_SENSE_ON        0x10
#define SEC_TS_CMD_SENSE_OFF       0x11
#define SEC_TS_CMD_SW_RESET        0x12
#define SEC_TS_CMD_TOUCHFUNCTION   0x30
#define SEC_TS_CMD_TSC_MODE        0x31
#define SEC_TS_CMD_REPORT_RATE     0x34
#define SEC_TS_CMD_COVERTYPE       0x38
#define SEC_TS_CMD_WAKEUP_GESTURE  0x39
#define SEC_TS_CMD_LOW_POWER_SENS  0x40
#define SEC_TS_CMD_FOD_INFO        0x15
#define SEC_TS_CMD_FOD_POSITION    0x19
#define SEC_TS_CMD_READ_ONE_EVENT  0x60
#define SEC_TS_CMD_READ_ALL_EVENT  0x61
#define SEC_TS_CMD_CLEAR_EVENTS    0x62
#define SEC_TS_CMD_LPM_AOD         0x9B

/* Touch types */
#define SEC_TS_TOUCH_NORMAL        0
#define SEC_TS_TOUCH_HOVER         1
#define SEC_TS_TOUCH_FLIPCOVER     2
#define SEC_TS_TOUCH_GLOVE         3
#define SEC_TS_TOUCH_STYLUS        4
#define SEC_TS_TOUCH_PALM          5
#define SEC_TS_TOUCH_WET           6
#define SEC_TS_TOUCH_PROXIMITY     7

/* Actions (tchsta) */
#define SEC_TS_ACTION_NONE         0
#define SEC_TS_ACTION_PRESS        1
#define SEC_TS_ACTION_MOVE         2
#define SEC_TS_ACTION_RELEASE      3

typedef struct {
    uint8_t  id;        /* tid - 1 */
    uint8_t  action;    /* SEC_TS_ACTION_* */
    uint8_t  ttype;     /* SEC_TS_TOUCH_* */
    uint16_t x;
    uint16_t y;
    uint8_t  pressure;
    uint8_t  major;
    uint8_t  minor;
} sec_ts_point;

typedef struct {
    uint8_t  event_id;  /* 0 coordinate, 1 status */
    uint8_t  left;      /* remaining events */
    uint8_t  raw[SEC_TS_EVENT_SIZE];
    sec_ts_point point;
} sec_ts_event;

/* Platform hooks the board must provide. */
typedef struct {
    int  (*i2c_write)(uint8_t addr, const uint8_t *buf, uint32_t len);
    int  (*i2c_read)(uint8_t addr, uint8_t cmd, uint8_t *buf, uint32_t len);
    void (*delay_ms)(uint32_t ms);
    void (*irq_enable)(int enable);
} sec_ts_platform;

int  sec_ts_init(const sec_ts_platform *plat);
int  sec_ts_sense_on(void);
int  sec_ts_sense_off(void);
int  sec_ts_read_events(sec_ts_event *out, uint32_t max, uint32_t *count);
int  sec_ts_fod_info(uint8_t *data, uint32_t len);

#endif /* NATIVE_SEC_TS_H */
