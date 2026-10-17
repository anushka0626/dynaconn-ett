#!/usr/bin/env bash
# Runs the test suite in every configuration the Phase 4 exit criteria require:
# GCC and Clang, each plain (RelWithChecks) and with ASan + UBSan.
#
#   scripts/check.sh            all configurations
#   scripts/check.sh gcc-asan   just one (gcc, gcc-asan, clang, clang-asan)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
configs=("${@:-gcc gcc-asan clang clang-asan}")
read -r -a configs <<< "${configs[*]}"

failed=()
for cfg in "${configs[@]}"; do
    case "$cfg" in
        gcc)        cxx=g++;     sanitize=OFF ;;
        gcc-asan)   cxx=g++;     sanitize=ON ;;
        clang)      cxx=clang++; sanitize=OFF ;;
        clang-asan) cxx=clang++; sanitize=ON ;;
        *) echo "unknown config: $cfg" >&2; exit 2 ;;
    esac

    dir="$root/build-check/$cfg"
    echo "=== $cfg ($cxx, sanitizers $sanitize)"
    cmake -S "$root" -B "$dir" -G Ninja -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_BUILD_TYPE=RelWithChecks -DDC_SANITIZE="$sanitize" > /dev/null
    cmake --build "$dir" > /dev/null
    if ! ctest --test-dir "$dir" --output-on-failure -j "$(nproc)"; then
        failed+=("$cfg")
    fi
done

if ((${#failed[@]})); then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "All configurations passed: ${configs[*]}"
