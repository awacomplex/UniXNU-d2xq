# CI191: guarded ingest with the tested Samsung Download reset

CI190's one-use reset test returned to Samsung Download without phone buttons.
The host recorded its disconnect and re-enumeration and then detected Download
again.  No intermediate phone markers were captured, so that test does not
identify whether the SCM PS_HOLD service or the direct fallback triggered the
reset.  It did not transfer a data chunk or advance image progress.

CI191 restores the normal CI184 data path.  The CI190 reset-only interception
before metadata is removed.  The payload and metadata formats, 140 source
hashes, fixed geometry, repair ordering, append baseline checks, target bounds,
source/target SHA256 checks and durable progress format remain unchanged.

The only normal-ingest change before its success hook is a local
`ci191IngestCommitted` latch.  It starts false and becomes true only after the
progress block was written, flushed and compared with a complete 4096-byte
readback.  The success wrapper additionally checks the actual source/target
digests and committed progress block, and validates the repair or append state
from the current transaction.  It authorizes reset using this fresh commit;
there is no ARM record upload, consume, or synthetic fixed counter/repair state.

The original CI190 hardware file remains byte-for-byte frozen at SHA256
`ca9607a237c9887f0dad4580462ef6e40c8cbbd9e11e11eb7fc7577adab8d120`.
The builder checks that digest, replaces its unique reset-only authorization
condition with the committed-ingest latch, and changes only the `CI190`/`ci190`
diagnostic and identifier prefixes to `CI191`/`ci191`.  The MMIO/SCM/PMIC,
counter/delay checks, error handling and reset ordering are unchanged.  The
diagnostic guard for a consumed ARM at counter 7/repair 2 is not applicable to
normal ingest and is not represented as a true ARM consume in CI191.

Invalid metadata, bad source/target content, SCSI failures, target/counter
flush failures and a counter readback mismatch cannot authorize hardware reset.
Repair re-entry with an old slot is rejected by the unchanged exact-next guard.
Completed append re-entry leaves `CI184 ALREADY` before any data flush, write,
or reset.  These persistent progress guards prevent a completed transaction
from repeatedly resetting if firmware returns to XNU.

Current real-device progress remains index 7, next repair slot 2.  Slots 0 and 1
are already verified; do not replay them.  The next data test is one
`repair_002.bin` transfer.  Its expected commit is index 7/next repair 3, followed
by automatic Download.  Validate that single transfer before continuing repair
3..6 and append 7..139.  Append 7 still fully verifies all seven baseline windows.

The normal progress messages remain CI184.  After `N1`, the new wrapper prints
`CI191 COMMITTED mode=... slot=... counter=... repair=...`, followed by CI191
hardware phase markers.  A commit marker means durable progress was verified;
automatic arrival in Download still requires observation.  CI191 itself has
not yet been built, packaged, flashed, or tested on the phone at document creation.

Host validation before publication passed: the actual builder expansion compiled
as ARM64 with assembly and format checks at default optimization and O2; 402
actual-ingest control-flow cases include 147 persistent re-entry checks, and 14
destructive negative controls were rejected.  Counter, metadata and progress
digests use real SHA256 in the model.  Payload hashing uses a controlled provider
that requires all 16384 blocks in order and the actual manifest digest; it is not
a cryptographic rehash of the real payload image.  Package verification and the
single repair-2 device test remain separate required steps.
