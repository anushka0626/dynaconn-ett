#pragma once
#include <vector>
#include <random>
#include <set>
#include <utility>

enum class OpType { INSERT, DELETE, QUERY };

struct GraphOperation {
    OpType type;
    int u;
    int v;
};

inline std::vector<GraphOperation> generate_churn_workload(
    int num_vertices, 
    int num_operations, 
    double p_insert, 
    double p_delete, 
    uint32_t seed = 42
) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> dist_op(0.0, 1.0);
    std::uniform_int_distribution<int> dist_v(0, num_vertices - 1);

    std::vector<GraphOperation> workload;
    workload.reserve(num_operations);

    std::set<std::pair<int, int>> active_edges;

    for (int i = 0; i < num_operations; ++i) {
        double roll = dist_op(gen);
        if (roll < p_insert) {
            int u = dist_v(gen);
            int v = dist_v(gen);
            if (u != v) {
                workload.push_back({OpType::INSERT, u, v});
                active_edges.insert({std::min(u, v), std::max(u, v)});
            }
        } else if (roll < p_insert + p_delete && !active_edges.empty()) {
            // Pick a random existing edge to delete
            auto it = active_edges.begin();
            std::advance(it, gen() % active_edges.size());
            workload.push_back({OpType::DELETE, it->first, it->second});
            active_edges.erase(it);
        } else {
            int u = dist_v(gen);
            int v = dist_v(gen);
            workload.push_back({OpType::QUERY, u, v});
        }
    }
    return workload;
}