#pragma once
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Hardware counters via perf_event_open (plan 7.3), counting user space only, for this
// thread. Requires perf_event_paranoid <= 2.
//
// On hybrid Intel CPUs (P-cores + E-cores), generic events must name the PMU: the cpu_core
// PMU's type id goes into the top 32 bits of the config (PERF_PMU_TYPE_SHIFT). On other
// CPUs (e.g. AMD), the extension is 0. Events that fail to open are reported as
// unavailable rather than failing the run. If the kernel had to multiplex the counters
// (time_running < time_enabled), values are scaled and the run is flagged.

namespace dc {

struct CounterSpec {
    const char* name;
    uint32_t type;
    uint64_t config;
};

class PerfCounters {
public:
    PerfCounters() {
        const uint64_t ext = static_cast<uint64_t>(core_pmu_type()) << 32;
        auto cache = [](uint64_t id, uint64_t op, uint64_t result) { return id | (op << 8) | (result << 16); };
        const CounterSpec specs[] = {
            {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
            {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
            {"branch_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
            {"l1d_load_misses", PERF_TYPE_HW_CACHE,
             cache(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)},
            {"llc_loads", PERF_TYPE_HW_CACHE,
             cache(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS)},
            {"llc_load_misses", PERF_TYPE_HW_CACHE,
             cache(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)},
            {"dtlb_load_misses", PERF_TYPE_HW_CACHE,
             cache(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)},
        };
        for (const auto& s : specs) {
            perf_event_attr attr;
            std::memset(&attr, 0, sizeof attr);
            attr.size = sizeof attr;
            attr.type = s.type;
            attr.config = s.config | ext;
            attr.disabled = leader < 0 ? 1 : 0;
            attr.exclude_kernel = 1;
            attr.exclude_hv = 1;
            attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            int fd = static_cast<int>(syscall(__NR_perf_event_open, &attr, 0, -1, leader, 0));
            names.push_back(s.name);
            if (fd < 0) {
                fds.push_back(-1);
                continue;
            }
            if (leader < 0) leader = fd;
            fds.push_back(fd);
        }
    }

    ~PerfCounters() {
        for (int fd : fds)
            if (fd >= 0) close(fd);
    }
    PerfCounters(const PerfCounters&) = delete;
    PerfCounters& operator=(const PerfCounters&) = delete;

    bool available() const { return leader >= 0; }
    const std::vector<std::string>& event_names() const { return names; }

    void start() {
        if (leader < 0) return;
        ioctl(leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }

    // Stops counting and returns one value per event (-1: unavailable). Sets `multiplexed`
    // if the group was not on the PMU the whole time (values are then scaled).
    std::vector<int64_t> stop(bool& multiplexed) {
        std::vector<int64_t> out(names.size(), -1);
        multiplexed = false;
        if (leader < 0) return out;
        ioctl(leader, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        std::vector<uint64_t> buf(3 + names.size());
        ssize_t got = read(leader, buf.data(), buf.size() * sizeof(uint64_t));
        if (got < static_cast<ssize_t>(3 * sizeof(uint64_t))) return out;
        uint64_t nr = buf[0], enabled = buf[1], running = buf[2];
        double scale = running > 0 && running < enabled ? static_cast<double>(enabled) / running : 1.0;
        multiplexed = running < enabled;
        size_t k = 0;  // group values come in the order the open events joined
        for (size_t i = 0; i < names.size() && k < nr; ++i) {
            if (fds[i] < 0) continue;
            out[i] = static_cast<int64_t>(static_cast<double>(buf[3 + k]) * scale);
            ++k;
        }
        return out;
    }

    // The cpu_core PMU's type id on hybrid Intel CPUs; 0 elsewhere (no extended type).
    static uint32_t core_pmu_type() {
        std::ifstream f("/sys/bus/event_source/devices/cpu_core/type");
        uint32_t t = 0;
        if (f >> t) return t;
        return 0;
    }

private:
    std::vector<std::string> names;
    std::vector<int> fds;
    int leader = -1;
};

}  // namespace dc
