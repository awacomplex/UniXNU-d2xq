# NativeSecTs — 三星 sec_ts（Y771_D）触摸驱动

d2xq 专用组件。协议来自内核源码 `drivers/input/touchscreen/sec_ts/y771_d/`（`N9760ZCS3DTJ1`），
完整契约见 `kernel-src/touch-interface.md`。

## 硬件

| 项 | 值 |
| --- | --- |
| I2C 地址 | 0x48 |
| 中断 | GPIO 0x57，type 0x2008 |
| 坐标 | 0x1000 × 0x1000 |
| 供电 | `s2dos05-ldo2`（DVDD）/ `s2dos05-ldo3`（AVDD） |
| 项目 | `"DAVINCI", "N970"`，tclm_level 1 |

## 寄存器命令（写 command 后再读写数据）

| 命令 | 值 | 用途 |
| --- | --- | --- |
| SENSE_ON | 0x10 | 开启 |
| SENSE_OFF | 0x11 | 关闭 |
| SW_RESET | 0x12 | 复位 |
| TOUCHFUNCTION | 0x30 | 功能位（含 FOD） |
| TSC_MODE | 0x31 | 扫描模式 |
| REPORT_RATE | 0x34 | 报点率 |
| COVERTYPE | 0x38 | 保护膜 |
| WAKEUP_GESTURE | 0x39 | 手势 |
| LOW_POWER_SENSITIVITY | 0x40 | 低功耗 |
| SPONGE_FOD_INFO | 0x15 | 屏下指纹信息 |
| SPONGE_FOD_POSITION | 0x19 | 屏下指纹位置 |
| READ_ONE_EVENT | 0x60 | 读 1 个事件（16B） |
| READ_ALL_EVENT | 0x61 | 批量读 |
| CLEAR_EVENT_STACK | 0x62 | 溢出清栈 |
| LPM_AOD_OFF_ON | 0x9B | AOD |

## 事件包

16 字节。`eid = b[0] & 3`：0 = 坐标、1 = 状态。

坐标：`x = (b[1] << 4) | (b[3] >> 4)`、`y = (b[2] << 4) | (b[3] & 0xF)`、`action = b[0] >> 6`、
`tid = (b[0] >> 2) & 0xF`、`z = b[6] & 0x3F`、`ttype = ((b[6] >> 6) << 2) | (b[7] >> 6)`、
`left = b[7] & 0x1F`。

## 文件

- `NativeSecTs.h`：寄存器/事件定义
- `NativeSecTs.c`：I2C 访问、事件解析、初始化序列骨架
- `build-native-sects.py`：构建脚本（由 UniXNU `[kernel].archives` 调用）

## 待补

- 固件是否需从 `/vendor/firmware` 加载
- `s2dos05` 上电/断电序列（SPMI）
- ~~触摸挂载的 GENI I2C SE 编号~~ → **i2c@0xc80000**（qupv3_2，实测 DT）
