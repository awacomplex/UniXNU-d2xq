      // CI185: entered only after payload and durable progress were verified.
      // Samsung's "download" reason is PM8150 PON SOFT_RB_SPARE, not MISC.
      // Pinned same-device evidence and safety limits: CI185_RESET.md.
      d2xq_console_text ("CI185 P0 ");
      OSSharedPtr<IOMemoryMap> ci185CoreMap, ci185CnfgMap, ci185WdtMap;
      OSSharedPtr<IOMemoryMap> ci185ObsMap, ci185RwMap;
      uintptr_t ci185Core = mapPhys (0x0c440000ULL, 0x1100, ci185CoreMap);
      uintptr_t ci185Cnfg = mapPhys (0x0c40a000ULL, 0x1000, ci185CnfgMap);
      uintptr_t ci185Wdt = mapPhys (0x17c10000ULL, 0x1000, ci185WdtMap);
      if (!ci185Core || !ci185Cnfg || !ci185Wdt)
        panic ("CI185 PASS slot=%u counter=%llu RESET map core=%u cnfg=%u wdt=%u", slot,
               finalCounter, (unsigned) !!ci185Core, (unsigned) !!ci185Cnfg, (unsigned) !!ci185Wdt);

      // Linux selects v5 at >=50000000. Reject unreviewed later majors.
      uint32_t ci185Version = read32 (ci185Core, 0);
      if (ci185Version < 0x50000000U || ci185Version >= 0x60000000U)
        panic ("CI185 PASS slot=%u RESET SPMI version=%08x", slot, ci185Version);
      const unsigned ci185Ee = 0;
      unsigned ci185Apid = 512, ci185Matches = 0, ci185OwnedMatches = 0;
      uint32_t ci185Entry = 0;
      // v5 permits duplicate PPIDs. Like Linux, retain the last APID owned
      // by this EE; APID zero is valid. Other EEs never gain write access.
      for (unsigned ci185Index = 0; ci185Index < 512; ci185Index++) {
        uint32_t ci185Candidate = read32 (ci185Core, 0x900 + 4 * ci185Index);
        if (!ci185Candidate || ((ci185Candidate >> 8) & 0xfffU) != 0x008U)
          continue;
        ci185Matches++;
        unsigned ci185Owner = read32 (ci185Cnfg, 0x700 + 4 * ci185Index) & 7U;
        if (ci185Owner == ci185Ee) {
          ci185Apid = ci185Index;
          ci185Entry = ci185Candidate;
          ci185OwnedMatches++;
        }
      }
      if (ci185Apid >= 512)
        panic ("CI185 PASS slot=%u RESET SPMI no PON owner ee=%u matches=%u ver=%08x",
               slot, ci185Ee, ci185Matches, ci185Version);

      // Map just the selected channel pages. OBS starts at an offset within
      // its mapped page; every mapping stays retained through the reset.
      uint64_t ci185ObsPhys = 0x0e600000ULL + 0x10000ULL * ci185Ee + 0x80ULL * ci185Apid;
      uintptr_t ci185ObsPage = mapPhys (ci185ObsPhys & ~0xfffULL, 0x1000, ci185ObsMap);
      uintptr_t ci185Rw = mapPhys (0x0c600000ULL + 0x10000ULL * ci185Apid, 0x1000, ci185RwMap);
      if (!ci185ObsPage || !ci185Rw)
        panic ("CI185 PASS slot=%u RESET SPMI map apid=%u obs=%u rw=%u", slot, ci185Apid,
               (unsigned) !!ci185ObsPage, (unsigned) !!ci185Rw);
      uintptr_t ci185Obs = ci185ObsPage + (uintptr_t) (ci185ObsPhys & 0xfffULL);

      // Prepare/check the proven APSS WDT before the persistent PMIC write.
      // watchdog_v2.c: RST=04 EN=08 STS=0c BARK=10 BITE=14, WDT_HZ=32765.
      write32 (ci185Wdt, 0x08, 0);
      asm volatile ("dsb sy" ::: "memory");
      write32 (ci185Wdt, 0x10, 5U * 32765U);
      write32 (ci185Wdt, 0x14, 32765U);
      asm volatile ("dsb sy" ::: "memory");
      if ((read32 (ci185Wdt, 0x08) & 1U) != 0 ||
          read32 (ci185Wdt, 0x10) != 5U * 32765U ||
          read32 (ci185Wdt, 0x14) != 32765U)
        panic ("CI185 PASS slot=%u RESET WDT config en=%x bark=%x bite=%x", slot,
               read32 (ci185Wdt, 0x08), read32 (ci185Wdt, 0x10), read32 (ci185Wdt, 0x14));

      auto ci185Wait = [&] (uintptr_t ci185Channel, const char *ci185Stage) {
        uint32_t ci185Status = 0;
        // pmic_arb_wait_for_done(): at most 1000 polls with 1 us delay.
        // No undocumented STATUS clear or CONFIG write.
        for (unsigned ci185Poll = 0; ci185Poll < 1000; ci185Poll++) {
          ci185Status = read32 (ci185Channel, 0x08);
          if ((ci185Status & 1U) != 0) {
            if ((ci185Status & 0x0eU) != 0)
              panic ("CI185 PASS slot=%u RESET SPMI %s status=%x apid=%u ver=%08x", slot,
                     ci185Stage, ci185Status, ci185Apid, ci185Version);
            asm volatile ("dsb sy" ::: "memory");
            return;
          }
          IODelay (1);
        }
        panic ("CI185 PASS slot=%u RESET SPMI %s timeout status=%x apid=%u ver=%08x", slot,
               ci185Stage, ci185Status, ci185Apid, ci185Version);
      };
      auto ci185Read = [&] (unsigned ci185Address, const char *ci185Stage) -> uint8_t {
        if ((ci185Address >> 8) != 0x08U)
          panic ("CI185 PASS slot=%u RESET SPMI bad PON address=%x", slot, ci185Address);
        // v5 fmt_cmd_v2: EXT_READL=1, low address byte, len-1=0.
        write32 (ci185Obs, 0, 0x08000000U | ((ci185Address & 0xffU) << 4));
        asm volatile ("dsb sy" ::: "memory");
        ci185Wait (ci185Obs, ci185Stage);
        return (uint8_t) (read32 (ci185Obs, 0x18) & 0xffU);
      };

      // Gen2 subtype 04/05 uses reason bits 7:1; Gen1 uses bits 7:2.
      uint8_t ci185Subtype = ci185Read (0x0805U, "subtype");
      if (ci185Subtype != 0x04U && ci185Subtype != 0x05U)
        panic ("CI185 PASS slot=%u RESET PON subtype=%02x apid=%u ver=%08x", slot,
               (unsigned) ci185Subtype, ci185Apid, ci185Version);
      uint8_t ci185Old = ci185Read (0x088fU, "reason read");
      uint8_t ci185New = (uint8_t) ((ci185Old & 1U) | (0x15U << 1));

      // Fail closed if the selected map, owner or version changed.
      if (read32 (ci185Core, 0) != ci185Version ||
          read32 (ci185Core, 0x900 + 4 * ci185Apid) != ci185Entry ||
          (read32 (ci185Cnfg, 0x700 + 4 * ci185Apid) & 7U) != ci185Ee)
        panic ("CI185 PASS slot=%u RESET SPMI mapping changed apid=%u ver=%08x", slot,
               ci185Apid, ci185Version);
      char ci185Marker[192];
      snprintf (ci185Marker, sizeof (ci185Marker),
                "CI185 P1 ver=%08x apid=%u ee=%u matches=%u owned=%u subtype=%02x old=%02x new=%02x ",
                ci185Version, ci185Apid, ci185Ee, ci185Matches, ci185OwnedMatches,
                (unsigned) ci185Subtype, (unsigned) ci185Old, (unsigned) ci185New);
      d2xq_console_text (ci185Marker);
      // EXT_WRITEL=0, addr=8f, one byte: CMD=000008f0. Preserve bit zero.
      write32 (ci185Rw, 0x10, ci185New);
      asm volatile ("dsb sy" ::: "memory");
      write32 (ci185Rw, 0, 0x000008f0U);
      asm volatile ("dsb sy" ::: "memory");
      ci185Wait (ci185Rw, "reason write");
      uint8_t ci185Actual = ci185Read (0x088fU, "reason verify");
      if (ci185Actual != ci185New)
        panic ("CI185 PASS slot=%u RESET PON verify old=%02x new=%02x got=%02x apid=%u ver=%08x",
               slot, (unsigned) ci185Old, (unsigned) ci185New, (unsigned) ci185Actual,
               ci185Apid, ci185Version);
      snprintf (ci185Marker, sizeof (ci185Marker),
                "CI185 P2 old=%02x new=%02x got=%02x apid=%u ver=%08x OK%u:%llu R ",
                (unsigned) ci185Old, (unsigned) ci185New, (unsigned) ci185Actual,
                ci185Apid, ci185Version, slot, finalCounter);
      d2xq_console_text (ci185Marker);
      write32 (ci185Wdt, 0x08, 1);
      write32 (ci185Wdt, 0x04, 1);
      asm volatile ("dsb sy" ::: "memory");
      uint64_t ci185ResetDeadline = 0;
      clock_interval_to_deadline (10, NSEC_PER_SEC, &ci185ResetDeadline);
      for (uint64_t ci185Spin = 0;
           ci185Spin < 1000000000ULL && mach_absolute_time () < ci185ResetDeadline;
           ci185Spin++) {
        asm volatile ("yield" ::: "memory");
      }
      panic ("CI185 PASS slot=%u counter=%llu RESET WDT failed sts=%x en=%x bark=%x bite=%x",
             slot, finalCounter, read32 (ci185Wdt, 0x0c), read32 (ci185Wdt, 0x08),
             read32 (ci185Wdt, 0x10), read32 (ci185Wdt, 0x14));
