/*******************************************************************************
 *
 * Time related utilities and performance counters
 *
 * Surround-view library interface
 *
 * Copyright (c) 2017 Cogent Embedded Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#ifndef SV_LIBSV_UTILS_TIME_H
#define SV_LIBSV_UTILS_TIME_H

/* This file can be included from C++ and C code */
#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <stdint.h>

/*******************************************************************************
 * Performance counters
 ******************************************************************************/

/* ...retrieve CPU cycles count
 * due to thread migration, use generic clock interface */
static inline uint32_t get_cpu_cycles(void)
{
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...translate value into nanoseconds (ignore wrap-around) */
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec);
}

static inline uint32_t get_time_usec(void)
{
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...translate value into microseconds (ignore wrap-around) */
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000);
}

#ifdef __cplusplus
}
#endif

#endif /* SV_LIBSV_UTILS_TIME_H */
