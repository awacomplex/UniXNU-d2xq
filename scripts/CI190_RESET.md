# CI190: the same-device TCSR fallback for an unavailable DLOAD call

The CI189 photo showed `RESET REFUSE SCM unavailable dload=0 sdi=1 arb=0 ps=1`,
with the availability transport returning status0. The single-use RECOVERY ARM
record was already consumed, and CI189 stopped before `C0`: it did not perform
a state-changing reset hardware write. The trusted progress remains repair
counter 7, next repair slot 2. This result does not establish a reset landing.

CI190 preserves CI189's reset-only hardware path and makes two source-backed
capability branches match the normal same-device Linux driver. It uses a new
CI190 ARM record/nonce in the outer gate; the old consumed CI189 record does
not authorize it. There is no ingest, userdata/misc/progress write or repair2
transfer. This is not a read-only build.

The new CI190 ARM record is exactly 4096 bytes with SHA256
`e89c31a3533211eb379636df3e57b79a3d9efa3f43d11e72cf932c6af2975e49`.
Only this CI190 record may authorize the new gate. Its consumption retains the
CI189 protocol: zero the complete fixed, previously tested RECOVERY first
block, flush, and read back the entire block before entering hardware. The
gate does not re-prove the runtime partition name; the fixed-address scope and
limits recorded in CI189 remain applicable.

## The two differences

1. Query DLOAD, SDI, arbiter and PS_HOLD availability with the existing strict
   signed-low32 transport checks. Only when DLOAD availability is exactly zero,
   query IO_WRITE base FNID `02000502`; require positive availability before
   any state-changing call. If DLOAD is available, retain BOOT10 `(0,0)` and
   never query or use IO_WRITE. If it is unavailable, use the same-device
   `scm_io_write(0x01fd3000, 0)` branch. Arginfo is 2; arg0 is the physical
   TCSR address and arg1 is a full 32-bit zero. Atomic FNID is `c2000502` for
   the discovered 64-bit ABI or `82000502` for 32-bit. Only status0 continues.
2. If arbiter-disable availability is exactly zero, print `CI190 B-SKIP` and
   skip that unsupported call, matching the source's conditional. If positive,
   retain PWR01 `(0)` and require success. Negative availability, transport
   errors, busy, unknown returned statuses and interrupted exhaustion remain
   terminal. There is no fallback from a failed supported operation.

`CI190 C1-DLOAD` means the supported DLOAD-clear call returned status0.
`CI190 C1-TCSR status0` means the secure IO_WRITE returned status0. Neither
marker claims that TCSR was read back or that Download was entered.

## Exact same-device evidence

All references are pinned to `huangkaikaihuang/android_kernel_samsung_d2xq`
commit `5b548735a5a70cddc5dbaa78d1ffd1770298522d`, ref `N9760ZCS3DTJ1`.
Original bytes/blob/SHA256 metadata are in
`E:\d2xq-work\ci189-references\SCM_ABI_MANIFEST.json` and
`dt-audit/SOURCE_MANIFEST.json`; the unchanged CI189 path and source review are
documented in [CI189_RESET.md](CI189_RESET.md).

| Source | Git blob | Exact evidence |
| --- | --- | --- |
| `drivers/power/reset/msm-poweroff.c` | `6e1e8d54aca87c5acba627308976da95688a8674` | 144-146: unsupported DLOAD uses `scm_io_write(tcsr_boot_misc_detect, arg1)`; 155-167: normal clear supplies zero; 725-728: resource start supplies address; 291-295 and 733-734: only a positively available arbiter-disable call executes |
| `arch/arm64/boot/dts/qcom/sm8150.dtsi` | `51a08b2458d41239238f57985b4365b7f74bc72b` | 1341-1345: `tcsr-boot-misc-detect` is `01fd3000/4` |
| `drivers/soc/qcom/scm.c` | `92c01c7f0ea725518f4d066c0a4483f130e04073` | 557-558: IO_READ=1, IO_WRITE=2; 572-583: IO_WRITE arguments `(address,u32value)`, arginfo2, atomic call; 587-598: availability result semantics |
| `include/soc/qcom/scm.h` | `dcdc851e7e243dea6bb863f4d13061de47c4e5ee` | 19: IO service5; 48: SIP FNID encoding; 63-76: value-argument metadata |

This is an explicitly source-backed full-word zero write, not an inferred
mask or read-modify-write. CI190 does not map TCSR directly, preserve guessed
bits, enable a Qualcomm crash/EDL cookie, try another address, or guess IDs.
The generic IO_READ interface is present in the same source, but that alone
does not establish the safety or read side effects of reading this particular
TCSR address. The normal Linux fallback does not perform a readback. CI190
therefore adds no IO_READ and reports status success without claiming a
verified TCSR register value.

## Preserved safeguards and sequence

`ci190-reset-hardware.inc` is the CI189 hardware implementation with the CI190
tag/arm-variable change and the two capability branches above. It requires
`ci190ArmConsumed`, the existing validated repair flag, counter7 and next2.
The outer CI190 gate owns the fresh ARM protocol and must consume/flush/read
back its fixed RECOVERY record before providing that flag; consult its source
and record JSON for exact authorization bytes. The hardware include itself
contains no storage operation. A subsequent boot with the consumed record
must stop before hardware, even if bootloader clears the PON reason.

The implementation preserves the EL1 check, raw 19.2 MHz counter self-test,
global monotonic checks, bounded microdelays, retained inhibit-cache MMIO
mapping, 512-entry SPMI owner scan, version/subtype validation, per-transaction
mapping revalidation, exact allowed addresses and full-byte PON readback.
It preserves the source's SMC32/64 self-discovery, non-atomic INFO queries,
atomic actual calls, all-register interrupted continuation and bounded
128-call/ten-counter-second wait. No busy retry or unknown-ID probe is added.

The resulting order is clear secure DLOAD (supported call or documented TCSR
fallback), IMEM normal zero, PMIC hard-reset configuration, IMEM reboot magic
`77665501`, PON Download reason15, secure SDI disable, conditional arbiter
disable, and secure/direct PS_HOLD under the already reviewed source
conditions. The PMIC byte sequence still clears 085b bit80, waits at least
500 microseconds, sets 085a low nibble7 preserving high bits, then sets 085b
bit80 preserving other bits. SID0 088f preserves bit0 and stores reason as
`(old & 1) | 2a`. All PMIC/IMEM checks precede the arbiter branch; no SPMI
operation occurs after that branch, including when the unsupported call is
skipped. No WDT, PSCI, HVC, IODelay or scheduler wait is introduced.

A supported PS_HOLD call returning an error terminates. Direct PS_HOLD is
allowed only when that capability was explicitly unavailable, or a supported
call returned success without resetting, as in the reviewed same-device
fall-through. Every returning failure is terminal, without rollback or a
second attempt. Software bounds cannot interrupt an SMC/MMIO access that does
not return. The static board audit and prior exported-DT address corroboration
do not establish the proprietary firmware's reset destination.

## Required result

ARM consumption, `C1-TCSR`, `P2`, or a PS_HOLD stage alone cannot prove success.
Require a fresh host-observed Samsung Download enumeration after this one
attempt. The existing two-row screen ring can retain stale suffixes; identify
the newest CI190 tag and complete panic text. Record validation/build hashes
and real-device outcome separately before selecting any next data transfer.
