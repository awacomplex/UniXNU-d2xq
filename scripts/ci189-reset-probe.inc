      {
        // Each exact host arming record permits at most one reset attempt.
        // This probe never reaches the following ingest or progress updates.
        d2xq_console_text ("CI189 P0 ");
        if (!repairCounter || counter != 7 || nextRepairSlot != 2 ||
            recoveryFirst != 0x24f13ULL || chunkBlocks != 16384ULL)
          panic ("CI189 RESET REFUSE repair=%u counter=%llu next=%u",
                 (unsigned) repairCounter, counter, nextRepairSlot);
        d2xq_ufs_stage = -1;
        static const uint8_t ci189ArmSha[32] = {
          0xc3, 0xa6, 0x49, 0x11, 0xc1, 0xa2, 0x3d, 0xdb,
          0xa3, 0xa0, 0xa3, 0x6e, 0x8d, 0x3e, 0x31, 0xd7,
          0x99, 0xf6, 0xb0, 0xaa, 0xa8, 0x9a, 0x45, 0x86,
          0xc5, 0x40, 0x04, 0xac, 0xf3, 0xe8, 0x66, 0xea
        };
        d2xq_console_text ("CI189 A0 ");
        readBlock (recoveryFirst, "CI189 arm", 0);
        SHA256_CTX ci189ArmContext;
        uint8_t ci189ArmDigest[SHA256_DIGEST_LENGTH];
        SHA256_Init (&ci189ArmContext);
        SHA256_Update (&ci189ArmContext, dma, 4096);
        SHA256_Final (ci189ArmDigest, &ci189ArmContext);
        if (memcmp (ci189ArmDigest, ci189ArmSha, sizeof (ci189ArmSha)) != 0)
          panic ("CI189 RESET READY arm absent or consumed got=%llx", digestPrefix (ci189ArmDigest));

        // The general hostWrite4k policy remains unchanged. This local command
        // authorizes exactly one zero-filled block at the fixed RECOVERY LBA.
        // It never accepts a caller-provided address, length, LUN or payload.
        static const uint8_t ci189Consumed[4096] = {};
        memcpy (dma, ci189Consumed, sizeof (ci189Consumed));
        uint8_t ci189ConsumeCdb[10] = { 0x2a, 0, 0, 0x02, 0x4f, 0x13, 0, 0, 1, 0 };
        uint32_t ci189ConsumeStatus = 0;
        IOReturn ci189ConsumeResult = hostScsiDir (0, ci189ConsumeCdb, 10, 1, 4096, 1900000,
                                                 &ci189ConsumeStatus);
        if (ci189ConsumeResult != kIOReturnSuccess || ci189ConsumeStatus != 0)
          panic ("CI189 RESET FAIL consume rr=%x st=%04x", ci189ConsumeResult, ci189ConsumeStatus);
        uint8_t ci189FlushCdb[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint32_t ci189FlushStatus = 0;
        IOReturn ci189FlushResult = hostScsiDir (0, ci189FlushCdb, 10, 0, 0, 1900000,
                                               &ci189FlushStatus);
        if (ci189FlushResult != kIOReturnSuccess || ci189FlushStatus != 0)
          panic ("CI189 RESET FAIL consume flush rr=%x st=%04x", ci189FlushResult, ci189FlushStatus);
        readBlock (recoveryFirst, "CI189 consumed verify", 0);
        if (memcmp (dma, ci189Consumed, sizeof (ci189Consumed)) != 0)
          panic ("CI189 RESET FAIL consumed readback");
        d2xq_console_text ("CI189 A1 ");
        bool ci189ArmConsumed = true;
        /* CI189_RESET_HARDWARE */
      }
