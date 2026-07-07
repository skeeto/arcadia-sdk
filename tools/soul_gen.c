#include <stdint.h>

typedef struct {
    uint16_t checksum;
    uint16_t subcode;
    bool     has_subcode;
} Soul;

static int32_t progress_from_name(char *name)
{
    int32_t r = 0;
    for (int i = 0; i < 8; i++) {
        char c = name[i];
        if (c < '0' || c > '9') break;
        r = r * 10 + (c - '0');
    }
    return r;
}

static uint32_t activation_hash(uint8_t *code12, int32_t *table, bool raw_31bit)
{
    uint32_t esi = 0xd1e7u;
    for (int i = 0; i < 12; i++) {
        uint32_t eax = code12[i] & 0x7fu;
        esi ^= eax * esi;
        esi += eax << i;
        esi -= table[i] + (int32_t)esi % 100;
    }

    esi &= 0x7fffffffu;

    if (raw_31bit) {
        return (esi - 1) & 0x7fffffffu;
    }

    int32_t rem = (int32_t)esi % 0xff2f;
    return (rem - 1) & 0xFFFF;
}

// 0 <= n <= 99999999
static Soul generate_soul(int32_t n)
{
    static int32_t TABLE_LOW[12]  = {11,17,3,101,7,312,12,13,19,23,15,27};
    static int32_t TABLE_MID[12]  = {11,17,3,101,52,11,97,33,17,103,41,77};
    static int32_t TABLE_HIGH[12] = {87,31,6,44,52,11,88,47,17,11,41,94};

    Soul soul = {0};
    char name[8];
    uint32_t v = n;
    for (int i = 7; i >= 0; i--) {
        name[i] = '0' + (v % 10);
        v /= 10;
    }

    int32_t progress = progress_from_name(name);

    int32_t *table;
    if (progress < 0x708) {
        table = TABLE_LOW;
    } else if (progress < 0xce4) {
        table = TABLE_MID;
    } else {
        table = TABLE_HIGH;
    }

    uint8_t code12[12] = {'S','O','U','L'};
    for (int i = 0; i < 8; i++) code12[4 + i] = name[i];

    bool raw_31bit = (progress >= 0xce4);
    uint32_t h = activation_hash(code12, table, raw_31bit);

    if (raw_31bit) {
        uint32_t full = h + 1;
        soul.checksum = full & 0xFFFF;
        soul.subcode  = full >> 16;
        soul.has_subcode = 1;
    } else {
        soul.checksum = h + 1;
    }

    return soul;
}

#ifdef __wasm__
// $ clang --target=wasm32 -Oz --nostdlib Wl,--no-entry soul_gen.c
[[clang::export_name("generate")]]
int64_t generate(int32_t n)
{
    Soul s = generate_soul(n);
    return (int64_t)s.has_subcode<<32 | (int64_t)s.checksum<<16 | s.subcode;
}

#elif TEST
// $ cc -DTEST soul_gen.c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int32_t id   = atoi(argv[i]);
        Soul    soul = generate_soul(id);
        printf("SOUL%08d%04X", id, soul.checksum);
        if (soul.has_subcode) {
            printf(" %04X", soul.subcode);
        }
        putchar('\n');
    }
}
#endif
