    {
      // CI183 audits the first seven image windows.  This block only issues
      // READ(10) commands; it does not advance the counter or touch MISC.
      static const uint64_t chunkBlocks = 16384;
      static const uint32_t sampleBlocks[4] = { 0, 260, 8192, 16383 };
      static const uint64_t sampleShaPrefix[7][4] = {
        { 0xbc908a6cf6649105ULL, 0x5327d75be7c04891ULL, 0xb5af7121aaaa0956ULL, 0xad7facb2586fc6e9ULL },
        { 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL },
        { 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL },
        { 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL, 0xad7facb2586fc6e9ULL },
        { 0xad7facb2586fc6e9ULL, 0x49341fc3e5c6ce88ULL, 0xade4ee2ebf9cd3b2ULL, 0x457e0e3f9e7c9e59ULL },
        { 0xad2f03a3252370a5ULL, 0x406412e7a29089e1ULL, 0x1708ce71bea4abe1ULL, 0x428e86ff724e4f85ULL },
        { 0x501f0aa2572be2f8ULL, 0x26ad1e94af6d3163ULL, 0xad68f1718e586e34ULL, 0xad7facb2586fc6e9ULL }
      };
      static const char *const expectedSha[7] = {
        "cd44ee892fd8f012c5ccf0801fa612efe9735e4819f1bc4d364ef0e93df8ea5f",
        "3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351",
        "3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351",
        "3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351",
        "3bd7852cd1d982e6585c6724dc9bb01a113cbca43a8816e242076761bcd3ec41",
        "dd5b815db8d76f412d557bc54383cd07d25570addce2c3a7ae7664a2f455634c",
        "694424012e370cf9c782ac424b7df9cadc125b9ba85008f511db5518f418ab25"
      };
      uint8_t *dma = (uint8_t *) gHost.virt + kDataOffset;
      if (layout->userdataFirst != 0x21f400ULL || layout->userdataLast != 0x3b90bfaULL)
        panic ("AUDIT FAIL layout first=%llx last=%llx", layout->userdataFirst, layout->userdataLast);
      const uint64_t ctrLba = layout->userdataLast;
      uint8_t ctrCdb[10] = { 0x28, 0, (uint8_t) (ctrLba >> 24), (uint8_t) (ctrLba >> 16),
                             (uint8_t) (ctrLba >> 8), (uint8_t) ctrLba, 0, 0, 1, 0 };
      d2xq_console_text ("A0 ");
      uint32_t ctrStatus = 0;
      IOReturn ctrResult = hostScsiDir (0, ctrCdb, 10, 2, 4096, 1900000, &ctrStatus);
      if (ctrResult != kIOReturnSuccess || ctrStatus != 0)
        panic ("AUDIT FAIL counter read rr=%x st=%04x", ctrResult, ctrStatus);
      if (dma[0] != 'D' || dma[1] != '2' || dma[2] != 'X' || dma[3] != 'Q')
        panic ("AUDIT FAIL counter magic");
      uint64_t index = 0;
      for (unsigned b = 0; b < 8; b++) index |= (uint64_t) dma[8 + b] << (8 * b);
      if (index != 7) panic ("AUDIT FAIL counter=%llu expected=7", index);
      d2xq_console_text ("A7 ");
      // The 20-second startup watchdog is not a suitable limit for 448 MiB
      // of intentional readback.  Per-command poll limits still apply.
      d2xq_ufs_stage = -1;

      // First test four representative blocks in every window, including the
      // first nonzero block (260) of window 4.  Fail early on a wrong window.
      for (unsigned window = 0; window < 7; window++) {
        for (unsigned sample = 0; sample < 4; sample++) {
          uint64_t lba = layout->userdataFirst + (uint64_t) window * chunkBlocks + sampleBlocks[sample];
          uint8_t cdb[10] = { 0x28, 0, (uint8_t) (lba >> 24), (uint8_t) (lba >> 16),
                              (uint8_t) (lba >> 8), (uint8_t) lba, 0, 0, 1, 0 };
          uint32_t status = 0;
          IOReturn result = hostScsiDir (0, cdb, 10, 2, 4096, 1900000, &status);
          if (result != kIOReturnSuccess || status != 0)
            panic ("AUDIT FAIL counter=7 sample w=%u b=%u rr=%x st=%04x",
                   window, sampleBlocks[sample], result, status);
          SHA256_CTX sha;
          uint8_t digest[SHA256_DIGEST_LENGTH];
          SHA256_Init (&sha);
          SHA256_Update (&sha, dma, 4096);
          SHA256_Final (digest, &sha);
          uint64_t prefix = 0;
          for (unsigned b = 0; b < 8; b++) prefix = (prefix << 8) | digest[b];
          if (prefix != sampleShaPrefix[window][sample])
            panic ("AUDIT FAIL counter=7 sample w=%u b=%u got=%llx", window, sampleBlocks[sample], prefix);
        }
        char marker[16];
        snprintf (marker, sizeof (marker), "S%u ", window);
        d2xq_console_text (marker);
      }
      d2xq_console_text ("SOK ");

      // Hash every byte of windows 0..6.  One READ(10) fills the 4 KiB DMA
      // buffer before each SHA256_Update; there are no write commands.
      for (unsigned window = 0; window < 7; window++) {
        SHA256_CTX sha;
        SHA256_Init (&sha);
        for (uint64_t block = 0; block < chunkBlocks; block++) {
          if ((block & 1023U) == 0) {
            char marker[20];
            snprintf (marker, sizeof (marker), "V%u.%llu ", window, block / 1024);
            d2xq_console_text (marker);
          }
          uint64_t lba = layout->userdataFirst + (uint64_t) window * chunkBlocks + block;
          uint8_t cdb[10] = { 0x28, 0, (uint8_t) (lba >> 24), (uint8_t) (lba >> 16),
                              (uint8_t) (lba >> 8), (uint8_t) lba, 0, 0, 1, 0 };
          uint32_t status = 0;
          IOReturn result = hostScsiDir (0, cdb, 10, 2, 4096, 1900000, &status);
          if (result != kIOReturnSuccess || status != 0)
            panic ("AUDIT FAIL counter=7 read w=%u b=%llu rr=%x st=%04x", window, block, result, status);
          SHA256_Update (&sha, dma, 4096);
        }
        uint8_t digest[SHA256_DIGEST_LENGTH];
        SHA256_Final (digest, &sha);
        static const char hex[] = "0123456789abcdef";
        char actualSha[65];
        for (unsigned b = 0; b < SHA256_DIGEST_LENGTH; b++) {
          actualSha[b * 2] = hex[digest[b] >> 4];
          actualSha[b * 2 + 1] = hex[digest[b] & 15];
        }
        actualSha[64] = 0;
        if (memcmp (actualSha, expectedSha[window], 64) != 0)
          panic ("AUDIT FAIL counter=7 SHA w=%u got=%.16s expected=%.16s",
                 window, actualSha, expectedSha[window]);
        char marker[16];
        snprintf (marker, sizeof (marker), "P%u ", window);
        d2xq_console_text (marker);
      }
      panic ("AUDIT PASS counter=7 SHA256 windows 0..6");
    }
