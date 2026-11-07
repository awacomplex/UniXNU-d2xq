# CI188: standalone Samsung Download reset test

The user confirmed a real-device `CI187 READONLY PASS`. That established both
observer READLs and the raw architectural-counter delay on this device. CI188
tests the reviewed Samsung PON Download reason followed by APSS watchdog reset,
without consuming repair slot 2 or changing any disk data/progress.

This build intentionally writes the PMIC restart-reason byte and the APSS WDT
registers. It is a reset-only test, not a read-only diagnostic or a chunk copy.
There is no assertion that Download was reached until the host observes it.

## Entry and isolation

The builder requires exactly one `CI188_RESET_PROBE` marker and expands
`ci188-reset-probe.inc` immediately after `C1`, following the existing complete
layout and trusted-counter validation. All image SHA, magic, checksum, inverse,
reserved-byte and geometry checks remain intact.

The standalone probe then requires
`repairCounter && counter == 7 && nextRepairSlot == 2`. Other states stop with
`CI188 RESET REFUSE` before reading architectural counters, mapping MMIO or
changing `d2xq_ufs_stage`. A valid state sets only that RAM diagnostic flag to -1.

Every completed path that has not reset the phone ends in a terminal
`CI188 RESET REFUSE` or `CI188 RESET FAIL`.
Nothing returns to the following metadata read, ingest, data write, counter
update or original CI185 success hook. No metadata is read by this test, no
progress is committed, and no `slot`/`finalCounter` value is fabricated. The
CI185 success hook and earlier diagnostic sources are preserved unchanged.

## Counter delay and mandatory pre-write test

The raw `CNTFRQ_EL0` / `ISB; MRS CNTVCT_EL0` / `NOP` helper is copied from the
real-device-tested CI187 implementation. Only 19,200,000 Hz is accepted. Each
microdelay requires `ceil(freq/1,000,000)+1 = 21` ticks and at most 4096 rounds.
Counter reversal or wrap panics immediately; a frozen counter or insufficient
progress at the bound panics as stalled/exhausted. No IODelay, XNU time framework,
scheduler, WFE or yield instruction is used.

`CI188 Q0` / `CI188 Q1` bracket an unconditional helper self-test **before any
PON or WDT write**. A failed self-test cannot proceed to hardware modification.
SPMI transactions retain their 1000-poll limit, using this helper only when
STATUS is not yet DONE. Read tags are S (subtype), R (old reason), V (verify);
write polling uses W, and the final reset wait uses T.

## Reviewed hardware sequence

Addresses, command encoding and register semantics remain those independently
checked against the same-device kernel sources recorded in
[CI185_RESET.md](CI185_RESET.md). CI188 changes the delay and test entry, not
the hardware values.

1. Retain mappings for core `0c440000/1100`, configuration `0c40a000/1000`, WDT
   `17c10000/1000`, and the selected observer/RW pages. Require SPMI version in
   `[50000000,60000000)`. Scan all 512 entries at core `900 + 4*i`; PPID must be
   008 and ownership at configuration `700 + 4*i`, low three bits, must be EE 0.
   Select the last matching owner-EE0 APID; APID 0 and legal duplicates remain
   supported. Observer is `0e600000 + 10000*EE + 80*APID`; RW is
   `0c600000 + 10000*APID`. Map the observer page aligned and add its offset.
2. Read SID 0 PON subtype 0805 with observer CMD `08000050`; accept only Gen2
   subtype 04/05. Read SOFT_RB_SPARE 088f with CMD `080008f0`. These checks occur
   before changing the watchdog to minimize failure side effects. If
   `(old & fe) == 2a` (old 2a or 2b), stop with
   `CI188 RESET REFUSE already Download` before any WDT/PON write and change no
   additional state.
3. `CI188 W0` precedes WDT writes: EN +08 = 0, BARK +10 = `5*32765`, BITE +14 =
   `32765` (decimal tick counts). Barriers and exact configuration readback match
   CI185. Stop on mismatch. `CI188 W1` means configuration readback passed.
4. Re-read and require the same complete selected APID entry, masked owner EE 0
   and hardware version before the PON write. `CI188 P1` prints version, APID,
   subtype, old/new reason bytes and match counts. Compute
   `new = (old & 01) | 2a`, preserving bit zero.
5. Write new to RW WDATA0 +10, barrier, then CMD +00 = `000008f0` for one-byte
   EXT_WRITEL. Poll STATUS +08, requiring DONE 01 and no error bits 0e. Observer
   EXT_READL of 088f must then return exactly the entire expected byte.
   `CI188 P2` confirms old/new/readback. No alternate reason, STATUS clear,
   CONFIG write, SEC_ACCESS unlock or SCM call is introduced.
6. `CI188 R0` precedes WDT EN +08 = 1 and RST +04 = 1, followed by the existing
   barrier. `CI188 R1` means those writes/barrier returned. The proven CI184/185
   WDT setting requests a bite at approximately one second.

## Bounded final reset wait

After enable/kick, sample a fresh raw counter start/last. The timeout is the
actual raw difference `freq * 10 = 192,000,000` ticks, representing ten seconds
at the accepted frequency. This is not a single 4096-round microdelay.

At most 10,000,000 iterations each run a bounded 21-tick helper and then sample
the raw counter again. Every outer sample must be at least the preceding one,
so a backwards step across helper invocations is also rejected. A frozen helper
is rejected within its 4096-round bound. If the raw ten-second threshold is
reached without reset, panic `CI188 RESET FAIL WDT no reset after 10s` with WDT
status/configuration. Exhausting the outer bound without reaching that threshold
instead panics `CI188 RESET FAIL wait exhausted`; it does not claim ten seconds
elapsed. Both paths are terminal and cannot continue ingest.

As in CI187, instruction bounds cannot make a trapped system-register/MMIO read
return, or prove physical time if firmware distorts the counter. A failure after
PON or WDT writes may leave the requested reason/configuration in effect; there
is no automatic rollback or retry within this function. Across boots, a retained
Download reason (2a/2b) is rejected before any WDT/PON write. If the bootloader
clears that reason, this test cannot guarantee strict one-shot behavior across
boots. Screen tails reuse the existing two-row ring,
so identify the newest CI188 marker rather than an old residual suffix.

## Device progress and test outcome

Use the existing recovery payload; no new chunk or metadata is required. Do not
run an ingest loop or transfer repair slot 2 during this reset-only build.
Counter 7 and `nextRepairSlot = 2` remain unchanged regardless of reset landing.
The next eligible data repair is still **slot 2** under a later reviewed ingest
build that explicitly restores its normal entry path.

The intended observation is a disconnect/reset followed by Samsung Download
enumeration. `P2`/`R1` alone establish the software reached those stages, not the
firmware's destination. Record the actual USB/screen result before choosing the
next build. This source has not itself established a new successful reset path.
