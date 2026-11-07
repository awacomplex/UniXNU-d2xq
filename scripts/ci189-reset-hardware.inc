      {
        // Only the trusted-counter + durably consumed RECOVERY-arm path enters.
        if (!ci189ArmConsumed || !repairCounter || counter != 7 || nextRepairSlot != 2)
          panic ("CI189 RESET REFUSE hardware gate");
        using ci189Signed32 = int;
        static_assert (sizeof (ci189Signed32) == 4, "SCM status requires a signed 32-bit int");
        d2xq_ufs_stage = -1;

        uint64_t ci189Frequency = 0, ci189El = 0;
        asm volatile ("mrs %0, cntfrq_el0" : "=r" (ci189Frequency) :: "memory");
        asm volatile ("mrs %0, CurrentEL" : "=r" (ci189El) :: "memory");
        char ci189Marker[224];
        snprintf (ci189Marker, sizeof (ci189Marker),
                  "CI189 F1 freq=%llu el=%llu ", ci189Frequency, ci189El);
        d2xq_console_text (ci189Marker);
        if (ci189Frequency != 19200000ULL || ci189El != 4ULL)
          panic ("CI189 RESET REFUSE freq=%llu el=%llu", ci189Frequency, ci189El);
        const uint64_t ci189DelayTicks = (ci189Frequency + 999999ULL) / 1000000ULL + 1ULL;
        uint64_t ci189ClockLast = 0;
        auto ci189Vct = [&] () -> uint64_t {
          uint64_t ci189Value = 0;
          asm volatile ("isb\n\tmrs %0, cntvct_el0" : "=r" (ci189Value) :: "memory");
          if (ci189Value < ci189ClockLast)
            panic ("CI189 RESET FAIL counter backward last=%llu now=%llu", ci189ClockLast, ci189Value);
          ci189ClockLast = ci189Value;
          return ci189Value;
        };
        auto ci189Delay = [&] (char ci189Tag) {
          uint64_t ci189Start = ci189Vct (), ci189Last = ci189Start;
          bool ci189Progress = false;
          for (unsigned ci189Round = 0; ci189Round < 4096; ci189Round++) {
            asm volatile ("nop" ::: "memory");
            uint64_t ci189Now = ci189Vct ();
            if (ci189Now < ci189Last)
              panic ("CI189 RESET FAIL delay %c backward start=%llu last=%llu now=%llu",
                     ci189Tag, ci189Start, ci189Last, ci189Now);
            if (ci189Now > ci189Last) ci189Progress = true;
            if (ci189Now - ci189Start >= ci189DelayTicks) return;
            ci189Last = ci189Now;
          }
          panic ("CI189 RESET FAIL delay %c %s start=%llu last=%llu",
                 ci189Tag, ci189Progress ? "exhausted" : "stalled", ci189Start, ci189Last);
        };
        d2xq_console_text ("CI189 Q0 ");
        ci189Delay ('Q');
        d2xq_console_text ("CI189 Q1 ");

        OSSharedPtr<IOMemoryMap> ci189CoreMap, ci189CnfgMap, ci189ObsMap, ci189RwMap;
        OSSharedPtr<IOMemoryMap> ci189ImemMap, ci189PsHoldMap;
        uintptr_t ci189Core = mapPhys (0x0c440000ULL, 0x1100, ci189CoreMap);
        uintptr_t ci189Cnfg = mapPhys (0x0c40a000ULL, 0x1000, ci189CnfgMap);
        uintptr_t ci189Imem = mapPhys (0x146bf000ULL, 0x1000, ci189ImemMap);
        uintptr_t ci189PsHold = mapPhys (0x0c264000ULL, 0x1000, ci189PsHoldMap);
        if (!ci189Core || !ci189Cnfg || !ci189Imem || !ci189PsHold)
          panic ("CI189 RESET FAIL map core=%u cnfg=%u imem=%u pshold=%u",
                 (unsigned) !!ci189Core, (unsigned) !!ci189Cnfg,
                 (unsigned) !!ci189Imem, (unsigned) !!ci189PsHold);
        uint32_t ci189Version = read32 (ci189Core, 0);
        if (ci189Version < 0x50000000U || ci189Version >= 0x60000000U)
          panic ("CI189 RESET FAIL SPMI version=%08x", ci189Version);
        const unsigned ci189Ee = 0;
        unsigned ci189Apid = 512, ci189Matches = 0, ci189OwnedMatches = 0;
        uint32_t ci189Entry = 0;
        for (unsigned ci189Index = 0; ci189Index < 512; ci189Index++) {
          uint32_t ci189Candidate = read32 (ci189Core, 0x900 + 4 * ci189Index);
          if (!ci189Candidate || ((ci189Candidate >> 8) & 0xfffU) != 0x008U) continue;
          ci189Matches++;
          if ((read32 (ci189Cnfg, 0x700 + 4 * ci189Index) & 7U) == ci189Ee) {
            ci189Apid = ci189Index;
            ci189Entry = ci189Candidate;
            ci189OwnedMatches++;
          }
        }
        if (ci189Apid >= 512)
          panic ("CI189 RESET FAIL no PON owner matches=%u ver=%08x", ci189Matches, ci189Version);
        uint64_t ci189ObsPhys = 0x0e600000ULL + 0x10000ULL * ci189Ee + 0x80ULL * ci189Apid;
        uintptr_t ci189ObsPage = mapPhys (ci189ObsPhys & ~0xfffULL, 0x1000, ci189ObsMap);
        uintptr_t ci189Rw = mapPhys (0x0c600000ULL + 0x10000ULL * ci189Apid, 0x1000, ci189RwMap);
        if (!ci189ObsPage || !ci189Rw)
          panic ("CI189 RESET FAIL channel map apid=%u", ci189Apid);
        uintptr_t ci189Obs = ci189ObsPage + (uintptr_t) (ci189ObsPhys & 0xfffULL);
        auto ci189CheckMapping = [&] () {
          if (read32 (ci189Core, 0) != ci189Version ||
              read32 (ci189Core, 0x900 + 4 * ci189Apid) != ci189Entry ||
              (read32 (ci189Cnfg, 0x700 + 4 * ci189Apid) & 7U) != ci189Ee)
            panic ("CI189 RESET FAIL mapping changed apid=%u ver=%08x", ci189Apid, ci189Version);
        };
        auto ci189Wait = [&] (uintptr_t ci189Channel, char ci189Tag) {
          uint32_t ci189Status = 0;
          for (unsigned ci189Poll = 0; ci189Poll < 1000; ci189Poll++) {
            ci189Status = read32 (ci189Channel, 0x08);
            if (ci189Status & 1U) {
              if (ci189Status & 0x0eU)
                panic ("CI189 RESET FAIL SPMI %c status=%x apid=%u", ci189Tag, ci189Status, ci189Apid);
              asm volatile ("dsb sy" ::: "memory");
              return;
            }
            ci189Delay (ci189Tag);
          }
          panic ("CI189 RESET FAIL SPMI %c timeout status=%x apid=%u", ci189Tag, ci189Status, ci189Apid);
        };
        auto ci189Read = [&] (unsigned ci189Address, char ci189Tag) -> uint8_t {
          if (ci189Address != 0x0805U && ci189Address != 0x085aU &&
              ci189Address != 0x085bU && ci189Address != 0x088fU)
            panic ("CI189 RESET FAIL read address=%x", ci189Address);
          ci189CheckMapping ();
          write32 (ci189Obs, 0, 0x08000000U | ((ci189Address & 0xffU) << 4));
          asm volatile ("dsb sy" ::: "memory");
          ci189Wait (ci189Obs, ci189Tag);
          return (uint8_t) (read32 (ci189Obs, 0x18) & 0xffU);
        };
        auto ci189Write = [&] (unsigned ci189Address, uint8_t ci189Value, char ci189Tag) {
          if (ci189Address != 0x085aU && ci189Address != 0x085bU && ci189Address != 0x088fU)
            panic ("CI189 RESET FAIL write address=%x", ci189Address);
          ci189CheckMapping ();
          write32 (ci189Rw, 0x10, ci189Value);
          asm volatile ("dsb sy" ::: "memory");
          write32 (ci189Rw, 0, (ci189Address & 0xffU) << 4);
          asm volatile ("dsb sy" ::: "memory");
          ci189Wait (ci189Rw, ci189Tag);
          uint8_t ci189Got = ci189Read (ci189Address, 'V');
          if (ci189Got != ci189Value)
            panic ("CI189 RESET FAIL byte address=%x want=%02x got=%02x",
                   ci189Address, (unsigned) ci189Value, (unsigned) ci189Got);
        };
        uint8_t ci189Subtype = ci189Read (0x0805U, 'S');
        if (ci189Subtype != 0x04U && ci189Subtype != 0x05U)
          panic ("CI189 RESET REFUSE subtype=%02x", (unsigned) ci189Subtype);
        snprintf (ci189Marker, sizeof (ci189Marker),
                  "CI189 P0 apid=%u ver=%08x subtype=%02x matches=%u owned=%u ",
                  ci189Apid, ci189Version, (unsigned) ci189Subtype, ci189Matches, ci189OwnedMatches);
        d2xq_console_text (ci189Marker);

        // Same-device scm.c: SMC32/64 carry state in registers 0..6 on INTERRUPTED.
        // Save them before any counter/log call; a C call may clobber fixed x0..6.
        auto ci189Smc = [&] (bool ci189Wide, uint32_t ci189Function, uint32_t ci189Arginfo,
                            uint64_t ci189Arg0, uint64_t ci189Arg1,
                            const char *ci189Tag, uint64_t *ci189Returns) -> ci189Signed32 {
          uint64_t ci189Regs[7] = { ci189Function, ci189Arginfo, ci189Arg0, ci189Arg1, 0, 0, 0 };
          uint64_t ci189Start = ci189Vct (), ci189Last = ci189Start;
          snprintf (ci189Marker, sizeof (ci189Marker), "CI189 S0 %s fn=%08x ", ci189Tag, ci189Function);
          d2xq_console_text (ci189Marker);
          for (unsigned ci189Call = 0; ci189Call < 128; ci189Call++) {
            {
              register uint64_t ci189R0 asm ("x0") = ci189Regs[0];
              register uint64_t ci189R1 asm ("x1") = ci189Regs[1];
              register uint64_t ci189R2 asm ("x2") = ci189Regs[2];
              register uint64_t ci189R3 asm ("x3") = ci189Regs[3];
              register uint64_t ci189R4 asm ("x4") = ci189Regs[4];
              register uint64_t ci189R5 asm ("x5") = ci189Regs[5];
              register uint64_t ci189R6 asm ("x6") = ci189Regs[6];
              asm volatile ("smc #0"
                            : "+r" (ci189R0), "+r" (ci189R1), "+r" (ci189R2), "+r" (ci189R3),
                              "+r" (ci189R4), "+r" (ci189R5), "+r" (ci189R6)
                            :
                            : "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                              "x16", "x17", "x19", "x20", "cc", "memory");
              ci189Regs[0] = ci189R0; ci189Regs[1] = ci189R1; ci189Regs[2] = ci189R2;
              ci189Regs[3] = ci189R3; ci189Regs[4] = ci189R4; ci189Regs[5] = ci189R5;
              ci189Regs[6] = ci189R6;
            }
            if (!ci189Wide)
              for (unsigned ci189Reg = 0; ci189Reg < 7; ci189Reg++) ci189Regs[ci189Reg] = (uint32_t) ci189Regs[ci189Reg];
            uint64_t ci189Now = ci189Vct ();
            if (ci189Now < ci189Last)
              panic ("CI189 RESET FAIL SCM %s counter backward", ci189Tag);
            if (ci189Now - ci189Start >= ci189Frequency * 10ULL)
              panic ("CI189 RESET FAIL SCM %s exceeded 10s calls=%u", ci189Tag, ci189Call + 1);
            ci189Last = ci189Now;
            ci189Signed32 ci189Status = (ci189Signed32) (uint32_t) ci189Regs[0];
            if (ci189Status == 1) continue;
            ci189Returns[0] = ci189Regs[1]; ci189Returns[1] = ci189Regs[2]; ci189Returns[2] = ci189Regs[3];
            snprintf (ci189Marker, sizeof (ci189Marker),
                      "CI189 S1 %s status=%d ret=%llx calls=%u ",
                      ci189Tag, (int) ci189Status, ci189Returns[0], ci189Call + 1);
            d2xq_console_text (ci189Marker);
            return ci189Status;
          }
          panic ("CI189 RESET FAIL SCM %s interrupted exhausted calls=128", ci189Tag);
        };
        uint64_t ci189Returns[3] = { 0, 0, 0 };
        bool ci189Wide = true;
        ci189Signed32 ci189AbiStatus = ci189Smc (true, 0xc2000601U, 1, 0x82000601U, 0, "ABI64", ci189Returns);
        if (ci189AbiStatus != 0 || ci189Returns[0] == 0) {
          ci189Wide = false;
          ci189AbiStatus = ci189Smc (false, 0x82000601U, 1, 0x82000601U, 0, "ABI32", ci189Returns);
          if (ci189AbiStatus != 0 || ci189Returns[0] == 0)
            panic ("CI189 RESET REFUSE SCM no ARMv8 ABI status=%d ret=%llx", (int) ci189AbiStatus, ci189Returns[0]);
        }
        const uint32_t ci189AbiMask = ci189Wide ? 0x40000000U : 0;
        snprintf (ci189Marker, sizeof (ci189Marker), "CI189 A2 abi=%u ", ci189Wide ? 64U : 32U);
        d2xq_console_text (ci189Marker);
        auto ci189Available = [&] (uint32_t ci189Function, const char *ci189Tag) -> ci189Signed32 {
          ci189Signed32 ci189Status = ci189Smc (ci189Wide, 0x02000601U | ci189AbiMask, 1,
                                       ci189Function, 0, ci189Tag, ci189Returns);
          if (ci189Status != 0)
            panic ("CI189 RESET REFUSE availability %s status=%d", ci189Tag, (int) ci189Status);
          ci189Signed32 ci189Value = (ci189Signed32) (uint32_t) ci189Returns[0];
          if (ci189Value < 0)
            panic ("CI189 RESET REFUSE availability %s value=%d", ci189Tag, (int) ci189Value);
          return ci189Value;
        };
        ci189Signed32 ci189DloadAvailable = ci189Available (0x02000110U, "AV-DLOAD");
        ci189Signed32 ci189SdiAvailable = ci189Available (0x02000109U, "AV-SDI");
        ci189Signed32 ci189ArbAvailable = ci189Available (0x02000901U, "AV-ARB");
        ci189Signed32 ci189PsAvailable = ci189Available (0x02000902U, "AV-PS");
        if (ci189DloadAvailable <= 0 || ci189SdiAvailable <= 0 || ci189ArbAvailable <= 0)
          panic ("CI189 RESET REFUSE SCM unavailable dload=%d sdi=%d arb=%d ps=%d",
                 (int) ci189DloadAvailable, (int) ci189SdiAvailable,
                 (int) ci189ArbAvailable, (int) ci189PsAvailable);
        auto ci189Call = [&] (uint32_t ci189Function, uint32_t ci189Arginfo,
                             uint64_t ci189Arg0, uint64_t ci189Arg1, const char *ci189Tag) {
          ci189Signed32 ci189Status = ci189Smc (ci189Wide, ci189Function | 0x80000000U | ci189AbiMask,
                                       ci189Arginfo, ci189Arg0, ci189Arg1, ci189Tag, ci189Returns);
          if (ci189Status != 0)
            panic ("CI189 RESET FAIL SCM %s status=%d ret=%llx", ci189Tag, (int) ci189Status, ci189Returns[0]);
        };
        auto ci189SetImem = [&] (uint32_t ci189Value) {
          write32 (ci189Imem, 0x65c, ci189Value);
          asm volatile ("dsb sy" ::: "memory");
          uint32_t ci189Got = read32 (ci189Imem, 0x65c);
          if (ci189Got != ci189Value)
            panic ("CI189 RESET FAIL IMEM want=%08x got=%08x", ci189Value, ci189Got);
        };

        // Normal Samsung reboot clears secure crash DLOAD; no unproven cookie.
        d2xq_console_text ("CI189 C0 ");
        ci189Call (0x02000110U, 2, 0, 0, "DLOAD0");
        ci189SetImem (0);
        d2xq_console_text ("CI189 C1 ");
        uint8_t ci189OldCtl2 = ci189Read (0x085bU, 'E');
        d2xq_console_text ("CI189 H0 ");
        ci189Write (0x085bU, (uint8_t) (ci189OldCtl2 & 0x7fU), 'E');
        uint64_t ci189ResetConfigLast = ci189Vct ();
        for (unsigned ci189Us = 0; ci189Us < 500; ci189Us++) {
          ci189Delay ('H');
          uint64_t ci189Now = ci189Vct ();
          if (ci189Now < ci189ResetConfigLast)
            panic ("CI189 RESET FAIL hard-reset delay backward");
          ci189ResetConfigLast = ci189Now;
        }
        uint8_t ci189OldCtl = ci189Read (0x085aU, 'H');
        uint8_t ci189HardCtl = (uint8_t) ((ci189OldCtl & 0xf0U) | 0x07U);
        ci189Write (0x085aU, ci189HardCtl, 'H');
        uint8_t ci189DisabledCtl2 = ci189Read (0x085bU, 'E');
        if (ci189DisabledCtl2 & 0x80U)
          panic ("CI189 RESET FAIL enable changed ctl2=%02x", (unsigned) ci189DisabledCtl2);
        uint8_t ci189HardCtl2 = (uint8_t) (ci189DisabledCtl2 | 0x80U);
        ci189Write (0x085bU, ci189HardCtl2, 'E');
        snprintf (ci189Marker, sizeof (ci189Marker),
                  "CI189 H1 ctl=%02x ctl2=%02x ", (unsigned) ci189HardCtl, (unsigned) ci189HardCtl2);
        d2xq_console_text (ci189Marker);

        d2xq_console_text ("CI189 I0 ");
        ci189SetImem (0x77665501U);
        d2xq_console_text ("CI189 I1 ");
        uint8_t ci189OldReason = ci189Read (0x088fU, 'R');
        uint8_t ci189NewReason = (uint8_t) ((ci189OldReason & 1U) | 0x2aU);
        d2xq_console_text ("CI189 P1 ");
        ci189Write (0x088fU, ci189NewReason, 'W');
        snprintf (ci189Marker, sizeof (ci189Marker),
                  "CI189 P2 old=%02x new=%02x got=%02x ", (unsigned) ci189OldReason,
                  (unsigned) ci189NewReason, (unsigned) ci189Read (0x088fU, 'R'));
        d2xq_console_text (ci189Marker);
        // Final complete PMIC/IMEM verification precedes the arbiter shutdown.
        if (ci189Read (0x085aU, 'H') != ci189HardCtl ||
            ci189Read (0x085bU, 'E') != ci189HardCtl2 ||
            ci189Read (0x088fU, 'R') != ci189NewReason ||
            read32 (ci189Imem, 0x65c) != 0x77665501U)
          panic ("CI189 RESET FAIL final reset configuration changed");
        d2xq_console_text ("CI189 D0 ");
        ci189Call (0x02000109U, 2, 1, 0, "SDI0");
        d2xq_console_text ("CI189 D1 B0 ");
        ci189Call (0x02000901U, 1, 0, 0, "ARB0");
        // No SPMI access, PMIC check or rollback is permitted after ARB0.
        d2xq_console_text ("CI189 B1 R0 ");
        if (ci189PsAvailable > 0)
          ci189Call (0x02000902U, 1, 0, 0, "PS0");
        // Same-device deassert_ps_hold: direct write only if SCM is unavailable
        // or a supported call returned successfully. SCM errors already panic.
        d2xq_console_text ("CI189 R1 X0 ");
        write32 (ci189PsHold, 0, 0);
        asm volatile ("dsb sy" ::: "memory");
        d2xq_console_text ("CI189 X1 ");
        uint64_t ci189ResetStart = ci189Vct (), ci189ResetLast = ci189ResetStart;
        for (unsigned ci189Attempt = 0; ci189Attempt < 10000000U; ci189Attempt++) {
          ci189Delay ('T');
          uint64_t ci189Now = ci189Vct ();
          if (ci189Now < ci189ResetLast)
            panic ("CI189 RESET FAIL final wait backward");
          if (ci189Now - ci189ResetStart >= ci189Frequency * 10ULL)
            panic ("CI189 RESET FAIL PS_HOLD no reset after 10s");
          ci189ResetLast = ci189Now;
        }
        panic ("CI189 RESET FAIL final wait exhausted attempts=10000000");
      }
