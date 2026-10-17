#pragma once
#include <cstddef>

// Common interface for every structure. Vertex ids are ints in [0, n).
// Self-loops are ignored; inserting an existing edge or deleting an absent one is a no-op.
class DynamicConnectivity {
public:
    virtual ~DynamicConnectivity() = default;
    virtual void insert_edge(int u, int v) = 0;
    virtual void delete_edge(int u, int v) = 0;
    virtual bool is_connected(int u, int v) = 0;
    virtual size_t edge_count() const = 0;  // distinct undirected edges currently present
};
