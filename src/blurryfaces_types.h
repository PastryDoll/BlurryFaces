#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#define CompletePastWriteBeforeFutureWrite asm volatile("" : : : "memory");

