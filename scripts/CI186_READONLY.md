# CI186: read-only SPMI access localization

CI185 repair slot 1 reached `VOK N0 N1 CI185 P0` on the device and stopped before
`P1`. Target data and progress were already flushed and verified. The latest
durable state is counter 7, trusted `D2REPR84`, `nextRepairSlot = 2`.

CI186 is a standalone diagnostic build. It does not ingest repair slot 2, change
the restart reason, write/configure/enable the watchdog, or automatically reset.
It stops on screen. Leave the existing recovery payload in place; no new chunk
or metadata is required for the diagnostic.

## Entry and terminal behavior

`ci184-guarded-ingest.inc` contains one `CI186_READONLY_PROBE` marker immediately
after `C1`, following all existing layout/counter magic, checksum, image SHA,
inverse-counter and reserved-byte checks. The builder requires exactly one
marker and expands `ci186-readonly-probe.inc` there. No optional fallback disables
this diagnostic in the CI186 branch.

The probe accepts only `repairCounter && counter == 7 && nextRepairSlot == 2`.
Any other state stops before MMIO mapping or access. After accepting that state,
it sets `d2xq_ufs_stage = -1`, the same RAM diagnostic state used by the guarded
ingest before lengthy reads. This avoids the startup watchdog obscuring the
instrumented operation. It does not touch a hardware watchdog register.

All completed paths end in a panic. Therefore the existing later metadata
read/validation, data copy, counter update and CI185 reset hook are unreachable
in this build. The original guards, their stale metadata rejection, and the
CI185 reset include remain intact. The only guard edit is the unique marker.

## Exact hardware scope

The addresses, map lengths, version limits, EE 0, 512 APID scan, duplicate PPID
selection, commands and `IODelay(1)` match the reviewed CI185 implementation;
see [CI185_RESET.md](CI185_RESET.md) for pinned same-device sources and blobs.

- Map core `0c440000/1100`, configuration `0c40a000/1000`, WDT `17c10000/1000`,
  selected observer page and selected RW page; retain every mapping.
- Read core version, requiring `[50000000,60000000)`. Read all 512 APID entries
  at core `900 + 4*i`. For PPID 008 matches, read owner at configuration
  `700 + 4*i`, low three bits. Select the last EE 0 mapping, allowing APID 0.
- Observer is `0e600000 + 10000*EE + 80*APID`, page-aligned before mapping.
  RW is `0c600000 + 10000*APID`; it is mapped only and never dereferenced.
- Read WDT EN +08, BARK +10 and BITE +14 once each. No WDT write occurs.
- Issue two observer one-byte EXT_READL commands only: subtype 0805 uses
  `08000050`; SOFT_RB_SPARE 088f uses `080008f0`. Accept subtype 04/05. Read
  STATUS +08, requiring DONE 01 and no error bits 0e, with at most 1000 polls
  and `IODelay(1)` between polls. Read RDATA0 +18 only after success.

Hardware control writes are restricted to the observer CMD +00 for those reads.
There is no RW CMD/WDATA, PON write, STATUS clear, CONFIG write, MISC write,
USERDATA write, watchdog configuration or reset. This diagnostic retains both
`dsb sy` barriers and the first actual delay so it can locate a stalled barrier
or `IODelay`, rather than changing those assumptions while diagnosing them.

All addresses, command values and register offsets above are hexadecimal;
APID count, poll limit and delay microseconds are decimal.

## Screen markers

Before-operation markers are printed before the call/access. Results, addresses
and register values are printed only after the relevant operation has returned.

| Before | After | Operation |
| --- | --- | --- |
| `CI186 P0` | next marker | Probe entry and exact durable-state gate |
| `MC` / `MN` / `MW` | `mC=...` / `mN=...` / `mW=...` | Core / configuration / WDT map |
| `V` | `v=...` | Core version read |
| `E<index>` | `e` | One APID core-table read, every index 0–511 |
| `O<index>` | `o<owner>` | Matched PPID's configuration owner read |
| scan complete | `APID=... entry=... matches=... owned=...` | Last EE 0 selection |
| `MO` / `MR` | `mO=...` / `mR=...` | Selected observer / RW mapping |
| `WE` / `WA` / `WI` | `we=...` / `wa=...` / `wi=...` | WDT EN / BARK / BITE reads |
| `<tag>C0` | `<tag>C1` | Observer READL command write |
| `<tag>B0` | `<tag>B1` | Barrier immediately after CMD |
| `<tag>T0` | `<tag>T1=<status>` | First STATUS read |
| `<tag>D0` | `<tag>D1` | First actual `IODelay(1)`, if STATUS is not yet DONE |
| `<tag>B2` | `<tag>B3` | Completion barrier |
| `<tag>A0` | `<tag>A1=<data>` | RDATA0 read |
| `QD0` | `QD1` | Unconditional final `IODelay(1)` check |

`<tag>` is `S` for subtype and `R` for old restart reason. For example, `SD0`
without `SD1` localizes a stall inside the first subtype polling delay. A command
that is already DONE requires no delay, so missing D0/D1 then is expected.
After both reads, `QD0`/`QD1` explicitly exercise one delay even if screen output
allowed both transactions to finish before their first status poll. This keeps
the fast transaction path from accidentally skipping the delay diagnostic.

The final `CI186 READONLY PASS` includes version, APID, subtype, old reason,
counter and next slot. Failure panics identify the rejected condition. A stalled
access leaves the last before-operation marker visible. This does not promise
that an inaccessible MMIO read will return or that hardware access is safe merely
because the software model passed; the purpose is to identify the real boundary.

After this diagnostic, the next eligible repair remains **slot 2**. Do not run an
ingest loop or transfer slot 2 under this build. A subsequent reviewed ingest
build must explicitly restore the normal entry path before continuing repairs.
