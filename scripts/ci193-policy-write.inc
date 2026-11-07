    {
      // CI193 stages only the pinned owner policies, outside the verified image.
      // No image, progress, log or store block may be written by this probe.
      static const uint64_t sourceFirst = 0x24f13ULL;
      static const uint64_t sourceBlocks = 20205;
      static const uint64_t imageEnd = 0x44f400ULL;
      static const uint64_t policyFirst = 0x44f800ULL;
      static const uint64_t policyBlocks = 17894;
      static const uint64_t policyEnd = policyFirst + policyBlocks;
      static const uint64_t payloadBytes = policyBlocks * 4096;
      static const uint64_t policyBytes[2] = { 47481664ULL, 25805344ULL };
      static const uint64_t policyPadded[2] = { 47484928ULL, 25808896ULL };
      static const uint64_t policyOffsets[2] = { 0, 47484928ULL };
      static const uint8_t metadataMagic[8] = { 'D', '2', 'P', 'O', 'L', '1', '9', '3' };
      static const uint8_t imageSha[32] = {
        0xfd,0x43,0xee,0x70,0x35,0xce,0x18,0x5c,0xe4,0x21,0x5a,0xc9,0x3f,0xdc,0x00,0x6f,
        0xd7,0x84,0xa5,0x4b,0x3b,0x74,0x42,0xe2,0x14,0x57,0xc2,0x26,0xcb,0xe0,0x30,0x93
      };
      static const uint8_t rootManifestSha[32] = {
        0xbc,0x90,0x8a,0x6c,0xf6,0x64,0x91,0x05,0x50,0x77,0x79,0x85,0x50,0x66,0x0f,0x40,
        0x12,0x8b,0xfe,0x6d,0xce,0x2f,0xdc,0x0d,0xb5,0x6e,0x6a,0xf9,0x65,0x11,0x20,0x8d
      };
      /* CI193_POLICY_BINDINGS */
      uint8_t *dma = (uint8_t *) gHost.virt + kDataOffset;
      d2xq_ufs_stage = -1;
      d2xq_console_text ("CI193 POLICY BEGIN ");
      static bool attemptStarted = false;
      if (attemptStarted) panic ("CI193 POLICY REFUSE repeated entry");
      attemptStarted = true;
      if (storeMode () != 0 || logMode () != 0 || gStoreOpen || gLogOpen || gQueue.installed)
        panic ("CI193 POLICY REFUSE nonexclusive transport");
      if (layout->userdataFirst != 0x21f400ULL || layout->userdataLast != 0x3b90bfaULL ||
          imageEnd != layout->userdataFirst + 140ULL * 16384 || policyFirst != imageEnd + 1024 ||
          policyEnd != 0x453de6ULL || policyEnd <= policyFirst || policyEnd > layout->userdataLast ||
          policyBlocks + 1 > sourceBlocks || sourceFirst + sourceBlocks > layout->userdataFirst ||
          policyOffsets[1] != policyPadded[0] || policyPadded[0] + policyPadded[1] != payloadBytes)
        panic ("CI193 POLICY FAIL geometry");
      const uint64_t ctrLba = layout->userdataLast;
      auto load32le = [] (const uint8_t *p) -> uint32_t {
        uint32_t v = 0; for (unsigned i = 0; i < 4; i++) v |= (uint32_t) p[i] << (i * 8); return v;
      };
      auto load64le = [] (const uint8_t *p) -> uint64_t {
        uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v |= (uint64_t) p[i] << (i * 8); return v;
      };
      auto readBlock = [&] (uint64_t lba, const char *phase) {
        if (lba > 0xffffffffULL ||
            (lba != ctrLba && lba != layout->userdataFirst &&
             !(lba >= sourceFirst && lba <= sourceFirst + policyBlocks) &&
             !(lba >= policyFirst && lba < policyEnd)))
          panic ("CI193 POLICY FAIL read bounds lba=%llx", lba);
        uint8_t cdb[10] = { 0x28,0,(uint8_t)(lba>>24),(uint8_t)(lba>>16),
                            (uint8_t)(lba>>8),(uint8_t)lba,0,0,1,0 };
        uint32_t status = 0;
        IOReturn result = hostScsiDir (0,cdb,10,2,4096,1900000,&status);
        if (result != kIOReturnSuccess || status != 0)
          panic ("CI193 POLICY FAIL %s read lba=%llx rr=%x st=%04x",phase,lba,result,status);
      };
      auto shaBytes = [] (const void *p, size_t bytes, uint8_t *out) {
        SHA256_CTX sha; SHA256_Init (&sha); SHA256_Update (&sha,p,bytes); SHA256_Final (out,&sha);
      };
      readBlock (ctrLba,"counter before");
      if (memcmp (dma,"D2XQ",4) != 0 || load32le(dma+4) != 0 || load64le(dma+8) != 140 ||
          memcmp (dma+16,"D2SAFE84",8) != 0 || memcmp(dma+24,imageSha,32) != 0 ||
          load64le(dma+56) != ~140ULL)
        panic ("CI193 POLICY REFUSE counter state");
      uint8_t digest[32]; shaBytes (dma,64,digest);
      if (memcmp(dma+64,digest,32) != 0) panic ("CI193 POLICY REFUSE counter checksum");
      for (unsigned b = 96; b < 4096; b++)
        if (dma[b] != 0) panic ("CI193 POLICY REFUSE counter reserved b=%u",b);
      static uint8_t savedCounter[4096]; memcpy(savedCounter,dma,4096);
      readBlock (layout->userdataFirst,"root manifest"); shaBytes(dma,4096,digest);
      if (memcmp(digest,rootManifestSha,32) != 0) panic ("CI193 POLICY REFUSE root manifest SHA");
      d2xq_console_text ("CI193 COUNTER140 ROOT OK ");

      readBlock (sourceFirst+policyBlocks,"metadata");
      if (memcmp(dma,metadataMagic,8) != 0 || load32le(dma+8) != 1 || load32le(dma+12) != 128 ||
          load64le(dma+16) != payloadBytes || load64le(dma+24) != policyFirst ||
          load64le(dma+32) != policyEnd || load64le(dma+40) != layout->userdataFirst ||
          load64le(dma+48) != ctrLba || load64le(dma+56) != sourceFirst ||
          memcmp(dma+64,policyBundleSha,32) != 0)
        panic ("CI193 POLICY REFUSE metadata identity");
      shaBytes(dma,96,digest);
      if (memcmp(dma+96,digest,32) != 0) panic ("CI193 POLICY REFUSE metadata checksum");
      for (unsigned b = 128; b < 4096; b++)
        if (dma[b] != 0) panic ("CI193 POLICY REFUSE metadata reserved b=%u",b);
      static uint8_t savedMetadata[4096]; memcpy(savedMetadata,dma,4096);

      // One pass checks the padded bundle, both unpadded signed envelopes and
      // every alignment byte. Target mismatches are repairable, source mismatches are not.
      auto checkWindow = [&] (uint64_t first, char phase, bool allowMismatch) -> bool {
        SHA256_CTX bundle,policies[2]; SHA256_Init(&bundle);
        for (unsigned p=0;p<2;p++) SHA256_Init(&policies[p]);
        bool paddingGood = true;
        for (uint64_t b=0;b<policyBlocks;b++) {
          if ((b & 4095U) == 0) {
            char marker[24]; snprintf(marker,sizeof(marker),"%c%llu ",phase,b/4096); d2xq_console_text(marker);
          }
          readBlock(first+b,phase=='S'?"source":"target");
          SHA256_Update(&bundle,dma,4096);
          const uint64_t offset=b*4096;
          const unsigned p=offset<policyOffsets[1]?0:1;
          const uint64_t relative=offset-policyOffsets[p];
          const size_t valid=relative>=policyBytes[p]?0:
            (size_t)(policyBytes[p]-relative<4096?policyBytes[p]-relative:4096);
          if (valid) SHA256_Update(&policies[p],dma,valid);
          for (unsigned z=(unsigned)valid;z<4096;z++) if (dma[z]!=0) paddingGood=false;
        }
        uint8_t total[32],one[2][32]; SHA256_Final(total,&bundle);
        for (unsigned p=0;p<2;p++) SHA256_Final(one[p],&policies[p]);
        bool matches=paddingGood && memcmp(total,policyBundleSha,32)==0 &&
          memcmp(one[0],policyEnvelopeSha[0],32)==0 && memcmp(one[1],policyEnvelopeSha[1],32)==0;
        if (!matches && !allowMismatch) panic("CI193 POLICY FAIL %c full SHA or padding",phase);
        return matches;
      };
      if (!checkWindow(sourceFirst,'S',false)) panic("CI193 POLICY FAIL source incomplete");
      d2xq_console_text("CI193 SOURCE OK ");
      bool matched=checkWindow(policyFirst,'T',true);
      auto writeBlock = [&] (uint64_t lba) {
        if (lba<policyFirst || lba>=policyEnd || lba>0xffffffffULL)
          panic("CI193 POLICY FAIL write bounds lba=%llx",lba);
        uint8_t cdb[10]={0x2a,0,(uint8_t)(lba>>24),(uint8_t)(lba>>16),
                         (uint8_t)(lba>>8),(uint8_t)lba,0,0,1,0};
        uint32_t status=0;
        IOReturn result=hostScsiDir(0,cdb,10,1,4096,1900000,&status);
        if(result!=kIOReturnSuccess || status!=0)
          panic("CI193 POLICY FAIL write lba=%llx rr=%x st=%04x",lba,result,status);
      };
      if (!matched) {
        SHA256_CTX copied; SHA256_Init(&copied);
        for (uint64_t b=0;b<policyBlocks;b++) {
          if ((b & 4095U)==0) {
            char marker[24]; snprintf(marker,sizeof(marker),"W%llu ",b/4096); d2xq_console_text(marker);
          }
          readBlock(sourceFirst+b,"copy source"); SHA256_Update(&copied,dma,4096);
          writeBlock(policyFirst+b);
        }
        SHA256_Final(digest,&copied);
        if(memcmp(digest,policyBundleSha,32)!=0) panic("CI193 POLICY FAIL source changed during copy");
      }
      // Even an already-matching target must be durably flushed, then freshly
      // read and checked in full, before any success result is possible.
      uint8_t flush[10]={0x35,0,0,0,0,0,0,0,0,0}; uint32_t status=0;
      IOReturn result=hostScsiDir(0,flush,10,0,0,1900000,&status);
      if(result!=kIOReturnSuccess || status!=0) panic("CI193 POLICY FAIL flush rr=%x st=%04x",result,status);
      d2xq_console_text("CI193 FLUSH OK ");
      if(!checkWindow(policyFirst,'F',false)) panic("CI193 POLICY FAIL readback incomplete");
      readBlock(sourceFirst+policyBlocks,"metadata after");
      if(memcmp(dma,savedMetadata,4096)!=0) panic("CI193 POLICY FAIL metadata changed");
      readBlock(layout->userdataFirst,"root manifest after"); shaBytes(dma,4096,digest);
      if(memcmp(digest,rootManifestSha,32)!=0) panic("CI193 POLICY FAIL root manifest changed");
      readBlock(ctrLba,"counter after");
      if(memcmp(dma,savedCounter,4096)!=0) panic("CI193 POLICY FAIL counter changed");
      panic("CI193 POLICY WRITE PASS bytes=%llu counter=%llu unchanged=1 skip=%u",
            payloadBytes,load64le(dma+8),(unsigned)matched);
    }
