      {
        // Diagnostic-only entry, after the complete trusted-counter checks.
        // Never reach metadata processing, ingest, the CI185 reset or a write.
        d2xq_console_text ("CI186 P0 ");
        if (!repairCounter || counter != 7 || nextRepairSlot != 2)
          panic ("CI186 READONLY REFUSE repair=%u counter=%llu next=%u",
                 (unsigned) repairCounter, counter, nextRepairSlot);
        // Only RAM diagnostic state: keep the startup watchdog from masking
        // the exact MMIO operation during deliberately verbose screen output.
        d2xq_ufs_stage = -1;

        char ci186Marker[160];
        OSSharedPtr<IOMemoryMap> ci186CoreMap, ci186CnfgMap, ci186WdtMap;
        OSSharedPtr<IOMemoryMap> ci186ObsMap, ci186RwMap;
        d2xq_console_text ("MC ");
        uintptr_t ci186Core = mapPhys (0x0c440000ULL, 0x1100, ci186CoreMap);
        snprintf (ci186Marker, sizeof (ci186Marker), "mC=%llx ", (uint64_t) ci186Core);
        d2xq_console_text (ci186Marker);
        if (!ci186Core) panic ("CI186 READONLY FAIL core map");
        d2xq_console_text ("MN ");
        uintptr_t ci186Cnfg = mapPhys (0x0c40a000ULL, 0x1000, ci186CnfgMap);
        snprintf (ci186Marker, sizeof (ci186Marker), "mN=%llx ", (uint64_t) ci186Cnfg);
        d2xq_console_text (ci186Marker);
        if (!ci186Cnfg) panic ("CI186 READONLY FAIL cnfg map");
        d2xq_console_text ("MW ");
        uintptr_t ci186Wdt = mapPhys (0x17c10000ULL, 0x1000, ci186WdtMap);
        snprintf (ci186Marker, sizeof (ci186Marker), "mW=%llx ", (uint64_t) ci186Wdt);
        d2xq_console_text (ci186Marker);
        if (!ci186Wdt) panic ("CI186 READONLY FAIL WDT map");

        d2xq_console_text ("V ");
        uint32_t ci186Version = read32 (ci186Core, 0);
        snprintf (ci186Marker, sizeof (ci186Marker), "v=%08x ", ci186Version);
        d2xq_console_text (ci186Marker);
        if (ci186Version < 0x50000000U || ci186Version >= 0x60000000U)
          panic ("CI186 READONLY FAIL version=%08x", ci186Version);

        const unsigned ci186Ee = 0;
        unsigned ci186Apid = 512, ci186Matches = 0, ci186OwnedMatches = 0;
        uint32_t ci186Entry = 0;
        for (unsigned ci186Index = 0; ci186Index < 512; ci186Index++) {
          snprintf (ci186Marker, sizeof (ci186Marker), "E%u ", ci186Index);
          d2xq_console_text (ci186Marker);
          uint32_t ci186Candidate = read32 (ci186Core, 0x900 + 4 * ci186Index);
          d2xq_console_text ("e ");
          if (!ci186Candidate || ((ci186Candidate >> 8) & 0xfffU) != 0x008U)
            continue;
          ci186Matches++;
          snprintf (ci186Marker, sizeof (ci186Marker), "O%u ", ci186Index);
          d2xq_console_text (ci186Marker);
          unsigned ci186Owner = read32 (ci186Cnfg, 0x700 + 4 * ci186Index) & 7U;
          snprintf (ci186Marker, sizeof (ci186Marker), "o%u ", ci186Owner);
          d2xq_console_text (ci186Marker);
          if (ci186Owner == ci186Ee) {
            ci186Apid = ci186Index;
            ci186Entry = ci186Candidate;
            ci186OwnedMatches++;
          }
        }
        if (ci186Apid >= 512)
          panic ("CI186 READONLY FAIL no PON owner ee=%u matches=%u ver=%08x",
                 ci186Ee, ci186Matches, ci186Version);
        snprintf (ci186Marker, sizeof (ci186Marker),
                  "APID=%u entry=%08x matches=%u owned=%u ",
                  ci186Apid, ci186Entry, ci186Matches, ci186OwnedMatches);
        d2xq_console_text (ci186Marker);

        uint64_t ci186ObsPhys = 0x0e600000ULL + 0x10000ULL * ci186Ee + 0x80ULL * ci186Apid;
        d2xq_console_text ("MO ");
        uintptr_t ci186ObsPage = mapPhys (ci186ObsPhys & ~0xfffULL, 0x1000, ci186ObsMap);
        snprintf (ci186Marker, sizeof (ci186Marker), "mO=%llx ", (uint64_t) ci186ObsPage);
        d2xq_console_text (ci186Marker);
        if (!ci186ObsPage) panic ("CI186 READONLY FAIL OBS map apid=%u", ci186Apid);
        uintptr_t ci186Obs = ci186ObsPage + (uintptr_t) (ci186ObsPhys & 0xfffULL);
        // Match CI185's RW mapping, but never read or write its registers.
        d2xq_console_text ("MR ");
        uintptr_t ci186Rw = mapPhys (0x0c600000ULL + 0x10000ULL * ci186Apid, 0x1000, ci186RwMap);
        snprintf (ci186Marker, sizeof (ci186Marker), "mR=%llx ", (uint64_t) ci186Rw);
        d2xq_console_text (ci186Marker);
        if (!ci186Rw) panic ("CI186 READONLY FAIL RW map apid=%u", ci186Apid);

        // Read each watchdog register once. No disable/config/enable/kick.
        d2xq_console_text ("WE ");
        uint32_t ci186WdtEn = read32 (ci186Wdt, 0x08);
        snprintf (ci186Marker, sizeof (ci186Marker), "we=%08x ", ci186WdtEn);
        d2xq_console_text (ci186Marker);
        d2xq_console_text ("WA ");
        uint32_t ci186WdtBark = read32 (ci186Wdt, 0x10);
        snprintf (ci186Marker, sizeof (ci186Marker), "wa=%08x ", ci186WdtBark);
        d2xq_console_text (ci186Marker);
        d2xq_console_text ("WI ");
        uint32_t ci186WdtBite = read32 (ci186Wdt, 0x14);
        snprintf (ci186Marker, sizeof (ci186Marker), "wi=%08x ", ci186WdtBite);
        d2xq_console_text (ci186Marker);

        auto ci186Read = [&] (unsigned ci186Address, char ci186Tag) -> uint8_t {
          if ((ci186Address >> 8) != 0x08U)
            panic ("CI186 READONLY FAIL PON address=%x", ci186Address);
          // Only permitted hardware control write: OBS one-byte EXT_READL.
          // No RW WDATA/CMD, PON write, STATUS clear or CONFIG write.
          snprintf (ci186Marker, sizeof (ci186Marker), "%cC0 ", ci186Tag);
          d2xq_console_text (ci186Marker);
          write32 (ci186Obs, 0, 0x08000000U | ((ci186Address & 0xffU) << 4));
          snprintf (ci186Marker, sizeof (ci186Marker), "%cC1 %cB0 ", ci186Tag, ci186Tag);
          d2xq_console_text (ci186Marker);
          asm volatile ("dsb sy" ::: "memory");
          snprintf (ci186Marker, sizeof (ci186Marker), "%cB1 ", ci186Tag);
          d2xq_console_text (ci186Marker);
          uint32_t ci186Status = 0;
          bool ci186FirstDelay = true;
          for (unsigned ci186Poll = 0; ci186Poll < 1000; ci186Poll++) {
            if (ci186Poll == 0) {
              snprintf (ci186Marker, sizeof (ci186Marker), "%cT0 ", ci186Tag);
              d2xq_console_text (ci186Marker);
            }
            ci186Status = read32 (ci186Obs, 0x08);
            if (ci186Poll == 0) {
              snprintf (ci186Marker, sizeof (ci186Marker), "%cT1=%08x ", ci186Tag, ci186Status);
              d2xq_console_text (ci186Marker);
            }
            if ((ci186Status & 1U) != 0) {
              if ((ci186Status & 0x0eU) != 0)
                panic ("CI186 READONLY FAIL %c status=%x apid=%u ver=%08x",
                       ci186Tag, ci186Status, ci186Apid, ci186Version);
              snprintf (ci186Marker, sizeof (ci186Marker), "%cB2 ", ci186Tag);
              d2xq_console_text (ci186Marker);
              asm volatile ("dsb sy" ::: "memory");
              snprintf (ci186Marker, sizeof (ci186Marker), "%cB3 %cA0 ", ci186Tag, ci186Tag);
              d2xq_console_text (ci186Marker);
              uint32_t ci186Data = read32 (ci186Obs, 0x18);
              snprintf (ci186Marker, sizeof (ci186Marker), "%cA1=%08x ", ci186Tag, ci186Data);
              d2xq_console_text (ci186Marker);
              return (uint8_t) (ci186Data & 0xffU);
            }
            if (ci186FirstDelay) {
              snprintf (ci186Marker, sizeof (ci186Marker), "%cD0 ", ci186Tag);
              d2xq_console_text (ci186Marker);
            }
            IODelay (1);
            if (ci186FirstDelay) {
              snprintf (ci186Marker, sizeof (ci186Marker), "%cD1 ", ci186Tag);
              d2xq_console_text (ci186Marker);
              ci186FirstDelay = false;
            }
          }
          panic ("CI186 READONLY FAIL %c timeout status=%x apid=%u ver=%08x",
                 ci186Tag, ci186Status, ci186Apid, ci186Version);
        };

        uint8_t ci186Subtype = ci186Read (0x0805U, 'S');
        if (ci186Subtype != 0x04U && ci186Subtype != 0x05U)
          panic ("CI186 READONLY FAIL subtype=%02x apid=%u ver=%08x",
                 (unsigned) ci186Subtype, ci186Apid, ci186Version);
        uint8_t ci186OldReason = ci186Read (0x088fU, 'R');
        // Screen output can let READL finish before its first status poll.
        // Exercise IODelay once even when neither transaction needs to wait.
        d2xq_console_text ("QD0 ");
        IODelay (1);
        d2xq_console_text ("QD1 ");
        // An unconditional terminal panic prevents all later metadata,
        // data/PMIC/watchdog writes and the unchanged CI185 success hook.
        panic ("CI186 READONLY PASS ver=%08x apid=%u subtype=%02x oldreason=%02x counter=%llu next=%u",
               ci186Version, ci186Apid, (unsigned) ci186Subtype,
               (unsigned) ci186OldReason, counter, nextRepairSlot);
      }
