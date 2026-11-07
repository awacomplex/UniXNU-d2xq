# CI192: complete, terminal read-only userdata verification

This independent build replaces the probe-10 panic with the final verifier in
`ci192-final-readonly.inc`. The builder expands only that template and the same
140 pinned chunk hashes. It does not expand CI191 ingest or any reset hook.
The original CI191 branch, payload files and host transfer scripts are untouched.

The verifier disables the startup watchdog, checks the exact userdata geometry,
and reads the complete 4096-byte progress record. It requires `D2XQ`, repair=0,
counter=140, `D2SAFE84`, the pinned original-image identity, inverse counter,
SHA256 of bytes 0..63, and zero reserved bytes. It saves the entire record.

It then reads all 140 x 16384 x 4096 bytes from userdata in order, once. Every
64 MiB digest must match the existing chunk table. In the same pass it hashes
only the original 9,382,658,048 image bytes, requiring SHA256
`fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`.
All 12,582,912 padding bytes after that original length must be zero. The target
range ends before the separate progress block at userdataLast.

After the image checks, it reads the progress block again and requires all 4096
bytes to match the saved record exactly. Only then does it stop with
`CI192 FINAL READONLY PASS chunks=140 image=9382658048 padding=12582912 counter=140`.
Markers `Vslot.0..3` show 16 MiB read progress and `Hslot` means that full padded
chunk matched. Any transport, checksum, padding or counter mismatch panics.

The verifier's only SCSI command is one-block READ(10), LUN0, direction=2. It
never reads a RECOVERY trailer, writes or flushes UFS, changes progress, resets
the device, returns to normal boot, publishes root media or executes launchd.
This validates stored image bytes; it does not establish that iOS can boot.

Do not use the old handoff's proposed OS-window `NXSB` shortcut as an acceptance
test. The pinned OS window at 0x100000 begins with a disk GPT (`EFI PART` at
window+512); SystemOS at 0x16a600000 is raw APFS (`NXSB` at window+32). Complete
image and chunk hashes cover both layouts without that assumption.

Host validation of the actual builder expansion passed ARM64 compilation,
assembly and printf checks at default optimization and O2. An independent
READ(10)-only harness passed 53 state, transport and coverage scenarios; all
12 destructive negative controls were rejected. The fault scenarios use a
controlled payload-hash provider, with real SHA256 for progress records. A
separate run of the same actual verifier read the complete pinned host image
using real streaming BCrypt SHA256 for all 140 chunk hashes and the original
image hash, and synthesized and checked the 12 MiB zero padding. It issued
2,293,762 simulated READ(10) commands, including both progress reads, without
write, flush or reset. These host checks do not replace phone readback.
