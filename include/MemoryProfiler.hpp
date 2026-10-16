#pragma once
#include <cstddef>
#include <iostream>
#include <iomanip>
#include <string>

struct MemoryReport {
    std::string name;
    size_t total_bytes;
    size_t active_edges;
    double bytes_per_edge;
};

inline void print_memory_report(const MemoryReport& report) {
    std::cout << std::left << std::setw(25) << report.name 
              << " | Total: " << std::setw(8) << (report.total_bytes / 1024.0) << " KB"
              << " | Bytes/Edge: " << std::setw(8) << std::fixed << std::setprecision(2) << report.bytes_per_edge 
              << "\n";
}