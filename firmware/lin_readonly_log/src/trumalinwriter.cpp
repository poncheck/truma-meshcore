#include "trumalinwriter.hpp"
#include "trumalin.hpp"
#include <cstring>

namespace trumalinwriter {

size_t build_answer_bytes(uint8_t pid, uint8_t raw_pid, const uint8_t payload[8],
                            uint8_t *out, size_t out_capacity) {
    if (out_capacity < 9) return 0;

    memcpy(out, payload, 8);

    if (pid == trumalin::PID_TRANSPORTLAYER_MASTER2SLAVE ||
        pid == trumalin::PID_TRANSPORTLAYER_SLAVE2MASTER) {
        // Classic checksum: covers only the 8 data bytes, PID excluded.
        out[8] = trumalin::calculate_checksum(payload, 8);
    } else {
        // Extended checksum: covers raw_pid + 8 data bytes.
        uint8_t with_pid[9];
        with_pid[0] = raw_pid;
        memcpy(&with_pid[1], payload, 8);
        out[8] = trumalin::calculate_checksum(with_pid, 9);
    }

    return 9;
}

} // namespace trumalinwriter
