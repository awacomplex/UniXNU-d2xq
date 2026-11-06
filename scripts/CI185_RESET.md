# CI185: Samsung Download restart after guarded ingest

This change replaces only `ci184-reset-download.inc`, the success hook included
after CI184 has verified the source and target hashes and flushed/read back the
repair progress or append counter. It does not change the data protocol, chunk
manifest, hashes, builder, or CI184 guard messages. It performs no MISC writes.
CI185 identifies the new hardware path with `CI185 P0`, `P1`, `P2` and CI185 reset
errors. The existing include filename is retained for builder compatibility.

## Pinned same-device evidence

Repository: `huangkaikaihuang/android_kernel_samsung_d2xq`, ref `N9760ZCS3DTJ1`.
Verified reference files are saved on the build host under
`E:\d2xq-work\ci185-references`; adjacent `.meta.json` files record source URLs,
GitHub blob SHA and local SHA256. The references were independently reviewed by
the reset research and ingest review agents before this draft was written.

| Source | Git blob | Relevant evidence |
| --- | --- | --- |
| [drivers/spmi/spmi-pmic-arb.c](https://github.com/huangkaikaihuang/android_kernel_samsung_d2xq/blob/N9760ZCS3DTJ1/drivers/spmi/spmi-pmic-arb.c) | `1ff30da74e4693ed83e08e1391e83e3950017804` | 28–102: version, ownership, opcodes, status, 512 APIDs, 1000 us; 250–296: bounded poll; 344–445: command/data order; 900–950: duplicate PPID ownership; 997–1034: v5 offsets and command format; 1120–1123: APID map offset |
| [drivers/samsung/debug/sec_debug.c](https://github.com/huangkaikaihuang/android_kernel_samsung_d2xq/blob/N9760ZCS3DTJ1/drivers/samsung/debug/sec_debug.c) | `d89f79cb1d0bfa10841cc2956a67b543a67c5ef6` | 294–296: `download` maps to PON restart reason; 431–435: PON setter |
| [include/linux/samsung/debug/sec_debug.h](https://github.com/huangkaikaihuang/android_kernel_samsung_d2xq/blob/N9760ZCS3DTJ1/include/linux/samsung/debug/sec_debug.h) | `1bb55042be537b5ddcb412a3dce622505bcaf269` | 203: `PON_RESTART_REASON_DOWNLOAD = 0x15` |
| [drivers/input/misc/qpnp-power-on.c](https://github.com/huangkaikaihuang/android_kernel_samsung_d2xq/blob/N9760ZCS3DTJ1/drivers/input/misc/qpnp-power-on.c) | `c51c222e4b5ce78fe184be51b76dd95c2ed9647c` | 58–68, 468–471: Gen2 subtype 04/05, subtype offset 05; 107: SOFT_RB_SPARE offset 8f; 429–438: regmap masked write; 485–501: Gen2 mask fe, reason shifted left one |
| [arch/arm64/boot/dts/qcom/sm8150.dtsi](https://github.com/huangkaikaihuang/android_kernel_samsung_d2xq/blob/N9760ZCS3DTJ1/arch/arm64/boot/dts/qcom/sm8150.dtsi) | `51a08b2458d41239238f57985b4365b7f74bc72b` | 1564–1580: core 0c440000/1100, channels 0c600000/2000000, observer 0e600000/100000, configuration 0c40a000/26000, EE 0 |
| [arch/arm64/boot/dts/qcom/pm8150.dtsi](https://github.com/huangkaikaihuang/android_kernel_samsung_d2xq/blob/N9760ZCS3DTJ1/arch/arm64/boot/dts/qcom/pm8150.dtsi) | `4f780f07ecb8d715db9790624ead7985b67ceac7` | 25–50: PM8150 SID 0, PON base 0800, system reset and restart reason storage |

All register addresses/values below are hexadecimal unless stated otherwise.

## Sequence and limits

1. `P0`: retain explicit device MMIO maps for core (1100 bytes, spanning two
   pages), configuration (one page), watchdog (one page), and the selected RW and
   observer pages. Refuse a missing map or version outside `[50000000,60000000)`.
   Linux selects v5 for all values at least 50000000; the upper bound is an extra
   CI185 restriction against unreviewed later major versions.
2. Scan exactly 512 core entries at `900 + 4*APID`; ignore zero entries and require
   `((entry >> 8) & fff) == 008` (SID 0 / peripheral 08). Read ownership from
   configuration `700 + 4*APID`, low three bits. Like the Linux v5 mapper, select
   the last matching APID with write owner EE 0, including APID 0. Duplicates for
   different EEs are legal and are counted. No owned match stops the hook.
3. RW address is `0c600000 + 10000*APID`; observer address is
   `0e600000 + 10000*EE + 80*APID`. The observer mapping is page-aligned, then its
   channel offset is added. Only the selected channel page is mapped in each
   region; APID 511 remains within the DTS regions.
4. Disable/configure/read back the proven APSS watchdog before the PMIC write:
   EN 08 = 0, BARK 10 = `5*32765`, BITE 14 = `32765` (tick counts are decimal).
   Any configuration mismatch stops before modifying the persistent PON byte.
   This retains CI184's watchdog sequence, whose reset was observed on the device.
5. Observer EXT_READL reads SID 0 address 0805 (CMD `08000050`); accept only Gen2
   subtype 04/05. Read 088f (CMD `080008f0`). Compute
   `new = (old & 01) | 2a`, preserving bit zero. Recheck the selected complete map
   entry, masked owner and hardware version before the PMIC write. `P1` prints
   version, APID, EE, match counts, subtype, old and new bytes.
6. Write new to RW WDATA0 (+10), barrier, then CMD (+00) = `000008f0` (one-byte
   EXT_WRITEL). Poll STATUS (+08) at most 1000 times with `IODelay(1)` (decimal
   microseconds); require DONE bit 01 and no error bits 0e. All reads use the same
   bound. Data is read from observer RDATA0 (+18). No undocumented STATUS clear,
   CONFIG change, SEC_ACCESS unlock or SCM call is introduced. The UEFI keypad
   driver's opcode 13 EXT_READ format is not applicable to this v5 long-address
   transaction.
7. Read 088f again through observer EXT_READL and require exact whole-byte
   equality. `P2` prints old/new/readback, APID, version and `OKslot:counter R`.
   Only then enable/kick APSS WDT (EN 08 = 1, RST 04 = 1). If the device has not
   reset after the bounded 10-second/fixed-iteration wait, panic with WDT state.

Every SPMI/ownership/subtype/readback failure leaves the device stopped, without
enabling the watchdog. A failure after the PMIC write can leave the requested
Download reason stored; it does not roll that byte back or try another reason.
The hook is reached only after progress is durable, so a reset failure does not
invalidate that progress. A real-device test is required to establish that
this PON reason plus APSS WDT reaches Samsung Download.

## Compatibility and current device progress

Continue using CI184 metadata and `ci184-prepare.py`. The 64 MiB payload + 4 KiB
metadata, complete source/target SHA256 checks, `D2REPR84`, `D2SAFE84`, repair
ordering and old-metadata rejection are unchanged. CI184 guard diagnostics in a
CI185 kernel are expected and do not imply the old reset hook was packaged.

The latest observed durable state is counter 7, `nextRepairSlot = 1`: repair slot
0 completed under CI184. The next real-device transfer must be **repair slot 1**,
then 2–6 after the reset landing has been verified, followed by append 7–139.
Do not reflash repair slot 0 or its old fallback recovery package. Existing slot 0
metadata must continue to stop at the CI184 repair sequence guard.
