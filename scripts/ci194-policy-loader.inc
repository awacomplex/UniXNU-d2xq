bool NativeUfsBlock::configureVerification ()
{
  gRootDiagnostic.stage = "CI194 external policy selection";
  gRootDiagnostic.policyCount = native_ufs_external_policy_count;
  gRootDiagnostic.verify = 0xffffffffU;
  ci194Transport ();
  if (!gCi194LayoutStarted || verifiedStorage || verified.hashes || online) return false;
  ci194Descriptors ();
  unsigned selected = 2;
  for (unsigned p = 0; p < 2; ++p) {
    const NativeExternalPolicyV1 &d = native_ufs_external_policies[p];
    if (!strcmp (label, d.label) && absolute == 0x21f400ULL * 4096 + d.windowOffset && bytes == d.windowBytes) {
      if (selected != 2 || gCi194PolicyLoaded[p]) return false;
      selected = p;
    }
  }
  if (selected == 2) return false;
  const NativeExternalPolicyV1 &d = native_ufs_external_policies[selected];
  const vm_size_t inputBytes = static_cast<vm_size_t> (d.manifestBytes);
  if (static_cast<uint64_t> (inputBytes) != d.manifestBytes || inputBytes > UINT64_MAX - 8191) return false;
  const vm_size_t tableBytes = round_page (inputBytes);
  if (tableBytes < inputBytes || tableBytes > UINT64_MAX - 4096) return false;
  verifiedStorageBytes = tableBytes + 4096;
  auto reject = [&] () -> bool {
    verified = {}; online = false;
    if (verifiedStorage) kmem_free (kernel_map, verifiedStorage, verifiedStorageBytes);
    verifiedStorage = 0; verifiedStorageBytes = 0;
    return false;
  };
  gRootDiagnostic.stage = "CI194 policy allocation";
  if (kmem_alloc (kernel_map, &verifiedStorage, verifiedStorageBytes,
      static_cast<kma_flags_t> (KMA_KOBJECT | KMA_DATA), VM_KERN_MEMORY_SECURITY) != KERN_SUCCESS)
    return reject ();
  bzero (reinterpret_cast<void *> (verifiedStorage), verifiedStorageBytes);
  SHA256_CTX context; SHA256_Init (&context);
  const uint64_t blocks = (d.manifestBytes + 4095) / 4096;
  for (uint64_t index = 0; index < blocks; ++index) {
    if ((index & 4095U) == 0) d2xq_console_text (selected ? "CI194 S ": "CI194 O ");
    gRootDiagnostic.stage = "CI194 fixed policy READ10";
    if (hostRead4k (0, d.firstLba + index, 1, bounce) != kIOReturnSuccess) return reject ();
    const uint64_t relative = index * 4096;
    const size_t length = static_cast<size_t> (d.manifestBytes - relative < 4096 ? d.manifestBytes - relative : 4096);
    for (size_t n = length; n < 4096; ++n) if (bounce[n]) {
      gRootDiagnostic.stage = "CI194 policy nonzero padding"; return reject ();
    }
    memcpy (reinterpret_cast<uint8_t *> (verifiedStorage) + relative, bounce, length);
    SHA256_Update (&context, bounce, length);
  }
  uint8_t digest[32]; SHA256_Final (digest, &context);
  if (memcmp (digest, d.manifestSha256, 32)) {
    gRootDiagnostic.stage = "CI194 policy envelope SHA"; return reject ();
  }
  uint8_t *key = reinterpret_cast<uint8_t *> (verifiedStorage + tableBytes);
  memcpy (key, verified_root_key, VR_KEY_BYTES);
  ci194Sha (key, VR_KEY_BYTES, digest);
  if (memcmp (digest, native_ufs_external_key_sha256, 32)) return reject ();
  gRootDiagnostic.stage = "CI194 policy read-only protection";
  if (vm_map_protect (kernel_map, verifiedStorage, verifiedStorage + verifiedStorageBytes,
      VM_PROT_READ, TRUE) != KERN_SUCCESS) return reject ();
  gRootDiagnostic.stage = "CI194 real owner RSA and table verification";
  const int status = vr_open (reinterpret_cast<const uint8_t *> (verifiedStorage), inputBytes,
      key, d.minimumGeneration, &vr_xnu_crypto, &verified);
  gRootDiagnostic.verify = static_cast<uint32_t> (status);
  if (status != VR_OK || verified.image_bytes != d.windowBytes || verified.generation != d.minimumGeneration ||
      verified.block_count != (d.windowBytes + 4095) / 4096 ||
      memcmp (verified.volume_uuid, d.volumeUuid, 16) || memcmp (verified.image_sha256, d.imageSha256, 32))
    return reject ();
  SHA384_CTX sha384; SHA384_Init (&sha384);
  SHA384_Update (&sha384, reinterpret_cast<const void *> (verifiedStorage), inputBytes);
  SHA384_Final (gCi194Envelope384[selected], &sha384);
  gCi194HashPointers[selected] = verified.hashes;
  gCi194Windows[selected] = this;
  gCi194PolicyLoaded[selected] = true;
  d2xq_console_text (selected ? "CI194 SYSTEMOS SIGNATURE OK ": "CI194 OS SIGNATURE OK ");
  return true;
}
