# CI187: raw architectural-counter delay diagnostic

CI186's latest device photo is `E:\qq下载\1794029741983.jpg`; the host crop is
`CI186-bottom.png` in the task workspace. Both observer READLs completed:
`SA1=00000004` identified a Gen2 PON subtype and `RA1=00000000` read the old
restart reason. The latest marker is `QD0` without `QD1`, at the unconditional
final `IODelay(1)` check. The display reuses a two-row ring and does not clear an
old row tail; the physically last `RC0 RC1 RB0` characters are residual text,
not evidence that the completed reason read was still executing.

CI187 keeps the diagnostic read-only and substitutes only its delay mechanism.
It cannot ingest repair slot 2, write PON, change WDT or disk state, or reset.
The persistent state remains trusted counter 7, `nextRepairSlot = 2`.

## Entry and retained protections

The new source is `ci187-readonly-probe.inc`. The existing guard's single marker
after `C1` is renamed to `CI187_READONLY_PROBE`; the builder requires exactly
one occurrence and expands that new source. The CI186 probe is preserved as a
historical source file, but is no longer expanded in this branch.

All pre-entry counter magic, image SHA, checksum, inverse, reserved-byte and
geometry checks remain intact. The probe itself requires
`repairCounter && counter == 7 && nextRepairSlot == 2` before counter-register
reads or MMIO. A rejected state leaves `d2xq_ufs_stage` unchanged. An accepted
state sets that RAM diagnostic flag to -1 to avoid the startup watchdog masking
the operation under test. This does not write a hardware watchdog register.

The CI186 mapping addresses/lengths and retained maps, strict v5 version range,
all 512 APID reads, PPID 008 selection, last EE 0 ownership rule, WDT EN/BARK/BITE
reads, observer READL commands, status checks, barriers and subtype check are
unchanged. See [CI186_READONLY.md](CI186_READONLY.md) for the full MMIO and marker
map and [CI185_RESET.md](CI185_RESET.md) for pinned same-device source evidence.
The CI185 reset include is unchanged and unreachable in this build.

The sole hardware control write remains observer CMD +00 for either of these
one-byte EXT_READL commands: subtype 0805 -> `08000050`, reason 088f -> `080008f0`.
There are no RW/PON/WDT/STATUS/CONFIG or disk writes. Every completed top-level
probe path ends in a panic, before any later metadata or ingest processing.

## Raw busy wait and limits

All `IODelay` calls in the expanded diagnostic are removed, including the
mandatory final check. No XNU time framework, absolute-time conversion,
scheduler, WFE, or sleeping primitive is called by the replacement.

1. `CI187 F0` precedes `MRS CNTFRQ_EL0`. `CI187 F1` prints the returned frequency
   and the calculated tick threshold. Only **19,200,000 Hz** is accepted; any
   other value causes `CI187 READONLY FAIL frequency=...` before mapping MMIO.
2. Each one-microsecond replacement uses
   `ceil(frequency / 1,000,000) + 1`, exactly **21 ticks** at the accepted
   frequency. The extra tick avoids returning early due to tick phase.
3. `ISB; MRS CNTVCT_EL0` obtains the starting counter. Each of at most **4096**
   rounds executes `NOP`, then another `ISB; MRS CNTVCT_EL0` sample. Return only
   if the counter is monotonic and `now - start >= 21`. No `start + ticks`
   expression is formed, avoiding deadline overflow.
4. Any sample below the preceding sample, including counter wrap, stops with
   `CI187 READONLY FAIL delay <tag> backward ...`. Equal adjacent samples are
   allowed; they can occur within one counter tick. If all 4096 rounds fail to
   advance, stop with `... stalled ...`. If the counter advances but the 21-tick
   target is still unmet at that limit, stop with `... exhausted ...`.
5. Each SPMI transaction still has at most 1000 STATUS polls. A pending poll
   uses the raw delay above. A delay failure panics immediately and cannot
   proceed to another command, a storage operation, or a reset path.

The fixed instruction bound limits software loops even if the counter freezes.
It cannot make a trapped or non-returning system-register instruction complete,
nor prove that 21 virtual ticks correspond to physical time if firmware reports
an incorrect frequency or supplies a distorted counter. CI187 is a diagnostic
test of this counter path on this device, not a validated reset implementation.

## Screen interpretation

The entry marker is `CI187 P0`; all diagnostics and terminal panics use CI187.
The existing `S`/`R` transaction markers are retained. `SD0/SD1` and `RD0/RD1`
now bracket the first actual raw busy wait for subtype/reason polling; they are
absent if that transaction is already DONE on its first STATUS read.

After both READLs, **`CI187 QD0` / `CI187 QD1`** bracket an unconditional raw
counter delay tagged `Q`, even when neither SPMI poll required a delay. The final
`CI187 READONLY PASS` prints version, APID, subtype, old reason, counter and
next slot, then remains on the panic screen. The display's reused row tails must
still be distinguished from the newest markers.

No new chunk or metadata is needed for this build. Do not transfer repair slot
2 or run an ingest loop. The next eligible repair remains **slot 2**, requiring
a subsequent reviewed build that deliberately restores the normal ingest path.
