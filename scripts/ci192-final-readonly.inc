    {
      // CI192 reads the final target independently of RECOVERY metadata.  Its
      // only SCSI operation is one-block READ(10); every outcome is terminal.
      static const uint64_t chunkBlocks = 16384;
      static const unsigned totalChunks = 140;
      static const uint64_t imageBytes = 9382658048ULL;
      static const uint64_t paddedBytes = (uint64_t) totalChunks * chunkBlocks * 4096;
      static const uint8_t safeCounterMagic[8] = { 'D', '2', 'S', 'A', 'F', 'E', '8', '4' };
      static const uint8_t imageSha[SHA256_DIGEST_LENGTH] = {
        0xfd, 0x43, 0xee, 0x70, 0x35, 0xce, 0x18, 0x5c, 0xe4, 0x21, 0x5a, 0xc9, 0x3f, 0xdc, 0x00, 0x6f,
        0xd7, 0x84, 0xa5, 0x4b, 0x3b, 0x74, 0x42, 0xe2, 0x14, 0x57, 0xc2, 0x26, 0xcb, 0xe0, 0x30, 0x93
      };
      /* CI184_CHUNK_HASH_TABLE */
      // The complete readback intentionally exceeds the startup watchdog.
      // Individual transport commands retain their bounded poll count.
      d2xq_ufs_stage = -1;
      d2xq_console_text ("CI192 FINAL READONLY BEGIN ");
      if (layout->userdataFirst != 0x21f400ULL || layout->userdataLast != 0x3b90bfaULL)
        panic ("CI192 FINAL READONLY FAIL layout first=%llx last=%llx", layout->userdataFirst, layout->userdataLast);
      const uint64_t ctrLba = layout->userdataLast;
      const uint64_t targetEnd = layout->userdataFirst + (uint64_t) totalChunks * chunkBlocks;
      if (targetEnd <= layout->userdataFirst || targetEnd > ctrLba ||
          imageBytes > paddedBytes || imageBytes % 4096 != 0 || paddedBytes - imageBytes != 12582912ULL)
        panic ("CI192 FINAL READONLY FAIL image geometry");
      uint8_t *dma = (uint8_t *) gHost.virt + kDataOffset;
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
      auto readBlock = [&] (uint64_t lba, const char *phase, uint64_t block) {
        if (lba > 0xffffffffULL ||
            (lba != ctrLba && (lba < layout->userdataFirst || lba >= targetEnd)))
          panic ("CI192 FINAL READONLY FAIL read bounds lba=%llx", lba);
        uint8_t cdb[10] = { 0x28, 0, (uint8_t) (lba >> 24), (uint8_t) (lba >> 16),
                            (uint8_t) (lba >> 8), (uint8_t) lba, 0, 0, 1, 0 };
        uint32_t status = 0;
        IOReturn result = hostScsiDir (0, cdb, 10, 2, 4096, 1900000, &status);
        if (result != kIOReturnSuccess || status != 0)
          panic ("CI192 FINAL READONLY FAIL %s read b=%llu rr=%x st=%04x", phase, block, result, status);
      };

      readBlock (ctrLba, "counter before", 0);
      if (memcmp (dma, "D2XQ", 4) != 0 || load32le (dma + 4) != 0 ||
          load64le (dma + 8) != totalChunks ||
          memcmp (dma + 16, safeCounterMagic, sizeof (safeCounterMagic)) != 0)
        panic ("CI192 FINAL READONLY FAIL counter state counter=%llu repair=%u", load64le (dma + 8), load32le (dma + 4));
      SHA256_CTX counterSha;
      uint8_t counterDigest[SHA256_DIGEST_LENGTH];
      SHA256_Init (&counterSha);
      SHA256_Update (&counterSha, dma, 64);
      SHA256_Final (counterDigest, &counterSha);
      if (memcmp (dma + 24, imageSha, SHA256_DIGEST_LENGTH) != 0 ||
          load64le (dma + 56) != ~(uint64_t) totalChunks ||
          memcmp (dma + 64, counterDigest, SHA256_DIGEST_LENGTH) != 0)
        panic ("CI192 FINAL READONLY FAIL counter checksum");
      for (unsigned b = 96; b < 4096; b++)
        if (dma[b] != 0) panic ("CI192 FINAL READONLY FAIL counter reserved b=%u", b);
      static uint8_t savedCounter[4096];
      memcpy (savedCounter, dma, sizeof (savedCounter));
      d2xq_console_text ("CI192 COUNTER140 OK ");

      SHA256_CTX wholeSha;
      SHA256_Init (&wholeSha);
      uint64_t bytesHashed = 0;
      uint64_t paddingChecked = 0;
      for (unsigned slot = 0; slot < totalChunks; slot++) {
        SHA256_CTX chunkSha;
        SHA256_Init (&chunkSha);
        for (uint64_t block = 0; block < chunkBlocks; block++) {
          if ((block & 4095U) == 0) {
            char marker[32];
            snprintf (marker, sizeof (marker), "V%u.%llu ", slot, block / 4096);
            d2xq_console_text (marker);
          }
          const uint64_t imageBlock = (uint64_t) slot * chunkBlocks + block;
          readBlock (layout->userdataFirst + imageBlock, "target", imageBlock);
          SHA256_Update (&chunkSha, dma, 4096);
          if (bytesHashed < imageBytes) {
            SHA256_Update (&wholeSha, dma, 4096);
            bytesHashed += 4096;
          } else {
            for (unsigned b = 0; b < 4096; b++)
              if (dma[b] != 0) panic ("CI192 FINAL READONLY FAIL padding block=%llu byte=%u", imageBlock, b);
            paddingChecked += 4096;
          }
        }
        uint8_t chunkDigest[SHA256_DIGEST_LENGTH];
        SHA256_Final (chunkDigest, &chunkSha);
        if (memcmp (chunkDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0)
          panic ("CI192 FINAL READONLY FAIL chunk SHA slot=%u", slot);
        char marker[24];
        snprintf (marker, sizeof (marker), "H%u ", slot);
        d2xq_console_text (marker);
      }
      uint8_t wholeDigest[SHA256_DIGEST_LENGTH];
      SHA256_Final (wholeDigest, &wholeSha);
      if (bytesHashed != imageBytes || paddingChecked != paddedBytes - imageBytes ||
          memcmp (wholeDigest, imageSha, SHA256_DIGEST_LENGTH) != 0)
        panic ("CI192 FINAL READONLY FAIL image SHA bytes=%llu padding=%llu", bytesHashed, paddingChecked);
      d2xq_console_text ("CI192 IMAGE SHA256 OK CI192 PADDING ZERO OK ");

      readBlock (ctrLba, "counter after", 0);
      if (memcmp (dma, savedCounter, sizeof (savedCounter)) != 0)
        panic ("CI192 FINAL READONLY FAIL counter changed");
      d2xq_console_text ("CI192 COUNTER UNCHANGED ");
      panic ("CI192 FINAL READONLY PASS chunks=%u image=%llu padding=%llu counter=%llu",
             totalChunks, bytesHashed, paddingChecked, load64le (dma + 8));
    }
