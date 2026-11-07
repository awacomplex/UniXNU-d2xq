// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Called once after bsd_bufferinit. Failures stop before storage publication.
void sheng_pmgr_run(void);
// Rechecks the original class identity, service match and provider ancestry.
int d2xq_ci194_pmgr_ready(void);

#ifdef __cplusplus
}
#endif
