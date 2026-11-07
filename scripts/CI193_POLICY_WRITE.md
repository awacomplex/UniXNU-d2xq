# CI193 external owner-policy staging

This terminal diagnostic copies the two exact, signed 21H461 owner-policy
envelopes from RECOVERY into the fixed userdata extent [0x44f800,0x453de6)
(4096-byte LBAs). The original 140 padded image chunks end at 0x44f400;
a 1024-block gap separates them from the policies. The completed counter at
0x3b90bfa and the image are never write destinations.

The compiled kernel binds the full padded bundle SHA256 and both unpadded
envelope SHA256 values. Host preparation independently checks the original
VerifiedRoot C implementation with actual RSA signatures and every source
block, checks the complete original-image digest, and tests invalid inputs.
Private signing material is kept outside the artifact and source directories.

Before any write the probe requires exclusive direct SCSI, exact userdata
geometry, D2SAFE84 counter 140 / repair 0 with complete checksum and reserved
checks, the pinned first root-manifest block, the fixed metadata protocol, and
a full source hash/padding pass. It checks the target before copying; an
already matching target skips the copy. All paths must flush and fully reread
the target, then confirm unchanged source metadata, root-manifest block and
all 4096 bytes of the counter. A second entry in the same boot is refused.

The only success terminal is `CI193 POLICY WRITE PASS`. No reset, image ingest,
counter update, writable log, store, APFS mount or root startup runs here.

Upload the 73,297,920-byte policy-transfer.bin to RECOVERY ID24 in one fresh
Download session. The reviewed custom Heimdall must report exactly one
sequence with 70 one-MiB packets (73,400,320 allocated bytes), no retry/error,
one successful upload and one reboot. The receipt alone is insufficient;
actual phone PASS is required before CI194 authenticated-view validation.
