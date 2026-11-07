    {
      // CI184: the target comes from a checked trailer, never from a blind
      // counter increment.  Original chunk files remain 64 MiB; the host adds
      // one 4 KiB metadata block at RECOVERY + 16384.
      static const uint64_t chunkBlocks = 16384;
      static const uint64_t payloadBytes = chunkBlocks * 4096;
      static const uint64_t recoveryFirst = 0x24f13ULL;
      static const unsigned totalChunks = 140;
      static const uint8_t metadataMagic[8] = { 'D', '2', 'X', 'Q', 'I', 'N', '2', 0 };
      static const uint8_t safeCounterMagic[8] = { 'D', '2', 'S', 'A', 'F', 'E', '8', '4' };
      static const uint8_t repairCounterMagic[8] = { 'D', '2', 'R', 'E', 'P', 'R', '8', '4' };
      static const uint8_t imageSha[SHA256_DIGEST_LENGTH] = {
        0xfd, 0x43, 0xee, 0x70, 0x35, 0xce, 0x18, 0x5c, 0xe4, 0x21, 0x5a, 0xc9, 0x3f, 0xdc, 0x00, 0x6f,
        0xd7, 0x84, 0xa5, 0x4b, 0x3b, 0x74, 0x42, 0xe2, 0x14, 0x57, 0xc2, 0x26, 0xcb, 0xe0, 0x30, 0x93
      };
      /* CI184_CHUNK_HASH_TABLE */
      uint8_t *dma = (uint8_t *) gHost.virt + kDataOffset;
      if (layout->userdataFirst != 0x21f400ULL || layout->userdataLast != 0x3b90bfaULL)
        panic ("CI184 FAIL layout first=%llx last=%llx", layout->userdataFirst, layout->userdataLast);
      if (layout->userdataFirst + totalChunks * chunkBlocks > layout->userdataLast)
        panic ("CI184 FAIL image exceeds userdata");
      const uint64_t ctrLba = layout->userdataLast;
      auto load32le = [] (const uint8_t *p) -> uint32_t {
        uint32_t value = 0;
        for (unsigned b = 0; b < 4; b++) value |= (uint32_t) p[b] << (8 * b);
        return value;
      };
      auto load64le = [] (const uint8_t *p) -> uint64_t {
        uint64_t value = 0;
        for (unsigned b = 0; b < 8; b++) value |= (uint64_t) p[b] << (8 * b);
        return value;
      };
      auto digestPrefix = [] (const uint8_t *p) -> uint64_t {
        uint64_t value = 0;
        for (unsigned b = 0; b < 8; b++) value = (value << 8) | p[b];
        return value;
      };
      auto readBlock = [&] (uint64_t lba, const char *phase, uint64_t block) {
        if (lba > 0xffffffffULL) panic ("CI184 FAIL read LBA overflow");
        uint8_t cdb[10] = { 0x28, 0, (uint8_t) (lba >> 24), (uint8_t) (lba >> 16),
                            (uint8_t) (lba >> 8), (uint8_t) lba, 0, 0, 1, 0 };
        uint32_t status = 0;
        IOReturn result = hostScsiDir (0, cdb, 10, 2, 4096, 1900000, &status);
        if (result != kIOReturnSuccess || status != 0)
          panic ("CI184 FAIL %s read b=%llu rr=%x st=%04x", phase, block, result, status);
      };

      d2xq_console_text ("C0 ");
      readBlock (ctrLba, "counter", 0);
      if (memcmp (dma, "D2XQ", 4) != 0) panic ("CI184 FAIL counter magic");
      uint64_t counter = load64le (dma + 8);
      if (counter > totalChunks) panic ("CI184 FAIL counter=%llu", counter);
      unsigned nextRepairSlot = load32le (dma + 4);
      bool safeCounter = memcmp (dma + 16, safeCounterMagic, sizeof (safeCounterMagic)) == 0;
      bool repairCounter = memcmp (dma + 16, repairCounterMagic, sizeof (repairCounterMagic)) == 0;
      bool legacyCounter = false;
      if (safeCounter || repairCounter) {
        SHA256_CTX counterSha;
        uint8_t counterDigest[SHA256_DIGEST_LENGTH];
        SHA256_Init (&counterSha);
        SHA256_Update (&counterSha, dma, 64);
        SHA256_Final (counterDigest, &counterSha);
        if (memcmp (dma + 24, imageSha, SHA256_DIGEST_LENGTH) != 0 ||
            load64le (dma + 56) != ~counter || memcmp (counterDigest, dma + 64, SHA256_DIGEST_LENGTH) != 0)
          panic ("CI184 FAIL trusted counter checksum=%llu", counter);
        for (unsigned b = 96; b < 4096; b++)
          if (dma[b] != 0) panic ("CI184 FAIL trusted counter reserved b=%u", b);
        if (safeCounter && (nextRepairSlot != 0 || counter < 8))
          panic ("CI184 FAIL safe counter state=%llu repair=%u", counter, nextRepairSlot);
        if (repairCounter && (counter != 7 || nextRepairSlot < 1 || nextRepairSlot > 7))
          panic ("CI184 FAIL repair counter state=%llu repair=%u", counter, nextRepairSlot);
      } else {
        // Only the exact legacy index-seven block can begin repair slot zero.
        // Unknown nonzero extensions must not silently restart the sequence.
        if (counter != 7 || nextRepairSlot != 0)
          panic ("CI184 FAIL legacy counter=%llu repair=%u", counter, nextRepairSlot);
        for (unsigned b = 16; b < 4096; b++)
          if (dma[b] != 0) panic ("CI184 FAIL unknown counter extension b=%u", b);
        legacyCounter = true;
      }
      static uint8_t savedCounter[4096];
      memcpy (savedCounter, dma, sizeof (savedCounter));
      d2xq_console_text ("C1 ");
      /* CI189_RESET_PROBE */

      readBlock (recoveryFirst + chunkBlocks, "metadata", 0);
      if (memcmp (dma, metadataMagic, sizeof (metadataMagic)) != 0)
        panic ("CI184 READY counter=%llu repair=%u metadata missing", counter, nextRepairSlot);
      if (load32le (dma + 8) != 2 || load32le (dma + 12) != 128)
        panic ("CI184 FAIL metadata version/size");
      for (unsigned b = 128; b < 4096; b++)
        if (dma[b] != 0) panic ("CI184 FAIL metadata reserved b=%u", b);
      SHA256_CTX metadataSha;
      uint8_t metadataDigest[SHA256_DIGEST_LENGTH];
      SHA256_Init (&metadataSha);
      SHA256_Update (&metadataSha, dma, 96);
      SHA256_Final (metadataDigest, &metadataSha);
      if (memcmp (metadataDigest, dma + 96, SHA256_DIGEST_LENGTH) != 0)
        panic ("CI184 FAIL metadata checksum");
      unsigned slot = load32le (dma + 16);
      unsigned mode = load32le (dma + 20);
      uint64_t expectedCounter = load64le (dma + 24);
      if (slot >= totalChunks || (mode != 1 && mode != 2))
        panic ("CI184 FAIL metadata slot=%u mode=%u", slot, mode);
      if (load64le (dma + 32) != payloadBytes || load64le (dma + 72) != layout->userdataFirst ||
          load64le (dma + 80) != layout->userdataLast || load64le (dma + 88) != recoveryFirst)
        panic ("CI184 FAIL metadata geometry");
      if (memcmp (dma + 40, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0)
        panic ("CI184 FAIL metadata SHA slot=%u", slot);
      bool already = false;
      if (mode == 1) {
        if (slot >= 7 || expectedCounter != 7 || counter != 7 || (!legacyCounter && !repairCounter))
          panic ("CI184 FAIL repair slot=%u counter=%llu expected=%llu", slot, counter, expectedCounter);
        if (slot != nextRepairSlot)
          panic ("CI184 FAIL repair sequence slot=%u next=%u counter=%llu", slot, nextRepairSlot, counter);
      } else {
        if (slot < 7 || expectedCounter != slot || (counter != slot && counter != (uint64_t) slot + 1))
          panic ("CI184 FAIL append slot=%u counter=%llu expected=%llu", slot, counter, expectedCounter);
        already = counter == (uint64_t) slot + 1;
        if ((slot >= 8 || already) && !safeCounter)
          panic ("CI184 FAIL untrusted append counter=%llu slot=%u", counter, slot);
        // A stale append trailer may have the same payload as the next all-zero
        // chunk.  Completion must stop here, before any flush, write or reset.
        if (already)
          panic ("CI184 ALREADY slot=%u counter=%llu next=%llu", slot, counter, counter);
        if (slot == 7 && repairCounter && nextRepairSlot != 7)
          panic ("CI184 FAIL incomplete repair next=%u", nextRepairSlot);
      }
      const uint64_t dst = layout->userdataFirst + (uint64_t) slot * chunkBlocks;
      const uint64_t dstEnd = dst + chunkBlocks;
      if (dst < layout->userdataFirst || dstEnd > ctrLba || dstEnd <= dst)
        panic ("CI184 FAIL target bounds slot=%u", slot);
      // Full readback intentionally outlives the startup watchdog.  Every
      // individual command retains the transport's bounded poll count.
      d2xq_ufs_stage = -1;
      char metadataMarker[24];
      snprintf (metadataMarker, sizeof (metadataMarker), "D%u.%u ", mode, slot);
      d2xq_console_text (metadataMarker);

      auto hashWindow = [&] (uint64_t first, char phase, unsigned displaySlot, uint8_t *digest) {
        SHA256_CTX sha;
        SHA256_Init (&sha);
        for (uint64_t block = 0; block < chunkBlocks; block++) {
          if ((block & 1023U) == 0) {
            char marker[24];
            snprintf (marker, sizeof (marker), "%c%u.%llu ", phase, displaySlot, block / 1024);
            d2xq_console_text (marker);
          }
          readBlock (first + block, phase == 'S' ? "source" : (phase == 'B' ? "baseline" : "target"), block);
          SHA256_Update (&sha, dma, 4096);
        }
        SHA256_Final (digest, &sha);
      };
      uint8_t sourceDigest[SHA256_DIGEST_LENGTH];
      hashWindow (recoveryFirst, 'S', slot, sourceDigest);
      if (memcmp (sourceDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0)
        panic ("CI184 FAIL source slot=%u got=%llx expected=%llx", slot,
               digestPrefix (sourceDigest), digestPrefix (expectedSha[slot]));
      d2xq_console_text ("SOK ");

      // A legacy counter of seven did not prove that its earlier windows were
      // correct.  Establish the full trusted baseline before appending slot 7.
      if (mode == 2 && slot == 7 && !already) {
        for (unsigned baseline = 0; baseline < 7; baseline++) {
          uint8_t baselineDigest[SHA256_DIGEST_LENGTH];
          hashWindow (layout->userdataFirst + (uint64_t) baseline * chunkBlocks, 'B', baseline, baselineDigest);
          if (memcmp (baselineDigest, expectedSha[baseline], SHA256_DIGEST_LENGTH) != 0)
            panic ("CI184 FAIL baseline w=%u got=%llx expected=%llx", baseline,
                   digestPrefix (baselineDigest), digestPrefix (expectedSha[baseline]));
          char marker[16];
          snprintf (marker, sizeof (marker), "B%u ", baseline);
          d2xq_console_text (marker);
        }
        d2xq_console_text ("BOK ");
      }

      uint8_t targetDigest[SHA256_DIGEST_LENGTH];
      hashWindow (dst, 'T', slot, targetDigest);
      bool skipped = memcmp (targetDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) == 0;
      if (already && !skipped)
        panic ("CI184 FAIL advanced counter target slot=%u got=%llx", slot, digestPrefix (targetDigest));
      auto writeBlock = [&] (uint64_t lba, bool counterWrite, uint64_t block) {
        if (already || lba > 0xffffffffULL || (counterWrite ? (lba != ctrLba) : (lba < dst || lba >= dstEnd)))
          panic ("CI184 FAIL write bounds slot=%u lba=%llx", slot, lba);
        uint8_t cdb[10] = { 0x2a, 0, (uint8_t) (lba >> 24), (uint8_t) (lba >> 16),
                            (uint8_t) (lba >> 8), (uint8_t) lba, 0, 0, 1, 0 };
        uint32_t status = 0;
        IOReturn result = hostScsiDir (0, cdb, 10, 1, 4096, 1900000, &status);
        if (result != kIOReturnSuccess || status != 0)
          panic ("CI184 FAIL write slot=%u b=%llu rr=%x st=%04x", slot, block, result, status);
      };
      auto flushCache = [&] (const char *phase) {
        uint8_t cdb[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint32_t status = 0;
        IOReturn result = hostScsiDir (0, cdb, 10, 0, 0, 1900000, &status);
        if (result != kIOReturnSuccess || status != 0)
          panic ("CI184 FAIL %s flush slot=%u rr=%x st=%04x", phase, slot, result, status);
      };
      if (!skipped) {
        SHA256_CTX copiedSha;
        SHA256_Init (&copiedSha);
        for (uint64_t block = 0; block < chunkBlocks; block++) {
          if ((block & 1023U) == 0) {
            char marker[24];
            snprintf (marker, sizeof (marker), "W%u.%llu ", slot, block / 1024);
            d2xq_console_text (marker);
          }
          readBlock (recoveryFirst + block, "copy source", block);
          SHA256_Update (&copiedSha, dma, 4096);
          writeBlock (dst + block, false, block);
        }
        uint8_t copiedDigest[SHA256_DIGEST_LENGTH];
        SHA256_Final (copiedDigest, &copiedSha);
        if (memcmp (copiedDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0)
          panic ("CI184 FAIL source changed slot=%u", slot);
        d2xq_console_text ("F0 ");
        flushCache ("target");
        d2xq_console_text ("F1 ");
        hashWindow (dst, 'V', slot, targetDigest);
        if (memcmp (targetDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0)
          panic ("CI184 FAIL verify slot=%u got=%llx expected=%llx", slot,
                 digestPrefix (targetDigest), digestPrefix (expectedSha[slot]));
        d2xq_console_text ("VOK ");
      } else {
        // A matching read might observe data left in the device's write
        // cache by an interrupted attempt.  Persist it before any counter.
        flushCache ("matched target");
        d2xq_console_text ("MATCH ");
      }

      uint64_t finalCounter = counter;
      if (!already) {
        if (mode == 2) finalCounter = (uint64_t) slot + 1;
        memset (savedCounter, 0, sizeof (savedCounter));
        memcpy (savedCounter, "D2XQ", 4);
        if (mode == 1) {
          unsigned nextRepair = slot + 1;
          for (unsigned b = 0; b < 4; b++) savedCounter[4 + b] = (uint8_t) (nextRepair >> (8 * b));
        }
        for (unsigned b = 0; b < 8; b++) savedCounter[8 + b] = (uint8_t) (finalCounter >> (8 * b));
        memcpy (savedCounter + 16, mode == 1 ? repairCounterMagic : safeCounterMagic, sizeof (safeCounterMagic));
        memcpy (savedCounter + 24, imageSha, SHA256_DIGEST_LENGTH);
        uint64_t inverseCounter = ~finalCounter;
        for (unsigned b = 0; b < 8; b++) savedCounter[56 + b] = (uint8_t) (inverseCounter >> (8 * b));
        SHA256_CTX nextCounterSha;
        SHA256_Init (&nextCounterSha);
        SHA256_Update (&nextCounterSha, savedCounter, 64);
        SHA256_Final (savedCounter + 64, &nextCounterSha);
        memcpy (dma, savedCounter, sizeof (savedCounter));
        d2xq_console_text ("N0 ");
        writeBlock (ctrLba, true, 0);
        flushCache ("counter");
        readBlock (ctrLba, "counter verify", 0);
        if (memcmp (dma, savedCounter, sizeof (savedCounter)) != 0)
          panic ("CI184 FAIL counter verify slot=%u", slot);
        d2xq_console_text ("N1 ");
      }
      // Source and target were verified; the repair progress or append index
      // was flushed and read back exactly.  Reset is eligible only here.
      /* CI184_SUCCESS_HOOK */
      panic ("CI184 PASS mode=%u slot=%u counter=%llu skip=%u already=%u",
             mode, slot, finalCounter, (unsigned) skipped, (unsigned) already);
    }
