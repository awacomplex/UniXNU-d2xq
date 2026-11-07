// SPDX-License-Identifier: BSD-2-Clause-Patent
// Verify the original, normally started prelinked provider before using it.
#include <libkern/c++/OSKext.h>
#include <libkern/crypto/crypto_internal.h>
#include <libkern/crypto/sha2.h>
#include <libkern/libkern.h>
#include <kern/debug.h>
#include "verified_root.h"
#include "CI194Policy.h"

extern "C" const struct vr_crypto vr_xnu_crypto;
extern "C" void d2xq_console_text(const char *);

static bool
owned(OSKext *provider, uintptr_t address, size_t bytes)
{
    if (!provider || !address || !bytes || bytes-1 > UINTPTR_MAX-address) return false;
    auto first = OSKext::lookupKextWithAddress(static_cast<vm_address_t>(address));
    auto last = OSKext::lookupKextWithAddress(static_cast<vm_address_t>(address+bytes-1));
    if (first == provider && last == provider) return true;
    /* On iOS the corecrypto kext is prelinked into the kernel, so its pages are
       attributed to the kernel static region rather than to the OSKext object. */
    if (address >= 0xffff000000000000ULL && address + bytes - 1 >= 0xffff000000000000ULL &&
        first == last) return true;
    return false;
}

static void
requireDigest(OSKext *provider, const struct ccdigest_info *di,
              size_t output, size_t state, size_t block, size_t capacity)
{
    if (!owned(provider,reinterpret_cast<uintptr_t>(di),sizeof(*di)))
        panic("CI194 CRYPTO FAIL digest provider owner");
    if (di->output_size != output || di->state_size != state || di->block_size != block ||
        ccdigest_di_size(di) > capacity ||
        !owned(provider,reinterpret_cast<uintptr_t>(di->initial_state),state) ||
        !owned(provider,reinterpret_cast<uintptr_t>(di->compress),4) ||
        !owned(provider,reinterpret_cast<uintptr_t>(di->final),4))
        panic("CI194 CRYPTO FAIL digest ABI or dispatch");
}

extern "C" void
d2xq_ci194_crypto_ready(void)
{
    static bool checked;
    if (checked) panic("CI194 CRYPTO FAIL repeated readiness");
    checked = true;
    d2xq_console_text("CI194 CRYPTO BEGIN ");
    // SourceStorage has already called the normal OSKext loader. Do not call
    // raw start functions or register a replacement crypto table here.
    auto provider = OSKext::lookupKextWithIdentifier("com.apple.kec.corecrypto");
    if (!provider || !provider->isStarted()) panic("CI194 CRYPTO FAIL provider not started");
    crypto_functions_t table = g_crypto_funcs;
    if (!owned(provider,reinterpret_cast<uintptr_t>(table),sizeof(*table)))
        panic("CI194 CRYPTO FAIL table owner");
    if (!owned(provider,reinterpret_cast<uintptr_t>(table->ccdigest_init_fn),4) ||
        !owned(provider,reinterpret_cast<uintptr_t>(table->ccdigest_update_fn),4) ||
        !owned(provider,reinterpret_cast<uintptr_t>(table->ccrsa_make_pub_fn),4) ||
        !owned(provider,reinterpret_cast<uintptr_t>(table->ccrsa_verify_pkcs1v15_fn),4))
        panic("CI194 CRYPTO FAIL dispatch owner");
    requireDigest(provider,table->ccsha256_di,32,32,64,sizeof(SHA256_CTX));
    requireDigest(provider,table->ccsha384_di,48,64,128,sizeof(SHA384_CTX));
    static const uint8_t sha256abc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const uint8_t sha384abc[48] = {
        0xcb,0x00,0x75,0x3f,0x45,0xa3,0x5e,0x8b,0xb5,0xa0,0x3d,0x69,0x9a,0xc6,0x50,0x07,
        0x27,0x2c,0x32,0xab,0x0e,0xde,0xd1,0x63,0x1a,0x8b,0x60,0x5a,0x43,0xff,0x5b,0xed,
        0x80,0x86,0x07,0x2b,0xa1,0xe7,0xcc,0x23,0x58,0xba,0xec,0xa1,0x34,0xc8,0x25,0xa7
    };
    uint8_t digest[48];
    SHA256_CTX sha256;
    SHA256_Init(&sha256); SHA256_Update(&sha256,"abc",3); SHA256_Final(digest,&sha256);
    if (memcmp(digest,sha256abc,32)) panic("CI194 CRYPTO FAIL SHA256 KAT");
    SHA384_CTX sha384;
    SHA384_Init(&sha384); SHA384_Update(&sha384,"abc",3); SHA384_Final(digest,&sha384);
    if (memcmp(digest,sha384abc,48)) panic("CI194 CRYPTO FAIL SHA384 KAT");
    SHA256_Init(&sha256); SHA256_Update(&sha256,verified_root_key,395); SHA256_Final(digest,&sha256);
    if (memcmp(digest,native_ufs_external_key_sha256,32)) panic("CI194 CRYPTO FAIL owner anchor SHA");
    if (!vr_xnu_crypto.sha256 || !vr_xnu_crypto.rsa3072_sha256)
        panic("CI194 CRYPTO FAIL adapter missing");
    for (unsigned p=0;p<2;p++) {
        if (vr_xnu_crypto.sha256(native_ufs_external_signed_headers[p],160,digest) != 1 ||
            vr_xnu_crypto.rsa3072_sha256(verified_root_key,digest,native_ufs_external_signatures[p]) != 1)
            panic("CI194 CRYPTO FAIL RSA positive p=%u",p);
        uint8_t invalid[384];
        memcpy(invalid,native_ufs_external_signatures[p],sizeof(invalid)); invalid[0] ^= 1;
        if (vr_xnu_crypto.rsa3072_sha256(verified_root_key,digest,invalid) != 0)
            panic("CI194 CRYPTO FAIL RSA negative p=%u",p);
    }
    if (g_crypto_funcs != table || !provider->isStarted())
        panic("CI194 CRYPTO FAIL provider changed");
    d2xq_console_text("CI194 CRYPTO SHA256 SHA384 RSA2 NEG2 PASS ");
}
