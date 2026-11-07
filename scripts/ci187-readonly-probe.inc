      {
        // Diagnostic-only entry, after the complete trusted-counter checks.
        // Never reach metadata processing, ingest, the CI185 reset or a write.
        d2xq_console_text ("CI187 P0 ");
        if (!repairCounter || counter != 7 || nextRepairSlot != 2)
          panic ("CI187 READONLY REFUSE repair=%u counter=%llu next=%u",
                 (unsigned) repairCounter, counter, nextRepairSlot);
        // Only RAM diagnostic state: keep the startup watchdog from masking
        // the exact MMIO operation during deliberately verbose screen output.
        d2xq_ufs_stage = -1;


        // Raw architectural counters only: no XNU time framework or scheduler.
        d2xq_console_text ("CI187 F0 ");
        uint64_t ci187Frequency = 0;
        asm volatile ("mrs %0, cntfrq_el0" : "=r" (ci187Frequency) :: "memory");
        uint64_t ci187DelayTicks = (ci187Frequency + 999999ULL) / 1000000ULL + 1ULL;
        char ci187FrequencyMarker[96];
        snprintf (ci187FrequencyMarker, sizeof (ci187FrequencyMarker),
                  "CI187 F1 freq=%llu ticks=%llu ", ci187Frequency, ci187DelayTicks);
        d2xq_console_text (ci187FrequencyMarker);
        if (ci187Frequency != 19200000ULL)
          panic ("CI187 READONLY FAIL frequency=%llu expected=19200000", ci187Frequency);
        auto ci187Vct = [] () -> uint64_t {
          uint64_t ci187Value = 0;
          asm volatile ("isb\n\tmrs %0, cntvct_el0" : "=r" (ci187Value) :: "memory");
          return ci187Value;
        };
        auto ci187Delay = [&] (char ci187Tag) {
          uint64_t ci187Start = ci187Vct ();
          uint64_t ci187Last = ci187Start;
          bool ci187Progress = false;
          for (unsigned ci187Round = 0; ci187Round < 4096; ci187Round++) {
            asm volatile ("nop" ::: "memory");
            uint64_t ci187Now = ci187Vct ();
            if (ci187Now < ci187Last)
              panic ("CI187 READONLY FAIL delay %c backward start=%llu last=%llu now=%llu round=%u",
                     ci187Tag, ci187Start, ci187Last, ci187Now, ci187Round);
            if (ci187Now > ci187Last) ci187Progress = true;
            // Monotonic checks above make this subtraction safe, including
            // refusal of a counter wrap. Never construct start+ticks.
            if (ci187Now - ci187Start >= ci187DelayTicks) return;
            ci187Last = ci187Now;
          }
          if (!ci187Progress)
            panic ("CI187 READONLY FAIL delay %c stalled start=%llu last=%llu rounds=4096",
                   ci187Tag, ci187Start, ci187Last);
          panic ("CI187 READONLY FAIL delay %c exhausted start=%llu last=%llu ticks=%llu rounds=4096",
                 ci187Tag, ci187Start, ci187Last, ci187DelayTicks);
        };

        char ci187Marker[160];
        OSSharedPtr<IOMemoryMap> ci187CoreMap, ci187CnfgMap, ci187WdtMap;
        OSSharedPtr<IOMemoryMap> ci187ObsMap, ci187RwMap;
        d2xq_console_text ("MC ");
        uintptr_t ci187Core = mapPhys (0x0c440000ULL, 0x1100, ci187CoreMap);
        snprintf (ci187Marker, sizeof (ci187Marker), "mC=%llx ", (uint64_t) ci187Core);
        d2xq_console_text (ci187Marker);
        if (!ci187Core) panic ("CI187 READONLY FAIL core map");
        d2xq_console_text ("MN ");
        uintptr_t ci187Cnfg = mapPhys (0x0c40a000ULL, 0x1000, ci187CnfgMap);
        snprintf (ci187Marker, sizeof (ci187Marker), "mN=%llx ", (uint64_t) ci187Cnfg);
        d2xq_console_text (ci187Marker);
        if (!ci187Cnfg) panic ("CI187 READONLY FAIL cnfg map");
        d2xq_console_text ("MW ");
        uintptr_t ci187Wdt = mapPhys (0x17c10000ULL, 0x1000, ci187WdtMap);
        snprintf (ci187Marker, sizeof (ci187Marker), "mW=%llx ", (uint64_t) ci187Wdt);
        d2xq_console_text (ci187Marker);
        if (!ci187Wdt) panic ("CI187 READONLY FAIL WDT map");

        d2xq_console_text ("V ");
        uint32_t ci187Version = read32 (ci187Core, 0);
        snprintf (ci187Marker, sizeof (ci187Marker), "v=%08x ", ci187Version);
        d2xq_console_text (ci187Marker);
        if (ci187Version < 0x50000000U || ci187Version >= 0x60000000U)
          panic ("CI187 READONLY FAIL version=%08x", ci187Version);

        const unsigned ci187Ee = 0;
        unsigned ci187Apid = 512, ci187Matches = 0, ci187OwnedMatches = 0;
        uint32_t ci187Entry = 0;
        for (unsigned ci187Index = 0; ci187Index < 512; ci187Index++) {
          snprintf (ci187Marker, sizeof (ci187Marker), "E%u ", ci187Index);
          d2xq_console_text (ci187Marker);
          uint32_t ci187Candidate = read32 (ci187Core, 0x900 + 4 * ci187Index);
          d2xq_console_text ("e ");
          if (!ci187Candidate || ((ci187Candidate >> 8) & 0xfffU) != 0x008U)
            continue;
          ci187Matches++;
          snprintf (ci187Marker, sizeof (ci187Marker), "O%u ", ci187Index);
          d2xq_console_text (ci187Marker);
          unsigned ci187Owner = read32 (ci187Cnfg, 0x700 + 4 * ci187Index) & 7U;
          snprintf (ci187Marker, sizeof (ci187Marker), "o%u ", ci187Owner);
          d2xq_console_text (ci187Marker);
          if (ci187Owner == ci187Ee) {
            ci187Apid = ci187Index;
            ci187Entry = ci187Candidate;
            ci187OwnedMatches++;
          }
        }
        if (ci187Apid >= 512)
          panic ("CI187 READONLY FAIL no PON owner ee=%u matches=%u ver=%08x",
                 ci187Ee, ci187Matches, ci187Version);
        snprintf (ci187Marker, sizeof (ci187Marker),
                  "APID=%u entry=%08x matches=%u owned=%u ",
                  ci187Apid, ci187Entry, ci187Matches, ci187OwnedMatches);
        d2xq_console_text (ci187Marker);

        uint64_t ci187ObsPhys = 0x0e600000ULL + 0x10000ULL * ci187Ee + 0x80ULL * ci187Apid;
        d2xq_console_text ("MO ");
        uintptr_t ci187ObsPage = mapPhys (ci187ObsPhys & ~0xfffULL, 0x1000, ci187ObsMap);
        snprintf (ci187Marker, sizeof (ci187Marker), "mO=%llx ", (uint64_t) ci187ObsPage);
        d2xq_console_text (ci187Marker);
        if (!ci187ObsPage) panic ("CI187 READONLY FAIL OBS map apid=%u", ci187Apid);
        uintptr_t ci187Obs = ci187ObsPage + (uintptr_t) (ci187ObsPhys & 0xfffULL);
        // Match CI185's RW mapping, but never read or write its registers.
        d2xq_console_text ("MR ");
        uintptr_t ci187Rw = mapPhys (0x0c600000ULL + 0x10000ULL * ci187Apid, 0x1000, ci187RwMap);
        snprintf (ci187Marker, sizeof (ci187Marker), "mR=%llx ", (uint64_t) ci187Rw);
        d2xq_console_text (ci187Marker);
        if (!ci187Rw) panic ("CI187 READONLY FAIL RW map apid=%u", ci187Apid);

        // Read each watchdog register once. No disable/config/enable/kick.
        d2xq_console_text ("WE ");
        uint32_t ci187WdtEn = read32 (ci187Wdt, 0x08);
        snprintf (ci187Marker, sizeof (ci187Marker), "we=%08x ", ci187WdtEn);
        d2xq_console_text (ci187Marker);
        d2xq_console_text ("WA ");
        uint32_t ci187WdtBark = read32 (ci187Wdt, 0x10);
        snprintf (ci187Marker, sizeof (ci187Marker), "wa=%08x ", ci187WdtBark);
        d2xq_console_text (ci187Marker);
        d2xq_console_text ("WI ");
        uint32_t ci187WdtBite = read32 (ci187Wdt, 0x14);
        snprintf (ci187Marker, sizeof (ci187Marker), "wi=%08x ", ci187WdtBite);
        d2xq_console_text (ci187Marker);

        auto ci187Read = [&] (unsigned ci187Address, char ci187Tag) -> uint8_t {
          if ((ci187Address >> 8) != 0x08U)
            panic ("CI187 READONLY FAIL PON address=%x", ci187Address);
          // Only permitted hardware control write: OBS one-byte EXT_READL.
          // No RW WDATA/CMD, PON write, STATUS clear or CONFIG write.
          snprintf (ci187Marker, sizeof (ci187Marker), "%cC0 ", ci187Tag);
          d2xq_console_text (ci187Marker);
          write32 (ci187Obs, 0, 0x08000000U | ((ci187Address & 0xffU) << 4));
          snprintf (ci187Marker, sizeof (ci187Marker), "%cC1 %cB0 ", ci187Tag, ci187Tag);
          d2xq_console_text (ci187Marker);
          asm volatile ("dsb sy" ::: "memory");
          snprintf (ci187Marker, sizeof (ci187Marker), "%cB1 ", ci187Tag);
          d2xq_console_text (ci187Marker);
          uint32_t ci187Status = 0;
          bool ci187FirstDelay = true;
          for (unsigned ci187Poll = 0; ci187Poll < 1000; ci187Poll++) {
            if (ci187Poll == 0) {
              snprintf (ci187Marker, sizeof (ci187Marker), "%cT0 ", ci187Tag);
              d2xq_console_text (ci187Marker);
            }
            ci187Status = read32 (ci187Obs, 0x08);
            if (ci187Poll == 0) {
              snprintf (ci187Marker, sizeof (ci187Marker), "%cT1=%08x ", ci187Tag, ci187Status);
              d2xq_console_text (ci187Marker);
            }
            if ((ci187Status & 1U) != 0) {
              if ((ci187Status & 0x0eU) != 0)
                panic ("CI187 READONLY FAIL %c status=%x apid=%u ver=%08x",
                       ci187Tag, ci187Status, ci187Apid, ci187Version);
              snprintf (ci187Marker, sizeof (ci187Marker), "%cB2 ", ci187Tag);
              d2xq_console_text (ci187Marker);
              asm volatile ("dsb sy" ::: "memory");
              snprintf (ci187Marker, sizeof (ci187Marker), "%cB3 %cA0 ", ci187Tag, ci187Tag);
              d2xq_console_text (ci187Marker);
              uint32_t ci187Data = read32 (ci187Obs, 0x18);
              snprintf (ci187Marker, sizeof (ci187Marker), "%cA1=%08x ", ci187Tag, ci187Data);
              d2xq_console_text (ci187Marker);
              return (uint8_t) (ci187Data & 0xffU);
            }
            if (ci187FirstDelay) {
              snprintf (ci187Marker, sizeof (ci187Marker), "%cD0 ", ci187Tag);
              d2xq_console_text (ci187Marker);
            }
            ci187Delay (ci187Tag);
            if (ci187FirstDelay) {
              snprintf (ci187Marker, sizeof (ci187Marker), "%cD1 ", ci187Tag);
              d2xq_console_text (ci187Marker);
              ci187FirstDelay = false;
            }
          }
          panic ("CI187 READONLY FAIL %c timeout status=%x apid=%u ver=%08x",
                 ci187Tag, ci187Status, ci187Apid, ci187Version);
        };

        uint8_t ci187Subtype = ci187Read (0x0805U, 'S');
        if (ci187Subtype != 0x04U && ci187Subtype != 0x05U)
          panic ("CI187 READONLY FAIL subtype=%02x apid=%u ver=%08x",
                 (unsigned) ci187Subtype, ci187Apid, ci187Version);
        uint8_t ci187OldReason = ci187Read (0x088fU, 'R');
        // Exercise one raw-counter delay even when both READLs are already
        // DONE at their first poll. This is the operation CI186 could not exit.
        d2xq_console_text ("CI187 QD0 ");
        ci187Delay ('Q');
        d2xq_console_text ("CI187 QD1 ");
        // An unconditional terminal panic prevents all later metadata,
        // data/PMIC/watchdog writes and the unchanged CI185 success hook.
        panic ("CI187 READONLY PASS ver=%08x apid=%u subtype=%02x oldreason=%02x counter=%llu next=%u",
               ci187Version, ci187Apid, (unsigned) ci187Subtype,
               (unsigned) ci187OldReason, counter, nextRepairSlot);
      }
