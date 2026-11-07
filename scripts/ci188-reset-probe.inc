      {
        // Reset-only entry after the existing complete trusted-counter checks.
        // No metadata read, ingest, disk write or progress update is reachable.
        d2xq_console_text ("CI188 P0 ");
        if (!repairCounter || counter != 7 || nextRepairSlot != 2)
          panic ("CI188 RESET REFUSE repair=%u counter=%llu next=%u",
                 (unsigned) repairCounter, counter, nextRepairSlot);
        d2xq_ufs_stage = -1; // RAM diagnostic state only.

        // Raw architectural counters only: no XNU time framework or scheduler.
        d2xq_console_text ("CI188 F0 ");
        uint64_t ci188Frequency = 0;
        asm volatile ("mrs %0, cntfrq_el0" : "=r" (ci188Frequency) :: "memory");
        uint64_t ci188DelayTicks = (ci188Frequency + 999999ULL) / 1000000ULL + 1ULL;
        char ci188FrequencyMarker[96];
        snprintf (ci188FrequencyMarker, sizeof (ci188FrequencyMarker),
                  "CI188 F1 freq=%llu ticks=%llu ", ci188Frequency, ci188DelayTicks);
        d2xq_console_text (ci188FrequencyMarker);
        if (ci188Frequency != 19200000ULL)
          panic ("CI188 RESET FAIL frequency=%llu expected=19200000", ci188Frequency);
        auto ci188Vct = [] () -> uint64_t {
          uint64_t ci188Value = 0;
          asm volatile ("isb\n\tmrs %0, cntvct_el0" : "=r" (ci188Value) :: "memory");
          return ci188Value;
        };
        auto ci188Delay = [&] (char ci188Tag) {
          uint64_t ci188Start = ci188Vct ();
          uint64_t ci188Last = ci188Start;
          bool ci188Progress = false;
          for (unsigned ci188Round = 0; ci188Round < 4096; ci188Round++) {
            asm volatile ("nop" ::: "memory");
            uint64_t ci188Now = ci188Vct ();
            if (ci188Now < ci188Last)
              panic ("CI188 RESET FAIL delay %c backward start=%llu last=%llu now=%llu round=%u",
                     ci188Tag, ci188Start, ci188Last, ci188Now, ci188Round);
            if (ci188Now > ci188Last) ci188Progress = true;
            // Monotonic checks above make this subtraction safe, including
            // refusal of a counter wrap. Never construct start+ticks.
            if (ci188Now - ci188Start >= ci188DelayTicks) return;
            ci188Last = ci188Now;
          }
          if (!ci188Progress)
            panic ("CI188 RESET FAIL delay %c stalled start=%llu last=%llu rounds=4096",
                   ci188Tag, ci188Start, ci188Last);
          panic ("CI188 RESET FAIL delay %c exhausted start=%llu last=%llu ticks=%llu rounds=4096",
                 ci188Tag, ci188Start, ci188Last, ci188DelayTicks);
        };

        // Verify the real-device-tested busy wait before any WDT/PON write.
        d2xq_console_text ("CI188 Q0 ");
        ci188Delay ('Q');
        d2xq_console_text ("CI188 Q1 ");

        OSSharedPtr<IOMemoryMap> ci188CoreMap, ci188CnfgMap, ci188WdtMap;
        OSSharedPtr<IOMemoryMap> ci188ObsMap, ci188RwMap;
        uintptr_t ci188Core = mapPhys (0x0c440000ULL, 0x1100, ci188CoreMap);
        uintptr_t ci188Cnfg = mapPhys (0x0c40a000ULL, 0x1000, ci188CnfgMap);
        uintptr_t ci188Wdt = mapPhys (0x17c10000ULL, 0x1000, ci188WdtMap);
        if (!ci188Core || !ci188Cnfg || !ci188Wdt)
          panic ("CI188 RESET FAIL map core=%u cnfg=%u wdt=%u",
                 (unsigned) !!ci188Core, (unsigned) !!ci188Cnfg, (unsigned) !!ci188Wdt);
        uint32_t ci188Version = read32 (ci188Core, 0);
        if (ci188Version < 0x50000000U || ci188Version >= 0x60000000U)
          panic ("CI188 RESET FAIL SPMI version=%08x", ci188Version);
        const unsigned ci188Ee = 0;
        unsigned ci188Apid = 512, ci188Matches = 0, ci188OwnedMatches = 0;
        uint32_t ci188Entry = 0;
        // Linux v5 permits duplicate PPIDs: select the last owner-EE0 APID.
        for (unsigned ci188Index = 0; ci188Index < 512; ci188Index++) {
          uint32_t ci188Candidate = read32 (ci188Core, 0x900 + 4 * ci188Index);
          if (!ci188Candidate || ((ci188Candidate >> 8) & 0xfffU) != 0x008U)
            continue;
          ci188Matches++;
          unsigned ci188Owner = read32 (ci188Cnfg, 0x700 + 4 * ci188Index) & 7U;
          if (ci188Owner == ci188Ee) {
            ci188Apid = ci188Index;
            ci188Entry = ci188Candidate;
            ci188OwnedMatches++;
          }
        }
        if (ci188Apid >= 512)
          panic ("CI188 RESET FAIL no PON owner ee=%u matches=%u ver=%08x",
                 ci188Ee, ci188Matches, ci188Version);
        uint64_t ci188ObsPhys = 0x0e600000ULL + 0x10000ULL * ci188Ee + 0x80ULL * ci188Apid;
        uintptr_t ci188ObsPage = mapPhys (ci188ObsPhys & ~0xfffULL, 0x1000, ci188ObsMap);
        uintptr_t ci188Rw = mapPhys (0x0c600000ULL + 0x10000ULL * ci188Apid, 0x1000, ci188RwMap);
        if (!ci188ObsPage || !ci188Rw)
          panic ("CI188 RESET FAIL channel map apid=%u obs=%u rw=%u", ci188Apid,
                 (unsigned) !!ci188ObsPage, (unsigned) !!ci188Rw);
        uintptr_t ci188Obs = ci188ObsPage + (uintptr_t) (ci188ObsPhys & 0xfffULL);

        auto ci188Wait = [&] (uintptr_t ci188Channel, char ci188Tag) {
          uint32_t ci188Status = 0;
          for (unsigned ci188Poll = 0; ci188Poll < 1000; ci188Poll++) {
            ci188Status = read32 (ci188Channel, 0x08);
            if ((ci188Status & 1U) != 0) {
              if ((ci188Status & 0x0eU) != 0)
                panic ("CI188 RESET FAIL SPMI %c status=%x apid=%u ver=%08x",
                       ci188Tag, ci188Status, ci188Apid, ci188Version);
              asm volatile ("dsb sy" ::: "memory");
              return;
            }
            ci188Delay (ci188Tag);
          }
          panic ("CI188 RESET FAIL SPMI %c timeout status=%x apid=%u ver=%08x",
                 ci188Tag, ci188Status, ci188Apid, ci188Version);
        };
        auto ci188Read = [&] (unsigned ci188Address, char ci188Tag) -> uint8_t {
          if ((ci188Address >> 8) != 0x08U)
            panic ("CI188 RESET FAIL PON address=%x", ci188Address);
          write32 (ci188Obs, 0, 0x08000000U | ((ci188Address & 0xffU) << 4));
          asm volatile ("dsb sy" ::: "memory");
          ci188Wait (ci188Obs, ci188Tag);
          return (uint8_t) (read32 (ci188Obs, 0x18) & 0xffU);
        };
        uint8_t ci188Subtype = ci188Read (0x0805U, 'S');
        if (ci188Subtype != 0x04U && ci188Subtype != 0x05U)
          panic ("CI188 RESET FAIL subtype=%02x apid=%u ver=%08x",
                 (unsigned) ci188Subtype, ci188Apid, ci188Version);
        uint8_t ci188Old = ci188Read (0x088fU, 'R');
        if ((ci188Old & 0xfeU) == 0x2aU)
          panic ("CI188 RESET REFUSE already Download old=%02x", (unsigned) ci188Old);
        uint8_t ci188New = (uint8_t) ((ci188Old & 1U) | 0x2aU);

        // Same checked APSS WDT setup as CI185, after the Q self-test.
        // RST=04 EN=08 STS=0c BARK=10 BITE=14, WDT_HZ=32765.
        d2xq_console_text ("CI188 W0 ");
        write32 (ci188Wdt, 0x08, 0);
        asm volatile ("dsb sy" ::: "memory");
        write32 (ci188Wdt, 0x10, 5U * 32765U);
        write32 (ci188Wdt, 0x14, 32765U);
        asm volatile ("dsb sy" ::: "memory");
        if ((read32 (ci188Wdt, 0x08) & 1U) != 0 ||
            read32 (ci188Wdt, 0x10) != 5U * 32765U ||
            read32 (ci188Wdt, 0x14) != 32765U)
          panic ("CI188 RESET FAIL WDT config en=%x bark=%x bite=%x",
                 read32 (ci188Wdt, 0x08), read32 (ci188Wdt, 0x10), read32 (ci188Wdt, 0x14));
        d2xq_console_text ("CI188 W1 ");
        // Refuse any selected entry/owner/version change before the PON write.
        if (read32 (ci188Core, 0) != ci188Version ||
            read32 (ci188Core, 0x900 + 4 * ci188Apid) != ci188Entry ||
            (read32 (ci188Cnfg, 0x700 + 4 * ci188Apid) & 7U) != ci188Ee)
          panic ("CI188 RESET FAIL mapping changed apid=%u ver=%08x", ci188Apid, ci188Version);
        char ci188Marker[192];
        snprintf (ci188Marker, sizeof (ci188Marker),
                  "CI188 P1 apid=%u ver=%08x subtype=%02x old=%02x new=%02x matches=%u owned=%u ",
                  ci188Apid, ci188Version, (unsigned) ci188Subtype,
                  (unsigned) ci188Old, (unsigned) ci188New, ci188Matches, ci188OwnedMatches);
        d2xq_console_text (ci188Marker);
        write32 (ci188Rw, 0x10, ci188New);
        asm volatile ("dsb sy" ::: "memory");
        write32 (ci188Rw, 0, 0x000008f0U);
        asm volatile ("dsb sy" ::: "memory");
        ci188Wait (ci188Rw, 'W');
        uint8_t ci188Actual = ci188Read (0x088fU, 'V');
        if (ci188Actual != ci188New)
          panic ("CI188 RESET FAIL PON verify old=%02x new=%02x got=%02x apid=%u ver=%08x",
                 (unsigned) ci188Old, (unsigned) ci188New, (unsigned) ci188Actual,
                 ci188Apid, ci188Version);
        snprintf (ci188Marker, sizeof (ci188Marker),
                  "CI188 P2 old=%02x new=%02x got=%02x apid=%u ver=%08x ",
                  (unsigned) ci188Old, (unsigned) ci188New, (unsigned) ci188Actual,
                  ci188Apid, ci188Version);
        d2xq_console_text (ci188Marker);
        d2xq_console_text ("CI188 R0 ");
        write32 (ci188Wdt, 0x08, 1);
        write32 (ci188Wdt, 0x04, 1);
        asm volatile ("dsb sy" ::: "memory");
        d2xq_console_text ("CI188 R1 ");

        // Actual ten-second counter window, not one 4096-round microdelay.
        // Each bounded delay detects a frozen counter; cross-iteration checks
        // also reject a backwards step between separate delay invocations.
        uint64_t ci188ResetStart = ci188Vct ();
        uint64_t ci188ResetLast = ci188ResetStart;
        const uint64_t ci188ResetTicks = ci188Frequency * 10ULL;
        for (unsigned ci188Attempt = 0; ci188Attempt < 10000000U; ci188Attempt++) {
          ci188Delay ('T');
          uint64_t ci188ResetNow = ci188Vct ();
          if (ci188ResetNow < ci188ResetLast)
            panic ("CI188 RESET FAIL wait backward start=%llu last=%llu now=%llu attempt=%u",
                   ci188ResetStart, ci188ResetLast, ci188ResetNow, ci188Attempt);
          if (ci188ResetNow - ci188ResetStart >= ci188ResetTicks)
            panic ("CI188 RESET FAIL WDT no reset after 10s ticks=%llu sts=%x en=%x bark=%x bite=%x",
                   ci188ResetNow - ci188ResetStart, read32 (ci188Wdt, 0x0c),
                   read32 (ci188Wdt, 0x08), read32 (ci188Wdt, 0x10), read32 (ci188Wdt, 0x14));
          ci188ResetLast = ci188ResetNow;
        }
        panic ("CI188 RESET FAIL wait exhausted start=%llu last=%llu ticks=%llu attempts=10000000",
               ci188ResetStart, ci188ResetLast, ci188ResetTicks);
      }
