# CI184 guarded ingest

The original `chunk_000.bin` .. `chunk_139.bin` files are each 64 MiB.  The
manifest pins all 140 full SHA256 values.  Generating it also verifies that the
concatenated first 9,382,658,048 bytes match image SHA256
`fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`, with only
zero padding after that image boundary.

Use `ci184-prepare.py wrap --chunks-dir E:/d2xq-work/chunks --out-dir
E:/d2xq-work/ci184 --mode repair --slot 1` to make `repair_001.bin`; `--all`
prepares slots 0..6.  Append mode covers slots 7..139.  The tool copies the
source and adds a 4 KiB trailer; it never changes an original chunk.  `verify
--file <wrapped.bin>` checks the full payload and trailer independently and
prints the target LBA and expected counter.

The kernel reads the trailer at RECOVERY LBA `0x24f13 + 16384`.  Without the
trailer magic it leaves `CI184 READY` on screen and performs no writes.

## Metadata block

All integers are little endian.  The wrapped file is exactly 67,112,960 bytes.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 8 bytes | `D2XQIN2\0` |
| 8 | u32 | version 2 |
| 12 | u32 | header size 128 |
| 16 | u32 | target slot 0..139 |
| 20 | u32 | mode: 1 repair, 2 append |
| 24 | u64 | expected counter: 7 for repair, slot for append |
| 32 | u64 | payload bytes 67,108,864 |
| 40 | 32 bytes | raw payload SHA256, also checked against the kernel table |
| 72 | u64 | USERDATA first LBA `0x21f400` |
| 80 | u64 | USERDATA last LBA `0x3b90bfa` |
| 88 | u64 | RECOVERY first LBA `0x24f13` |
| 96 | 32 bytes | SHA256 of bytes 0..95 |
| 128 | 3968 bytes | all zero |

Repair mode requires counter index 7 and the exact next repair slot, starting
at zero.  Each verified repair persistently advances its repair progress while
keeping index 7.  A stale repair trailer is rejected before source hashing or
any write/reset, even when two slots contain identical all-zero data.

Append mode requires counter equal to slot.  A completed append retry with
counter=slot+1 and a trusted extension immediately leaves `CI184 ALREADY` on
screen, before any data readback, flush, write or reset.  The host must stop and
reconcile that result instead of interpreting another USB reset as completion.
Slot 7 accepts either the exact legacy index-seven state or completed repair
progress 7; intermediate repair progress 1..6 is rejected.  It fully verifies
windows 0..6 before writing or advancing the index.  Append slots 8 onward
require the trusted `D2SAFE84` counter extension.

## Trusted counter block

Verified repair and append operations write a complete zero-filled 4 KiB block
with these fields:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `D2XQ` |
| 4 | u32 | repair: next repair slot 1..7; append: zero |
| 8 | u64 | repair: index stays 7; append: next image slot 8..140 |
| 16 | 8 bytes | repair: `D2REPR84`; append: `D2SAFE84` |
| 24 | 32 bytes | raw pinned original image SHA256 |
| 56 | u64 | bitwise complement of index |
| 64 | 32 bytes | SHA256 of counter bytes 0..63 |
| 96 | 4000 bytes | all zero |

`D2REPR84` is accepted only with index 7 and repair progress 1..7.
`D2SAFE84` is accepted only with index 8..140 and a zero repair field.  Both
require the pinned image hash, index complement, checksum and zero reserved
bytes.  The only accepted legacy state is index 7, a zero repair field and
all-zero bytes 16..4095; it represents next repair slot zero.  Unknown nonzero
extensions are rejected.

The source is fully hashed before target writes.  The existing target is fully
hashed and matching data is reused.  Written targets are flushed with
SYNCHRONIZE CACHE(10) and fully read back.  Matching targets are flushed too,
covering data left in a device write cache by an interrupted attempt.  Only
then may repair commit its next repair slot, or append commit the next image
index.  That commit is flushed and all 4 KiB are compared on readback.  The
`CI184_SUCCESS_HOOK` is reached after all checks and that durable progress
commit have passed.  Repeated metadata therefore cannot reach this hook.

Progress markers: `Dmode.slot` accepts metadata; `Sslot.n` hashes source;
`Bwindow.n` verifies the baseline; `Tslot.n` checks target; `Wslot.n` writes;
`F0/F1` delimit target flush; `Vslot.n` reads back; `N0/N1` delimit counter
commit.  Any failure panics before the success hook.

The success hook reads MISC LBA `0x6513`, changes only the 32-byte boot command
to `bootloader`, flushes it, and compares the complete 4 KiB block on readback.
It then maps the SM8150 APSS watchdog device page at `0x17c10000`, disables it,
programs a five-second bark and one-second bite (32765 ticks/second), enables
it, and resets the watchdog counter. `OKslot:counter R` is printed just before
enabling the watchdog. A reset timeout panics with the watchdog registers.
The watchdog sequence and whether MISC selects Samsung Download still need
real-device verification before unattended batching.

## CI185 reset revision (same ingest protocol)

The 65-packet single-sequence Heimdall variant delivered the complete payload
and metadata. Real-device repair slot 0 advanced the persistent repair state
to 1 and rebooted back into XNU. Re-entry stopped at `repair sequence slot=0
next=1 counter=7`. Thus slot 0 is verified and must not be replayed; the MISC
`bootloader` request did not select Samsung Odin Download.

CI185 replaces only the success reset hook with the same-device Samsung PON
Download reason. It validates SPMI v5, EE0 ownership and Gen2 PON subtype,
updates PMIC SID0:0x088f to `(old & 1) | 0x2a`, reads it back exactly, and then
enables the already tested APSS watchdog. No MISC request is written by this
revision. Hardware evidence, diagnostic markers and failure behavior are in
`CI185_RESET.md`. All payload/counter formats and all ingest guards above stay
the same. Automatic arrival in Odin Download requires a real-device test of
repair slot 1 before batching.
