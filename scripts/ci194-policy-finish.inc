struct Ci194ReadCompletion {
  volatile uint32_t done;
  IOReturn status;
  UInt64 actual;
};

static void ci194ReadCompleted (void *target, void *, IOReturn status, UInt64 actual)
{
  Ci194ReadCompletion *state = static_cast<Ci194ReadCompletion *> (target);
  state->status = status; state->actual = actual;
  __atomic_store_n (&state->done, 1U, __ATOMIC_RELEASE);
}

bool NativeUfsBlock::ci194SelfTest ()
{
  if (!online || !verified.hashes || !bytes || (bytes & 511)) return false;
  IOBufferMemoryDescriptor *memory = IOBufferMemoryDescriptor::inTaskWithOptions (
      kernel_task, static_cast<IOOptionBits> (kIODirectionIn), 512, 512);
  if (!memory) return false;
  if (memory->prepare () != kIOReturnSuccess) { memory->release (); return false; }
  const uint64_t sectors[2] = { 0, bytes / 512 - 1 };
  bool ok = true;
  for (unsigned n = 0; n < 2 && ok; ++n) {
    Ci194ReadCompletion state = { 0, kIOReturnError, 0 };
    IOStorageCompletion completion = {};
    completion.target = &state; completion.action = ci194ReadCompleted; completion.parameter = nullptr;
    const IOReturn accepted = doAsyncReadWrite (memory, sectors[n], 1, nullptr, &completion);
    if (accepted != kIOReturnSuccess) { ok = false; break; }
    unsigned tick = 0;
    while (!__atomic_load_n (&state.done, __ATOMIC_ACQUIRE) && tick++ < 500) ci194Wait (10);
    // Do not release a descriptor that an outstanding completion might own.
    if (!__atomic_load_n (&state.done, __ATOMIC_ACQUIRE)) ci194Fail ("verified self-test completion timeout");
    ok = state.status == kIOReturnSuccess && state.actual == 512;
  }
  const IOReturn completed = memory->complete ();
  memory->release ();
  if (!ok || completed != kIOReturnSuccess) return false;
  // The same public method must reject an output direction before any media IO.
  IOBufferMemoryDescriptor *output = IOBufferMemoryDescriptor::inTaskWithOptions (
      kernel_task, static_cast<IOOptionBits> (kIODirectionOut), 512, 512);
  if (!output) return false;
  Ci194ReadCompletion refused = { 0, kIOReturnSuccess, 0 };
  IOStorageCompletion completion = {};
  completion.target = &refused; completion.action = ci194ReadCompleted; completion.parameter = nullptr;
  const IOReturn result = doAsyncReadWrite (output, 0, 1, nullptr, &completion);
  output->release ();
  // With the persistent Data store open the media is writable by design;
  // the read-only refusal only applies when the store is closed.
  if (native_ufs_store_mode () == 2) return true;
  return result == kIOReturnNotWritable && !refused.done;
}


// IOMedia inherits no UUID from the block device; root matching needs one.
static void ci194TagMedia (NativeUfsBlock *window, const char *uuid)
{
  OSString *value = OSString::withCString (uuid);
  if (!value) return;
  IORegistryIterator *media = IORegistryIterator::iterateOver (
      window, gIOServicePlane, kIORegistryIterateRecursively);
  if (media) {
    while (IOService *entry = OSDynamicCast (IOService, media->getNextObject ())) {
      OSString *klass = OSDynamicCast (OSString, entry->getProperty ("IOClass"));
      if (klass && !strcmp (klass->getCStringNoCopy (), "IOMedia"))
        entry->setProperty ("UUID", value);
    }
    media->release ();
  }
  value->release ();
}



static void ci194TagThread (void *, wait_result_t)
{
  // IOMedia appears asynchronously after registerService; keep tagging it so the
  // cache mount and root matching can find the signed UUID at any later point.
  for (unsigned round = 0; round < 600; ++round) {
    for (unsigned p = 0; p < 2; ++p) {
      NativeUfsBlock *window = gCi194Windows[p];
      char uuid[37];
      if (window && window->signedVolumeUuid (uuid)) ci194TagMedia (window, uuid);
    }
    ci194Wait (100);
  }
}

extern "C" void native_ufs_ci194_finish (void)
{
  if (!gCi194Entered || !gCi194LayoutStarted || gCi194FinishEntered) ci194Fail ("finish entry state");
  gCi194FinishEntered = true;
  ci194Print ("CI194 FIN ");
  ci194Transport ();
  // SourceStorage calls this only after the original bsd_autoconf has published
  // IOBSD, and before setconf/mountroot. This function never returns to them.
  ci194Print ("CI194 FIN-APFS ");
  const OSReturn loaded = OSKextLoadKextWithIdentifier ("com.apple.filesystems.apfs");
  if (loaded != kOSReturnSuccess) ci194Fail ("real APFS kext load");
  ci194Print ("CI194 FIN-APFSOK ");
  if (ci194DoneSem) {
    semaphore_wait (ci194DoneSem);
  } else {
    for (unsigned tick = 0; __atomic_load_n (&gCi194Publisher, __ATOMIC_ACQUIRE) != 2; ++tick) {
      if (tick == 400000) rootDiagnosticFailure ("CI194 publisher timeout");
      ci194Transport (); ci194Wait (100);
    }
  }
  if (gPending != 2 || native_ufs_blocks_published != 2 || !gCi194PolicyLoaded[0] || !gCi194PolicyLoaded[1] ||
      !gCi194Windows[0] || !gCi194Windows[1] || gCi194Windows[0] == gCi194Windows[1])
    ci194Fail ("two authenticated windows not published");
  dev_t devices[2] = { NODEV, NODEV };
  for (unsigned p = 0; p < 2; ++p) {
    char uuid[37], other[37], path[64] = {};
    if (!gCi194Windows[p]->signedVolumeUuid (uuid) || !gCi194Windows[1-p]->signedVolumeUuid (other))
      ci194Fail ("signed UUID unavailable");
    ci194TagMedia (gCi194Windows[p], uuid);
    (void) native_verified_block_volume_device (uuid, &devices[p], path, sizeof (path));
    ci194Print ("CI194 FIN-UUID ");
    if (native_ufs_block_authenticates (gCi194Windows[p], other, nullptr) != 0 ||
        native_ufs_block_authenticates (gCi194Windows[p], uuid, nullptr) != 1)
      ci194Fail ("cross-window authorization accepted");
    const uint64_t before = __atomic_load_n (&gCi194WindowVerified[p], __ATOMIC_RELAXED);
    if (!gCi194Windows[p]->ci194SelfTest () ||
        __atomic_load_n (&gCi194WindowVerified[p], __ATOMIC_RELAXED) < before + 2)
      ci194Fail ("real first/last sector verified reads");
  }
  if (native_ufs_integrity_failures || native_ufs_block_failures ||
      !native_ufs_block_reads || !native_ufs_verified_blocks ||
      !gCi194WindowVerified[0] || !gCi194WindowVerified[1]) ci194Fail ("media identity or integrity counters");
  RootLayout final = {}; const char *why = "";
  ci194Print ("CI194 FIN-SECT ");
  if (!findRootLayout (&final, &why, gHost.rootBlocks)) ci194Fail ("final root/GPT parse");
  ci194Snapshot (final, true);
  ci194Transport ();
  ci194Print ("CI194 AUTHENTICATED WINDOWS PASS ");
  {
    thread_t tagger = nullptr;
    if (kernel_thread_start (ci194TagThread, nullptr, &tagger) == KERN_SUCCESS) thread_deallocate (tagger);
  }
  ci194Print ("CI194 RETURNING ");
  __asm__ volatile ("" ::: "memory");
  return;
}
