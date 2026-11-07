      {
        // This is a fresh normal-ingest commit, not an ARM-record consume.
        // The latch is set only after the complete progress block was written,
        // flushed and read back.  The original stale/ALREADY guards run before
        // payload processing and prevent this authorization on a later boot.
        if (!ci191IngestCommitted || already ||
            memcmp (sourceDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0 ||
            memcmp (targetDigest, expectedSha[slot], SHA256_DIGEST_LENGTH) != 0 ||
            memcmp (dma, savedCounter, sizeof (savedCounter)) != 0 ||
            load64le (dma + 8) != finalCounter)
          panic ("CI191 RESET REFUSE ingest commit mode=%u slot=%u counter=%llu",
                 mode, slot, finalCounter);
        if (mode == 1) {
          if (slot >= 7 || expectedCounter != 7 || counter != 7 ||
              nextRepairSlot != slot || finalCounter != 7 ||
              load32le (dma + 4) != slot + 1 ||
              memcmp (dma + 16, repairCounterMagic, sizeof (repairCounterMagic)) != 0)
            panic ("CI191 RESET REFUSE repair commit slot=%u counter=%llu next=%u",
                   slot, finalCounter, load32le (dma + 4));
        } else if (mode == 2) {
          if (slot < 7 || slot >= totalChunks || expectedCounter != slot ||
              counter != slot || finalCounter != (uint64_t) slot + 1 ||
              load32le (dma + 4) != 0 ||
              memcmp (dma + 16, safeCounterMagic, sizeof (safeCounterMagic)) != 0)
            panic ("CI191 RESET REFUSE append commit slot=%u counter=%llu next=%u",
                   slot, finalCounter, load32le (dma + 4));
        } else {
          panic ("CI191 RESET REFUSE commit mode=%u", mode);
        }
        char ci191CommitMarker[112];
        snprintf (ci191CommitMarker, sizeof (ci191CommitMarker),
                  "CI191 COMMITTED mode=%u slot=%u counter=%llu repair=%u ",
                  mode, slot, finalCounter, load32le (dma + 4));
        d2xq_console_text (ci191CommitMarker);
        /* CI191_RESET_HARDWARE */
      }
