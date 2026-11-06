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

Repair mode requires counter 7 and slot below 7.  It preserves the counter.
Append mode requires counter equal to slot.  The only completed retry allowed
is counter=slot+1 with a trusted counter extension and a matching target hash;
that path never rewrites data.  Slot 7 fully verifies windows 0..6 before it
can write or advance the counter.  Append slots 8 onward require a trusted
counter extension, preventing a legacy unverified counter from skipping ahead.

## Trusted counter block

The first append writes a complete zero-filled 4 KiB block with these fields:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `D2XQ` |
| 8 | u64 | next slot index |
| 16 | 8 bytes | `D2SAFE84` |
| 24 | 32 bytes | raw pinned original image SHA256 |
| 56 | u64 | bitwise complement of index |
| 64 | 32 bytes | SHA256 of counter bytes 0..63 |
| 96 | 4000 bytes | all zero |

The source is fully hashed before target writes.  The existing target is fully
hashed and matching data is reused.  Written targets are flushed with
SYNCHRONIZE CACHE(10) and fully read back.  Matching targets are flushed too,
covering data left in a device write cache by an interrupted attempt.  Only
then may append commit the next counter; it flushes and compares all 4 KiB on
readback.  The `CI184_SUCCESS_HOOK` is reached after all checks have passed.

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
