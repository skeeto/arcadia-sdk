#include <stdint.h>

// NetSpades 1.4 (Synthetic Reality, 1995-1997) activation-code generator.
//
// Reversed from SPADES.EXE (16-bit NE). The "Enter Activation Code" dialog
// accepts a 10-character code, upper-cases it, then requires:
//
//     code = SSSSSS + "%04X" % checksum(SSSSSS)
//
// where SSSSSS is any 6-character serial (stored/shown as the "Serial Number")
// and the trailing four upper-hex digits are the checksum below. The compare is
// case-insensitive, so the serial may contain letters. All internal arithmetic
// mirrors the original 16-bit C: signed chars, 16-bit intermediates, 32-bit
// (long) accumulate with wraparound, and signed truncating division.
static uint16_t checksum(char const *serial)
{
    int8_t c[6];
    for (int i = 0; i < 6; i++) {
        c[i] = (int8_t)serial[i];
    }

    int16_t A = (int16_t)(c[0] + c[1]);
    int16_t B = (int16_t)(c[1] * c[5]);
    int16_t C = (int16_t)(c[2] - c[0]);
    int16_t D = (int16_t)(c[3] * c[4]);
    int16_t E = (int16_t)(c[4] + c[2]);
    int16_t F = (int16_t)(c[5] * c[3]);

    int32_t acc = A;
    acc = (int32_t)((uint32_t)acc * (uint32_t)(int32_t)B);
    acc += C;
    acc = (int32_t)((uint32_t)acc * (uint32_t)(int32_t)D);
    acc /= 3;
    acc = (int32_t)((uint32_t)acc * (uint32_t)(int32_t)E);
    acc = (int32_t)((uint32_t)acc * (uint32_t)(int32_t)F);

    uint32_t u = (uint32_t)acc;
    return (uint16_t)((u >> 16) ^ (u & 0xffff));
}

#if __wasm__
// $ clang --target=wasm32 -Oz -s -nostdlib -Wl,--no-entry spades_gen.c
[[clang::export_name("checksum")]]
uint16_t wasm_checksum(uint32_t s0, uint32_t s1, uint32_t s2,
                       uint32_t s3, uint32_t s4, uint32_t s5)
{
    char serial[6] = {(char)s0, (char)s1, (char)s2, (char)s3, (char)s4, (char)s5};
    return checksum(serial);
}

#elif TEST
// $ cc -DTEST spades_gen.c -o spades_gen
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        char serial[7] = "000000";
        for (int j = 0; j < 6 && argv[i][j]; j++) {
            serial[j] = (char)toupper((unsigned char)argv[i][j]);
        }
        printf("%s%04X\n", serial, checksum(serial));
    }
    return 0;
}
#endif
