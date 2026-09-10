#include "key_combo_sequence.h"

#include <cstdlib>
#include <cstring>

namespace key_combo {

void Generate(int sequence[kLength], const int rejected[kLength]) {
    do {
        int lastSymbol = -1;
        for (int i = 0; i < kLength - 1; ++i) {
            int symbol = lastSymbol;
            while (symbol == lastSymbol) {
                symbol = std::rand() % 4;
            }
            sequence[i] = symbol;
            lastSymbol = symbol;
        }
        sequence[kLength - 1] = 4;
    } while (rejected
        && std::memcmp(sequence, rejected, sizeof(int) * kLength) == 0);
}

} // namespace key_combo
