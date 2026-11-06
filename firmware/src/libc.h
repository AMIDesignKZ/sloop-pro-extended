/* SPDX-License-Identifier: GPL-3.0-only */
/* Freestanding C library declarations supplied by firmware/src/libc.c.
 * The target toolchain intentionally has no hosted string.h. */
#pragma once
#include <stddef.h>

void *memset(void *dst, int value, size_t length);
void *memcpy(void *dst, const void *src, size_t length);
int memcmp(const void *a, const void *b, size_t length);
