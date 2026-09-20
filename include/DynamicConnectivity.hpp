#pragma once
#include <cstddef>

class DynamicConnectivity {
public:
    virtual ~DynamicConnectivity() = default;
    virtual void insert_edge(int u, int v) = 0;
    virtual void delete_edge(int u, int v) = 0;
    virtual bool is_connected(int u, int v) = 0;
};