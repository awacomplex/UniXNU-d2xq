      {
        // Only the trusted-counter + durably consumed RECOVERY-arm path enters.
        if (!ci190ArmConsumed || !repairCounter || counter != 7 || nextRepairSlot != 2)
          panic ("CI190 RESET REFUSE hardware gate");
        using ci190Signed32 = int;
        static_assert (sizeof (ci190Signed32) == 4, "SCM status requires a signed 32-bit int");
        d2xq_ufs_stage = -1;

        uint64_t ci190Frequency = 0, ci190El = 0;
        asm volatile ("mrs %0, cntfrq_el0" : "=r" (ci190Frequency) :: "memory");
        asm volatile ("mrs %0, CurrentEL" : "=r" (ci190El) :: "memory");
        char ci190Marker[224];
        snprintf (ci190Marker, sizeof (ci190Marker),
                  "CI190 F1 freq=%llu el=%llu ", ci190Frequency, ci190El);
        d2xq_console_text (ci190Marker);
        if (ci190Frequency != 19200000ULL || ci190El != 4ULL)
          panic ("CI190 RESET REFUSE freq=%llu el=%llu", ci190Frequency, ci190El);
        const uint64_t ci190DelayTicks = (ci190Frequency + 999999ULL) / 1000000ULL + 1ULL;
        uint64_t ci190ClockLast = 0;
        auto ci190Vct = [&] () -> uint64_t {
          uint64_t ci190Value = 0;
          asm volatile ("isb\n\tmrs %0, cntvct_el0" : "=r" (ci190Value) :: "memory");
          if (ci190Value < ci190ClockLast)
            panic ("CI190 RESET FAIL counter backward last=%llu now=%llu", ci190ClockLast, ci190Value);
          ci190ClockLast = ci190Value;
          return ci190Value;
        };
        auto ci190Delay = [&] (char ci190Tag) {
          uint64_t ci190Start = ci190Vct (), ci190Last = ci190Start;
          bool ci190Progress = false;
          for (unsigned ci190Round = 0; ci190Round < 4096; ci190Round++) {
            asm volatile ("nop" ::: "memory");
            uint64_t ci190Now = ci190Vct ();
            if (ci190Now < ci190Last)
              panic ("CI190 RESET FAIL delay %c backward start=%llu last=%llu now=%llu",
                     ci190Tag, ci190Start, ci190Last, ci190Now);
            if (ci190Now > ci190Last) ci190Progress = true;
            if (ci190Now - ci190Start >= ci190DelayTicks) return;
            ci190Last = ci190Now;
          }
          panic ("CI190 RESET FAIL delay %c %s start=%llu last=%llu",
                 ci190Tag, ci190Progress ? "exhausted" : "stalled", ci190Start, ci190Last);
        };
        d2xq_console_text ("CI190 Q0 ");
        ci190Delay ('Q');
        d2xq_console_text ("CI190 Q1 ");

        OSSharedPtr<IOMemoryMap> ci190CoreMap, ci190CnfgMap, ci190ObsMap, ci190RwMap;
        OSSharedPtr<IOMemoryMap> ci190ImemMap, ci190PsHoldMap;
        uintptr_t ci190Core = mapPhys (0x0c440000ULL, 0x1100, ci190CoreMap);
        uintptr_t ci190Cnfg = mapPhys (0x0c40a000ULL, 0x1000, ci190CnfgMap);
        uintptr_t ci190Imem = mapPhys (0x146bf000ULL, 0x1000, ci190ImemMap);
        uintptr_t ci190PsHold = mapPhys (0x0c264000ULL, 0x1000, ci190PsHoldMap);
        if (!ci190Core || !ci190Cnfg || !ci190Imem || !ci190PsHold)
          panic ("CI190 RESET FAIL map core=%u cnfg=%u imem=%u pshold=%u",
                 (unsigned) !!ci190Core, (unsigned) !!ci190Cnfg,
                 (unsigned) !!ci190Imem, (unsigned) !!ci190PsHold);
        uint32_t ci190Version = read32 (ci190Core, 0);
        if (ci190Version < 0x50000000U || ci190Version >= 0x60000000U)
          panic ("CI190 RESET FAIL SPMI version=%08x", ci190Version);
        const unsigned ci190Ee = 0;
        unsigned ci190Apid = 512, ci190Matches = 0, ci190OwnedMatches = 0;
        uint32_t ci190Entry = 0;
        for (unsigned ci190Index = 0; ci190Index < 512; ci190Index++) {
          uint32_t ci190Candidate = read32 (ci190Core, 0x900 + 4 * ci190Index);
          if (!ci190Candidate || ((ci190Candidate >> 8) & 0xfffU) != 0x008U) continue;
          ci190Matches++;
          if ((read32 (ci190Cnfg, 0x700 + 4 * ci190Index) & 7U) == ci190Ee) {
            ci190Apid = ci190Index;
            ci190Entry = ci190Candidate;
            ci190OwnedMatches++;
          }
        }
        if (ci190Apid >= 512)
          panic ("CI190 RESET FAIL no PON owner matches=%u ver=%08x", ci190Matches, ci190Version);
        uint64_t ci190ObsPhys = 0x0e600000ULL + 0x10000ULL * ci190Ee + 0x80ULL * ci190Apid;
        uintptr_t ci190ObsPage = mapPhys (ci190ObsPhys & ~0xfffULL, 0x1000, ci190ObsMap);
        uintptr_t ci190Rw = mapPhys (0x0c600000ULL + 0x10000ULL * ci190Apid, 0x1000, ci190RwMap);
        if (!ci190ObsPage || !ci190Rw)
          panic ("CI190 RESET FAIL channel map apid=%u", ci190Apid);
        uintptr_t ci190Obs = ci190ObsPage + (uintptr_t) (ci190ObsPhys & 0xfffULL);
        auto ci190CheckMapping = [&] () {
          if (read32 (ci190Core, 0) != ci190Version ||
              read32 (ci190Core, 0x900 + 4 * ci190Apid) != ci190Entry ||
              (read32 (ci190Cnfg, 0x700 + 4 * ci190Apid) & 7U) != ci190Ee)
            panic ("CI190 RESET FAIL mapping changed apid=%u ver=%08x", ci190Apid, ci190Version);
        };
        auto ci190Wait = [&] (uintptr_t ci190Channel, char ci190Tag) {
          uint32_t ci190Status = 0;
          for (unsigned ci190Poll = 0; ci190Poll < 1000; ci190Poll++) {
            ci190Status = read32 (ci190Channel, 0x08);
            if (ci190Status & 1U) {
              if (ci190Status & 0x0eU)
                panic ("CI190 RESET FAIL SPMI %c status=%x apid=%u", ci190Tag, ci190Status, ci190Apid);
              asm volatile ("dsb sy" ::: "memory");
              return;
            }
            ci190Delay (ci190Tag);
          }
          panic ("CI190 RESET FAIL SPMI %c timeout status=%x apid=%u", ci190Tag, ci190Status, ci190Apid);
        };
        auto ci190Read = [&] (unsigned ci190Address, char ci190Tag) -> uint8_t {
          if (ci190Address != 0x0805U && ci190Address != 0x085aU &&
              ci190Address != 0x085bU && ci190Address != 0x088fU)
            panic ("CI190 RESET FAIL read address=%x", ci190Address);
          ci190CheckMapping ();
          write32 (ci190Obs, 0, 0x08000000U | ((ci190Address & 0xffU) << 4));
          asm volatile ("dsb sy" ::: "memory");
          ci190Wait (ci190Obs, ci190Tag);
          return (uint8_t) (read32 (ci190Obs, 0x18) & 0xffU);
        };
        auto ci190Write = [&] (unsigned ci190Address, uint8_t ci190Value, char ci190Tag) {
          if (ci190Address != 0x085aU && ci190Address != 0x085bU && ci190Address != 0x088fU)
            panic ("CI190 RESET FAIL write address=%x", ci190Address);
          ci190CheckMapping ();
          write32 (ci190Rw, 0x10, ci190Value);
          asm volatile ("dsb sy" ::: "memory");
          write32 (ci190Rw, 0, (ci190Address & 0xffU) << 4);
          asm volatile ("dsb sy" ::: "memory");
          ci190Wait (ci190Rw, ci190Tag);
          uint8_t ci190Got = ci190Read (ci190Address, 'V');
          if (ci190Got != ci190Value)
            panic ("CI190 RESET FAIL byte address=%x want=%02x got=%02x",
                   ci190Address, (unsigned) ci190Value, (unsigned) ci190Got);
        };
        uint8_t ci190Subtype = ci190Read (0x0805U, 'S');
        if (ci190Subtype != 0x04U && ci190Subtype != 0x05U)
          panic ("CI190 RESET REFUSE subtype=%02x", (unsigned) ci190Subtype);
        snprintf (ci190Marker, sizeof (ci190Marker),
                  "CI190 P0 apid=%u ver=%08x subtype=%02x matches=%u owned=%u ",
                  ci190Apid, ci190Version, (unsigned) ci190Subtype, ci190Matches, ci190OwnedMatches);
        d2xq_console_text (ci190Marker);

        // Same-device scm.c: SMC32/64 carry state in registers 0..6 on INTERRUPTED.
        // Save them before any counter/log call; a C call may clobber fixed x0..6.
        auto ci190Smc = [&] (bool ci190Wide, uint32_t ci190Function, uint32_t ci190Arginfo,
                            uint64_t ci190Arg0, uint64_t ci190Arg1,
                            const char *ci190Tag, uint64_t *ci190Returns) -> ci190Signed32 {
          uint64_t ci190Regs[7] = { ci190Function, ci190Arginfo, ci190Arg0, ci190Arg1, 0, 0, 0 };
          uint64_t ci190Start = ci190Vct (), ci190Last = ci190Start;
          snprintf (ci190Marker, sizeof (ci190Marker), "CI190 S0 %s fn=%08x ", ci190Tag, ci190Function);
          d2xq_console_text (ci190Marker);
          for (unsigned ci190Call = 0; ci190Call < 128; ci190Call++) {
            {
              register uint64_t ci190R0 asm ("x0") = ci190Regs[0];
              register uint64_t ci190R1 asm ("x1") = ci190Regs[1];
              register uint64_t ci190R2 asm ("x2") = ci190Regs[2];
              register uint64_t ci190R3 asm ("x3") = ci190Regs[3];
              register uint64_t ci190R4 asm ("x4") = ci190Regs[4];
              register uint64_t ci190R5 asm ("x5") = ci190Regs[5];
              register uint64_t ci190R6 asm ("x6") = ci190Regs[6];
              asm volatile ("smc #0"
                            : "+r" (ci190R0), "+r" (ci190R1), "+r" (ci190R2), "+r" (ci190R3),
                              "+r" (ci190R4), "+r" (ci190R5), "+r" (ci190R6)
                            :
                            : "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                              "x16", "x17", "x19", "x20", "cc", "memory");
              ci190Regs[0] = ci190R0; ci190Regs[1] = ci190R1; ci190Regs[2] = ci190R2;
              ci190Regs[3] = ci190R3; ci190Regs[4] = ci190R4; ci190Regs[5] = ci190R5;
              ci190Regs[6] = ci190R6;
            }
            if (!ci190Wide)
              for (unsigned ci190Reg = 0; ci190Reg < 7; ci190Reg++) ci190Regs[ci190Reg] = (uint32_t) ci190Regs[ci190Reg];
            uint64_t ci190Now = ci190Vct ();
            if (ci190Now < ci190Last)
              panic ("CI190 RESET FAIL SCM %s counter backward", ci190Tag);
            if (ci190Now - ci190Start >= ci190Frequency * 10ULL)
              panic ("CI190 RESET FAIL SCM %s exceeded 10s calls=%u", ci190Tag, ci190Call + 1);
            ci190Last = ci190Now;
            ci190Signed32 ci190Status = (ci190Signed32) (uint32_t) ci190Regs[0];
            if (ci190Status == 1) continue;
            ci190Returns[0] = ci190Regs[1]; ci190Returns[1] = ci190Regs[2]; ci190Returns[2] = ci190Regs[3];
            snprintf (ci190Marker, sizeof (ci190Marker),
                      "CI190 S1 %s status=%d ret=%llx calls=%u ",
                      ci190Tag, (int) ci190Status, ci190Returns[0], ci190Call + 1);
            d2xq_console_text (ci190Marker);
            return ci190Status;
          }
          panic ("CI190 RESET FAIL SCM %s interrupted exhausted calls=128", ci190Tag);
        };
        uint64_t ci190Returns[3] = { 0, 0, 0 };
        bool ci190Wide = true;
        ci190Signed32 ci190AbiStatus = ci190Smc (true, 0xc2000601U, 1, 0x82000601U, 0, "ABI64", ci190Returns);
        if (ci190AbiStatus != 0 || ci190Returns[0] == 0) {
          ci190Wide = false;
          ci190AbiStatus = ci190Smc (false, 0x82000601U, 1, 0x82000601U, 0, "ABI32", ci190Returns);
          if (ci190AbiStatus != 0 || ci190Returns[0] == 0)
            panic ("CI190 RESET REFUSE SCM no ARMv8 ABI status=%d ret=%llx", (int) ci190AbiStatus, ci190Returns[0]);
        }
        const uint32_t ci190AbiMask = ci190Wide ? 0x40000000U : 0;
        snprintf (ci190Marker, sizeof (ci190Marker), "CI190 A2 abi=%u ", ci190Wide ? 64U : 32U);
        d2xq_console_text (ci190Marker);
        auto ci190Available = [&] (uint32_t ci190Function, const char *ci190Tag) -> ci190Signed32 {
          ci190Signed32 ci190Status = ci190Smc (ci190Wide, 0x02000601U | ci190AbiMask, 1,
                                       ci190Function, 0, ci190Tag, ci190Returns);
          if (ci190Status != 0)
            panic ("CI190 RESET REFUSE availability %s status=%d", ci190Tag, (int) ci190Status);
          ci190Signed32 ci190Value = (ci190Signed32) (uint32_t) ci190Returns[0];
          if (ci190Value < 0)
            panic ("CI190 RESET REFUSE availability %s value=%d", ci190Tag, (int) ci190Value);
          return ci190Value;
        };
        ci190Signed32 ci190DloadAvailable = ci190Available (0x02000110U, "AV-DLOAD");
        ci190Signed32 ci190SdiAvailable = ci190Available (0x02000109U, "AV-SDI");
        ci190Signed32 ci190ArbAvailable = ci190Available (0x02000901U, "AV-ARB");
        ci190Signed32 ci190PsAvailable = ci190Available (0x02000902U, "AV-PS");
        ci190Signed32 ci190IoWriteAvailable = 0;
        if (ci190DloadAvailable == 0)
          ci190IoWriteAvailable = ci190Available (0x02000502U, "AV-IOW");
        if (ci190SdiAvailable <= 0 || (ci190DloadAvailable == 0 && ci190IoWriteAvailable <= 0))
          panic ("CI190 RESET REFUSE SCM unavailable dload=%d sdi=%d arb=%d ps=%d iow=%d",
                 (int) ci190DloadAvailable, (int) ci190SdiAvailable,
                 (int) ci190ArbAvailable, (int) ci190PsAvailable, (int) ci190IoWriteAvailable);
        auto ci190Call = [&] (uint32_t ci190Function, uint32_t ci190Arginfo,
                             uint64_t ci190Arg0, uint64_t ci190Arg1, const char *ci190Tag) {
          ci190Signed32 ci190Status = ci190Smc (ci190Wide, ci190Function | 0x80000000U | ci190AbiMask,
                                       ci190Arginfo, ci190Arg0, ci190Arg1, ci190Tag, ci190Returns);
          if (ci190Status != 0)
            panic ("CI190 RESET FAIL SCM %s status=%d ret=%llx", ci190Tag, (int) ci190Status, ci190Returns[0]);
        };
        auto ci190SetImem = [&] (uint32_t ci190Value) {
          write32 (ci190Imem, 0x65c, ci190Value);
          asm volatile ("dsb sy" ::: "memory");
          uint32_t ci190Got = read32 (ci190Imem, 0x65c);
          if (ci190Got != ci190Value)
            panic ("CI190 RESET FAIL IMEM want=%08x got=%08x", ci190Value, ci190Got);
        };

        // Normal Samsung reboot clears secure crash DLOAD; no unproven cookie.
        d2xq_console_text ("CI190 C0 ");
        if (ci190DloadAvailable > 0) {
          ci190Call (0x02000110U, 2, 0, 0, "DLOAD0");
          d2xq_console_text ("CI190 C1-DLOAD ");
        } else {
          // Same-device msm-poweroff.c: unsupported DLOAD uses this DT resource
          // through scm_io_write(address, arg1). Clearing DLOAD gives arg1=0.
          // No direct TCSR map, mask, alternate address or unproven IO_READ.
          ci190Call (0x02000502U, 2, 0x01fd3000ULL, 0, "TCSR0");
          d2xq_console_text ("CI190 C1-TCSR status0 ");
        }
        ci190SetImem (0);
        d2xq_console_text ("CI190 C1 ");
        uint8_t ci190OldCtl2 = ci190Read (0x085bU, 'E');
        d2xq_console_text ("CI190 H0 ");
        ci190Write (0x085bU, (uint8_t) (ci190OldCtl2 & 0x7fU), 'E');
        uint64_t ci190ResetConfigLast = ci190Vct ();
        for (unsigned ci190Us = 0; ci190Us < 500; ci190Us++) {
          ci190Delay ('H');
          uint64_t ci190Now = ci190Vct ();
          if (ci190Now < ci190ResetConfigLast)
            panic ("CI190 RESET FAIL hard-reset delay backward");
          ci190ResetConfigLast = ci190Now;
        }
        uint8_t ci190OldCtl = ci190Read (0x085aU, 'H');
        uint8_t ci190HardCtl = (uint8_t) ((ci190OldCtl & 0xf0U) | 0x07U);
        ci190Write (0x085aU, ci190HardCtl, 'H');
        uint8_t ci190DisabledCtl2 = ci190Read (0x085bU, 'E');
        if (ci190DisabledCtl2 & 0x80U)
          panic ("CI190 RESET FAIL enable changed ctl2=%02x", (unsigned) ci190DisabledCtl2);
        uint8_t ci190HardCtl2 = (uint8_t) (ci190DisabledCtl2 | 0x80U);
        ci190Write (0x085bU, ci190HardCtl2, 'E');
        snprintf (ci190Marker, sizeof (ci190Marker),
                  "CI190 H1 ctl=%02x ctl2=%02x ", (unsigned) ci190HardCtl, (unsigned) ci190HardCtl2);
        d2xq_console_text (ci190Marker);

        d2xq_console_text ("CI190 I0 ");
        ci190SetImem (0x77665501U);
        d2xq_console_text ("CI190 I1 ");
        uint8_t ci190OldReason = ci190Read (0x088fU, 'R');
        uint8_t ci190NewReason = (uint8_t) ((ci190OldReason & 1U) | 0x2aU);
        d2xq_console_text ("CI190 P1 ");
        ci190Write (0x088fU, ci190NewReason, 'W');
        snprintf (ci190Marker, sizeof (ci190Marker),
                  "CI190 P2 old=%02x new=%02x got=%02x ", (unsigned) ci190OldReason,
                  (unsigned) ci190NewReason, (unsigned) ci190Read (0x088fU, 'R'));
        d2xq_console_text (ci190Marker);
        // Final complete PMIC/IMEM verification precedes the arbiter shutdown.
        if (ci190Read (0x085aU, 'H') != ci190HardCtl ||
            ci190Read (0x085bU, 'E') != ci190HardCtl2 ||
            ci190Read (0x088fU, 'R') != ci190NewReason ||
            read32 (ci190Imem, 0x65c) != 0x77665501U)
          panic ("CI190 RESET FAIL final reset configuration changed");
        d2xq_console_text ("CI190 D0 ");
        ci190Call (0x02000109U, 2, 1, 0, "SDI0");
        d2xq_console_text ("CI190 D1 B0 ");
        if (ci190ArbAvailable > 0)
          ci190Call (0x02000901U, 1, 0, 0, "ARB0");
        else
          d2xq_console_text ("CI190 B-SKIP ");
        // Same-device source skips an unavailable arbiter-disable call. No
        // SPMI access, PMIC check or rollback is permitted after this point.
        d2xq_console_text ("CI190 B1 R0 ");
        if (ci190PsAvailable > 0)
          ci190Call (0x02000902U, 1, 0, 0, "PS0");
        // Same-device deassert_ps_hold: direct write only if SCM is unavailable
        // or a supported call returned successfully. SCM errors already panic.
        d2xq_console_text ("CI190 R1 X0 ");
        write32 (ci190PsHold, 0, 0);
        asm volatile ("dsb sy" ::: "memory");
        d2xq_console_text ("CI190 X1 ");
        uint64_t ci190ResetStart = ci190Vct (), ci190ResetLast = ci190ResetStart;
        for (unsigned ci190Attempt = 0; ci190Attempt < 10000000U; ci190Attempt++) {
          ci190Delay ('T');
          uint64_t ci190Now = ci190Vct ();
          if (ci190Now < ci190ResetLast)
            panic ("CI190 RESET FAIL final wait backward");
          if (ci190Now - ci190ResetStart >= ci190Frequency * 10ULL)
            panic ("CI190 RESET FAIL PS_HOLD no reset after 10s");
          ci190ResetLast = ci190Now;
        }
        panic ("CI190 RESET FAIL final wait exhausted attempts=10000000");
      }
