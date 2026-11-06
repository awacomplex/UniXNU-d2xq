      // The payload and any counter update have been flushed and verified.
      // Try the boot-control request used by the earlier ingest builds, then
      // trigger the non-secure APSS watchdog from the d2xq SM8150 device tree.
      OSSharedPtr<IOMemoryMap> ci184WdtMap;
      uintptr_t ci184Wdt = mapPhys (0x17c10000ULL, 0x1000, ci184WdtMap);
      if (!ci184Wdt)
        panic ("CI184 PASS slot=%u counter=%llu RESET map failed", slot, finalCounter);

      const uint64_t ci184MiscLba = 0x6513;
      uint8_t ci184MiscRead[10] = { 0x28, 0, (uint8_t) (ci184MiscLba >> 24),
          (uint8_t) (ci184MiscLba >> 16), (uint8_t) (ci184MiscLba >> 8),
          (uint8_t) ci184MiscLba, 0, 0, 1, 0 };
      uint32_t ci184ResetStatus = 0;
      IOReturn ci184ResetResult = hostScsiDir (0, ci184MiscRead, 10, 2, 4096, 1900000, &ci184ResetStatus);
      if (ci184ResetResult != kIOReturnSuccess || ci184ResetStatus != 0)
        panic ("CI184 PASS slot=%u RESET misc read rr=%x st=%x", slot, ci184ResetResult, ci184ResetStatus);
      uint8_t *ci184Misc = gScratch;
      memcpy (ci184Misc, dma, 4096);
      memset (ci184Misc, 0, 32);
      memcpy (ci184Misc, "bootloader", 10);
      memcpy (dma, ci184Misc, 4096);
      uint8_t ci184MiscWrite[10] = { 0x2a, 0, (uint8_t) (ci184MiscLba >> 24),
          (uint8_t) (ci184MiscLba >> 16), (uint8_t) (ci184MiscLba >> 8),
          (uint8_t) ci184MiscLba, 0, 0, 1, 0 };
      d2xq_console_text ("M0 ");
      ci184ResetStatus = 0;
      ci184ResetResult = hostScsiDir (0, ci184MiscWrite, 10, 1, 4096, 1900000, &ci184ResetStatus);
      if (ci184ResetResult != kIOReturnSuccess || ci184ResetStatus != 0)
        panic ("CI184 PASS slot=%u RESET misc write rr=%x st=%x", slot, ci184ResetResult, ci184ResetStatus);
      uint8_t ci184Sync[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
      ci184ResetStatus = 0;
      ci184ResetResult = hostScsiDir (0, ci184Sync, 10, 0, 0, 1900000, &ci184ResetStatus);
      if (ci184ResetResult != kIOReturnSuccess || ci184ResetStatus != 0)
        panic ("CI184 PASS slot=%u RESET misc flush rr=%x st=%x", slot, ci184ResetResult, ci184ResetStatus);
      ci184ResetStatus = 0;
      ci184ResetResult = hostScsiDir (0, ci184MiscRead, 10, 2, 4096, 1900000, &ci184ResetStatus);
      if (ci184ResetResult != kIOReturnSuccess || ci184ResetStatus != 0 || memcmp (dma, ci184Misc, 4096) != 0)
        panic ("CI184 PASS slot=%u RESET misc verify rr=%x st=%x", slot, ci184ResetResult, ci184ResetStatus);
      d2xq_console_text ("M1 ");

      // watchdog_v2.c: RST=0x04, EN=0x08, STS=0x0c, BARK=0x10,
      // BITE=0x14.  The same-device driver uses WDT_HZ=32765 and allows
      // BITE before BARK in msm_trigger_wdog_bite().
      write32 (ci184Wdt, 0x08, 0);
      asm volatile ("dsb sy" ::: "memory");
      write32 (ci184Wdt, 0x10, 5U * 32765U);
      write32 (ci184Wdt, 0x14, 32765U);
      asm volatile ("dsb sy" ::: "memory");
      if ((read32 (ci184Wdt, 0x08) & 1U) != 0 ||
          read32 (ci184Wdt, 0x10) != 5U * 32765U ||
          read32 (ci184Wdt, 0x14) != 32765U)
        panic ("CI184 PASS slot=%u RESET WDT config en=%x bark=%x bite=%x", slot,
               read32 (ci184Wdt, 0x08), read32 (ci184Wdt, 0x10), read32 (ci184Wdt, 0x14));
      char ci184Done[48];
      snprintf (ci184Done, sizeof (ci184Done), "OK%u:%llu R ", slot, finalCounter);
      d2xq_console_text (ci184Done);
      write32 (ci184Wdt, 0x08, 1);
      write32 (ci184Wdt, 0x04, 1);
      asm volatile ("dsb sy" ::: "memory");
      uint64_t ci184ResetDeadline = 0;
      clock_interval_to_deadline (10, NSEC_PER_SEC, &ci184ResetDeadline);
      for (uint64_t ci184Spin = 0;
           ci184Spin < 1000000000ULL && mach_absolute_time () < ci184ResetDeadline;
           ci184Spin++) {
        asm volatile ("yield" ::: "memory");
      }
      panic ("CI184 PASS slot=%u counter=%llu RESET WDT failed sts=%x en=%x bark=%x bite=%x",
             slot, finalCounter, read32 (ci184Wdt, 0x0c), read32 (ci184Wdt, 0x08),
             read32 (ci184Wdt, 0x10), read32 (ci184Wdt, 0x14));
