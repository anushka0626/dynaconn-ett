#pragma once
#include "DynamicConnectivity.hpp"
#include <vector>
#include <unordered_set>
#include <queue>

class BaselineBFS : public DynamicConnectivity {
    int n;
    std::vector<std::unordered_set<int>> adj;

public:
    explicit BaselineBFS(int vertices) : n(vertices), adj(vertices) {}

    void insert_edge(int u, int v) override {
        if (u == v) return;
        adj[u].insert(v);
        adj[v].insert(u);
    }

    void delete_edge(int u, int v) override {
        adj[u].erase(v);
        adj[v].erase(u);
    }

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        std::vector<bool> visited(n, false);
        std::queue<int> q;
        
        q.push(u);
        visited[u] = true;

        while (!q.empty()) {
            int curr = q.front();
            q.pop();

            if (curr == v) return true;

            for (int neighbor : adj[curr]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    q.push(neighbor);
                }
            }
        }
        return false;
    }
};