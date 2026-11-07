extern "C" void d2xq_console_text (const char *);
// Included in the NativeUfs translation unit, after store/log declarations and
// before NativeUfsBlock. All media reads use the already reviewed locked path.
static bool gCi194Entered = false, gCi194LayoutStarted = false, gCi194FinishEntered = false;
static volatile uint32_t gCi194Publisher = 0;
static bool gCi194PolicyLoaded[2] = {};
static const uint8_t *gCi194HashPointers[2] = {};
static uint8_t gCi194Envelope384[2][48] = {};
static uint64_t gCi194WindowVerified[2] = {};
static uint8_t gCi194Counter[4096], gCi194Root[4096], gCi194GptHeader[4096], gCi194GptEntries[8192];
static RootLayout gCi194Layout = {};
static uint64_t gCi194Capacity = 0;
class NativeUfsBlock;
static NativeUfsBlock *gCi194Windows[2] = {};

[[noreturn]] static void ci194Fail (const char *reason)
{
  panic ("CI194 AUTHENTICATED WINDOWS FAIL %s", reason);
}

/* Two threads report progress; serialize the framebuffer console. */
static volatile uint32_t ci194PrintLock;
static void ci194Print (const char *text)
{
  while (__atomic_exchange_n (&ci194PrintLock, 1U, __ATOMIC_ACQUIRE)) { }
  d2xq_console_text (text);
  __atomic_store_n (&ci194PrintLock, 0U, __ATOMIC_RELEASE);
}

/* Timers can be unavailable this early; the CI194 waits busy-poll instead. */
static void ci194Wait (unsigned ms) { IODelay (ms * 1000U); }

static void ci194Sha (const void *bytes, size_t length, uint8_t out[32])
{
  SHA256_CTX context; SHA256_Init (&context); SHA256_Update (&context, bytes, length); SHA256_Final (out, &context);
}

static void ci194Transport (void)
{
  if (!gHost.ready || gHost.poisoned || gQueue.installed || gStoreOpen || gLogOpen ||
      gStoreRegionBlocks || gLogRegionBlocks || storeMode () || logMode ())
    ci194Fail ("nonexclusive or writable transport");
}

static void ci194FixedRead (uint64_t lba, void *out)
{
  ci194Transport ();
  if (!out || !(lba == 1 || lba == 2 || lba == 3 || lba == 0x21f400ULL ||
                lba == 0x3b90bfaULL || (lba >= 0x44f800ULL && lba < 0x453de6ULL)))
    ci194Fail ("fixed read bounds");
  if (hostRead4k (0, lba, 1, out) != kIOReturnSuccess) ci194Fail ("fixed READ10 transport");
}

static void ci194Descriptors (void)
{
  if (native_ufs_external_policy_version != 1 || native_ufs_external_policy_count != 2 || PAGE_SIZE != 4096)
    ci194Fail ("descriptor ABI or page size");
  const uint64_t first[2] = { 0x44f800ULL, 0x452549ULL };
  const uint64_t length[2] = { 47481664ULL, 25805344ULL };
  const uint64_t offsets[2] = { 1048576ULL, 6079643648ULL };
  const uint64_t sizes[2] = { 6077580800ULL, 3303014400ULL };
  const char *labels[2] = { "OS", "SystemOS" };
  const uint8_t uuids[2][16] = {
    { 0x04,0x76,0x9d,0x26,0x2c,0xa0,0x48,0x25,0xb4,0x43,0xe2,0xe5,0xf8,0x83,0x00,0xf2 },
    { 0x88,0x46,0xfe,0x78,0x29,0x10,0x42,0x97,0xaf,0x97,0xd5,0x9b,0xb6,0xf8,0xfc,0x16 }
  };
  uint64_t end = first[0];
  for (unsigned p = 0; p < 2; ++p) {
    const NativeExternalPolicyV1 &d = native_ufs_external_policies[p];
    const size_t labelBytes = strlen (labels[p]);
    if (d.firstLba != first[p] || d.firstLba != end || d.manifestBytes != length[p] ||
        d.windowOffset != offsets[p] || d.windowBytes != sizes[p] || d.minimumGeneration != 1 ||
        memcmp (d.volumeUuid, uuids[p], 16) || memcmp (d.label, labels[p], labelBytes + 1))
      ci194Fail ("descriptor identity");
    for (size_t n = labelBytes + 1; n < sizeof (d.label); ++n)
      if (d.label[n]) ci194Fail ("descriptor label reserved");
    // Fixed non-overlapping lengths ensure no metadata can supply an extent.
    const uint64_t blocks = (d.manifestBytes + 4095) / 4096;
    if (d.firstLba > UINT32_MAX || blocks > UINT32_MAX - d.firstLba ||
        d.windowOffset > 9382658048ULL || d.windowBytes > 9382658048ULL - d.windowOffset)
      ci194Fail ("descriptor bounds");
    end = d.firstLba + blocks;
  }
  if (end != 0x453de6ULL || end >= 0x3b90bfaULL || memcmp (uuids[0], uuids[1], 16) == 0)
    ci194Fail ("descriptor reservation");
  uint8_t digest[32]; ci194Sha (verified_root_key, 395, digest);
  if (memcmp (digest, native_ufs_external_key_sha256, 32)) ci194Fail ("public anchor SHA");
}

static void ci194CheckCounter (const uint8_t *block)
{
  if (memcmp (block, "D2XQ", 4) || le32 (block + 4) || le64 (block + 8) != 140 ||
      memcmp (block + 16, "D2SAFE84", 8) ||
      memcmp (block + 24, native_ufs_external_image_sha256, 32) || le64 (block + 56) != ~140ULL)
    ci194Fail ("counter140 state");
  uint8_t digest[32]; ci194Sha (block, 64, digest);
  if (memcmp (digest, block + 64, 32)) ci194Fail ("counter checksum");
  for (unsigned n = 96; n < 4096; ++n) if (block[n]) ci194Fail ("counter reserved");
}

static void ci194Snapshot (const RootLayout &layout, bool final)
{
  ci194Descriptors ();
  if (gHost.rootBlocks < 0x3b90c00ULL || gHost.rootBlocks > 0x100000000ULL || layout.userdataFirst != 0x21f400ULL ||
      layout.userdataLast != 0x3b90bfaULL || layout.imageBytes != 9382658048ULL ||
      layout.manifestStatus || layout.windows != 2 || layout.gptEntriesLba != 2 ||
      layout.gptEntryCount != 40 || layout.gptHeaderCrc != 0x666e2c31U || layout.gptEntriesCrc != 0xdc72edbdU)
    ci194Fail ("root/GPT geometry");
  for (unsigned p = 0; p < 2; ++p) {
    const RootWindow &w = layout.window[p]; const NativeExternalPolicyV1 &d = native_ufs_external_policies[p];
    if (strcmp (w.name, d.label) || w.offset != d.windowOffset || w.size != d.windowBytes ||
        memcmp (w.sha256, d.imageSha256, 32)) ci194Fail ("SHNGROOT window identity");
  }
  static uint8_t block[4096];
  if (!final) {
    ci194FixedRead (layout.userdataLast, block); ci194CheckCounter (block);
    memcpy (gCi194Counter, block, 4096);
  }
  ci194FixedRead (layout.userdataFirst, block);
  uint8_t digest[32]; ci194Sha (block, 4096, digest);
  if (memcmp (digest, native_ufs_external_root_manifest_sha256, 32)) ci194Fail ("SHNGROOT exact SHA");
  if (final) { if (memcmp (block, gCi194Root, 4096)) ci194Fail ("SHNGROOT changed"); }
  else memcpy (gCi194Root, block, 4096);
  ci194FixedRead (1, block);
  static const uint8_t expectedHeader[32] = {
    0x1e,0xb3,0xc5,0x70,0x33,0x57,0x74,0x4c,0xdb,0x3e,0x44,0xf6,0x5e,0x3b,0x59,0x6e,
    0x92,0x42,0x6a,0x69,0x86,0xe4,0x97,0x6e,0xf0,0x38,0x06,0x56,0x5a,0x90,0x60,0x45
  };
  ci194Sha (block, 4096, digest);
  if (memcmp (digest, expectedHeader, 32)) ci194Fail ("GPT exact header SHA");
  if (final) { if (memcmp (block, gCi194GptHeader, 4096)) ci194Fail ("GPT header changed"); }
  else memcpy (gCi194GptHeader, block, 4096);
  for (unsigned p = 0; p < 2; ++p) {
    ci194FixedRead (2 + p, block);
    if (final) { if (memcmp (block, gCi194GptEntries + p * 4096, 4096)) ci194Fail ("GPT array changed"); }
    else memcpy (gCi194GptEntries + p * 4096, block, 4096);
  }
  static const uint8_t expectedEntries[32] = {
    0xc4,0x08,0xcd,0xa9,0x19,0x4a,0xb5,0x42,0xce,0x13,0xd6,0xdf,0x88,0x4e,0xa7,0x1d,
    0xf6,0x63,0xa8,0x08,0x45,0xe2,0xf4,0xf0,0x24,0x6e,0x7c,0xd1,0x2d,0xa3,0x8f,0x39
  };
  ci194Sha (gCi194GptEntries, sizeof (gCi194GptEntries), digest);
  if (memcmp (digest, expectedEntries, 32)) ci194Fail ("GPT exact entries SHA");
  if (final) {
    if (gHost.rootBlocks != gCi194Capacity || memcmp (&layout, &gCi194Layout, sizeof (layout)))
      ci194Fail ("root layout/capacity changed");
    // Counter is the final storage operation before terminal PASS.
    ci194FixedRead (layout.userdataLast, block); ci194CheckCounter (block);
    if (memcmp (block, gCi194Counter, 4096)) ci194Fail ("counter changed");
  }
  else { gCi194Layout = layout; gCi194Capacity = gHost.rootBlocks; }
}

static void ci194Begin (const RootLayout &layout)
{
  if (!gCi194Entered || gCi194LayoutStarted) ci194Fail ("repeated layout entry");
  gCi194LayoutStarted = true;
  ci194Snapshot (layout, false);
  d2xq_console_text ("CI194 COUNTER140 ROOT GPT OK ");
}

static void ci194CountVerified (const vr_view *view)
{
  for (unsigned p = 0; p < 2; ++p) {
    if (gCi194PolicyLoaded[p] && view->hashes == gCi194HashPointers[p]) {
      __atomic_fetch_add (&gCi194WindowVerified[p], 1, __ATOMIC_RELAXED);
      return;
    }
  }
  ci194Fail ("unbound verified read");
}
