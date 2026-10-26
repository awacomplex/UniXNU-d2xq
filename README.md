# UniXNU-d2xq

Samsung Galaxy Note10+ 5G（SM-N9760 / `d2xq` / SM8150）的 UniXNU 设备仓库骨架。

结构照 `RigoLigoRLC/unixnu-sm-t733`（三星 format 2 设备仓），SoC 共用部分在
`../UniXNU-sm8150-common`，通过 `feeds.toml` 的 `local` 源引用。

## 实机事实（2026-10-25 侦察）

| 项 | 值 |
| --- | --- |
| 平台 | `msmnile` / SM8150 |
| Bootloader | `N9760ZCU6HZE1`，已解锁（`verifiedbootstate=orange`，`warranty_bit=1`） |
| 引导设备 | `1d84000.ufshc`（UFS 3.0） |
| 面板 | `ss_dsi_panel_S6E3HA9_AMB675TG01_WQHD`（1440×3040） |
| 触摸 | `sec_ts` @ I2C `0x48` |
| 内存 | MemTotal 11,634,912 kB（物理 12 GB） |
| 整盘 / userdata | 249,835,520 KB / 240,934,892 KB（`sda33`） |
| fastboot | 不可用（`androidboot.fastbootd=false`）→ Odin 封装 |

## 当前状态

骨架，`board.toml` 里 `example = true`（未资格化）。待办：

- P0：root 下读 GPT 与 `/proc/device-tree`，核对 userdata LBA、DRAM span
- P1：用内核 `sm8150.dtsi` 补全 `../UniXNU-sm8150-common/dts/sm8150.dtsi`
- P2：Samsung/Odin 封装（参考 sm-t733 `scripts/`）
- P3：显示/触摸（面板 `S6E3HA9`、`sec_ts`）
- P4：XNU prelink/ADT/trust cache

## 目录

```
board.toml          格式 2 板级配置（含 TODO）
feeds.toml          固定 SM8150 共用源
configs/            d2xq_defconfig
dts/d2xq.dts        板级 DTS（骨架）
porting.toml        移植记录（按子系统）
proprietary-files.txt
../UniXNU-sm8150-common/   SoC 共用仓
../tools/recon-d2xq.ps1    实机侦察脚本
../recon/                  侦察输出
```
