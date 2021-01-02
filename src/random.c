#include "global.h"
#include "random.h"

EWRAM_DATA static u8 sUnknown = 0;
EWRAM_DATA static u16 sRng3Value[2] = {0};

// IWRAM common
u32 gRngValue;
u32 gRng2Value;

static u16 Xoroshiro32PlusPlusInternal(u16 * seed);

u16 Random (void)
{
    gRngValue = ISO_RANDOMIZE1(gRngValue);
    return gRngValue >> 16;
}

void SeedRng (u16 seed)
{
    gRngValue = seed;
    sUnknown = 0;
}

void SeedRng2 (u16 seed)
{
    gRng2Value = seed;
}

u16 Random2 (void)
{
    gRng2Value = ISO_RANDOMIZE1(gRng2Value);
    return gRng2Value >> 16;
}

const u16 a = 13;
const u16 b = 5;
const u16 c = 10;
const u16 d = 9;
 
static inline u16 rol(u16 x, u16 k)
{
    return (x << k) | (x >> ((sizeof(x) * 8) - k));
}

u16 Random3 (void)
{
    return Xoroshiro32PlusPlusInternal(sRng3Value);
}

void SeedRng3 (u32 seed)
{
    if (seed == 0) {
        seed = 1;
    }

    sRng3Value[0] = seed & 0xffff;
    sRng3Value[1] = (seed >> 16) & 0xffff;
}

u32 GetRng3Seed (void)
{
    return (sRng3Value[0] | sRng3Value[1] << 16);
}


static u16 Xoroshiro32PlusPlusInternal (u16 * seed)
{
    u16 result = rol(seed[0] + seed[1], d) + seed[0];
 
    seed[1] ^= seed[0];
    seed[0] = rol(seed[0], a) ^ seed[1] ^ (seed[1] << b);
    seed[1] = rol(seed[1], c);
 
 
    return result - 1;
}
