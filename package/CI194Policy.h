/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef D2XQ_CI194_POLICY_H
#define D2XQ_CI194_POLICY_H
#include <stdint.h>
#include <stddef.h>

/* NativeUfs-only ABI. Do not alias NativeVerifiedBlockPolicy or put pointers in
 * this descriptor. All byte digests/UUIDs use their literal wire byte order. */
struct NativeExternalPolicyV1 {
    uint64_t firstLba;
    uint64_t manifestBytes;
    uint64_t windowOffset;
    uint64_t windowBytes;
    uint64_t minimumGeneration;
    uint8_t manifestSha256[32];
    uint8_t imageSha256[32];
    uint8_t volumeUuid[16];
    char label[16];
};
#ifdef __cplusplus
static_assert(sizeof(NativeExternalPolicyV1) == 136, "external policy size");
static_assert(alignof(NativeExternalPolicyV1) == 8, "external policy alignment");
static_assert(offsetof(NativeExternalPolicyV1, firstLba) == 0, "policy LBA offset");
static_assert(offsetof(NativeExternalPolicyV1, minimumGeneration) == 32, "policy generation offset");
static_assert(offsetof(NativeExternalPolicyV1, manifestSha256) == 40, "policy manifest digest offset");
static_assert(offsetof(NativeExternalPolicyV1, imageSha256) == 72, "policy image digest offset");
static_assert(offsetof(NativeExternalPolicyV1, volumeUuid) == 104, "policy UUID offset");
static_assert(offsetof(NativeExternalPolicyV1, label) == 120, "policy label offset");
extern "C" {
#endif
extern const uint64_t native_ufs_external_policy_version;
extern const uint64_t native_ufs_external_policy_count;
extern const struct NativeExternalPolicyV1 native_ufs_external_policies[2];
extern const uint8_t verified_root_key[395];
extern const uint8_t native_ufs_external_key_sha256[32];
extern const uint8_t native_ufs_external_root_manifest_sha256[32];
extern const uint8_t native_ufs_external_image_sha256[32];
extern const uint8_t native_ufs_external_signed_headers[2][160];
extern const uint8_t native_ufs_external_signatures[2][384];
void native_ufs_ci194_finish(void) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
