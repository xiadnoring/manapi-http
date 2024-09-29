#include "ManapiUnicode.hpp"

#define BIT_AT(n, i, t) ((n >> (sizeof(t) * 8 - (i + 1))) & 1)

int manapi::net::unicode::count_of_octet(unsigned char c) {
    int i = 0;

    if (c < 128) {
        return 1;
    }

    for (; i < 8; i++) {
        if (BIT_AT(c, i, char) == 0) {
            return i;
        }
    }

    return i;
}
