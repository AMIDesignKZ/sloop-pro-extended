/* SPDX-License-Identifier: GPL-3.0-only */
/* Single storage definition for the warm-reset guard. */
#include "bootguard.h"
#if defined(__APPLE__)
bootguard_t bootguard;
#else
bootguard_t bootguard __attribute__((section(".noinit")));
#endif
