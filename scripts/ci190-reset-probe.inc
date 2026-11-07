      {
        // Each exact host arming record permits at most one reset attempt.
        // This probe never reaches the following ingest or progress updates.
        d2xq_console_text ("CI190 P0 ");
        if (!repairCounter || counter != 7 || nextRepairSlot != 2 ||
            recoveryFirst != 0x24f13ULL || chunkBlocks != 16384ULL)
          panic ("CI190 RESET REFUSE repair=%u counter=%llu next=%u",
                 (unsigned) repairCounter, counter, nextRepairSlot);
        d2xq_ufs_stage = -1;
        static const uint8_t ci190ArmSha[32] = {
          0xe8, 0x9c, 0x31, 0xa3, 0x53, 0x32, 0x11, 0xeb,
          0x37, 0x96, 0x36, 0xdf, 0x3e, 0x57, 0xb7, 0x9a,
          0x3d, 0x9e, 0xfa, 0x3f, 0x43, 0xd1, 0x1e, 0x72,
          0xcf, 0x93, 0x2c, 0x6a, 0xf2, 0x97, 0x5e, 0x49
        };
        d2xq_console_text ("CI190 A0 ");
        readBlock (recoveryFirst, "CI190 arm", 0);
        SHA256_CTX ci190ArmContext;
        uint8_t ci190ArmDigest[SHA256_DIGEST_LENGTH];
        SHA256_Init (&ci190ArmContext);
        SHA256_Update (&ci190ArmContext, dma, 4096);
        SHA256_Final (ci190ArmDigest, &ci190ArmContext);
        if (memcmp (ci190ArmDigest, ci190ArmSha, sizeof (ci190ArmSha)) != 0)
          panic ("CI190 RESET READY arm absent or consumed got=%llx", digestPrefix (ci190ArmDigest));

        // The general hostWrite4k policy remains unchanged. This local command
        // authorizes exactly one zero-filled block at the fixed RECOVERY LBA.
        // It never accepts a caller-provided address, length, LUN or payload.
        static const uint8_t ci190Consumed[4096] = {};
        memcpy (dma, ci190Consumed, sizeof (ci190Consumed));
        uint8_t ci190ConsumeCdb[10] = { 0x2a, 0, 0, 0x02, 0x4f, 0x13, 0, 0, 1, 0 };
        uint32_t ci190ConsumeStatus = 0;
        IOReturn ci190ConsumeResult = hostScsiDir (0, ci190ConsumeCdb, 10, 1, 4096, 1900000,
                                                 &ci190ConsumeStatus);
        if (ci190ConsumeResult != kIOReturnSuccess || ci190ConsumeStatus != 0)
          panic ("CI190 RESET FAIL consume rr=%x st=%04x", ci190ConsumeResult, ci190ConsumeStatus);
        uint8_t ci190FlushCdb[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint32_t ci190FlushStatus = 0;
        IOReturn ci190FlushResult = hostScsiDir (0, ci190FlushCdb, 10, 0, 0, 1900000,
                                               &ci190FlushStatus);
        if (ci190FlushResult != kIOReturnSuccess || ci190FlushStatus != 0)
          panic ("CI190 RESET FAIL consume flush rr=%x st=%04x", ci190FlushResult, ci190FlushStatus);
        readBlock (recoveryFirst, "CI190 consumed verify", 0);
        if (memcmp (dma, ci190Consumed, sizeof (ci190Consumed)) != 0)
          panic ("CI190 RESET FAIL consumed readback");
        d2xq_console_text ("CI190 A1 ");
        bool ci190ArmConsumed = true;
        /* CI190_RESET_HARDWARE */
      }
