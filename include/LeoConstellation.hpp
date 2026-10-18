#pragma once
#include "Workloads.hpp"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// LEO constellation workload (plan 6.5): inter-satellite link (ISL) churn in a Walker-delta
// constellation, e.g. 72 planes x 22 satellites (1,584, Starlink shell 1's layout) at 53 deg
// inclination and 550 km altitude.
//
// Topology: "+Grid". Each satellite links to its two in-plane neighbors (always up) and to
// the same-index satellite in the next plane (index shifted by the phasing F across the
// last-to-first plane wrap). A cross-plane link is up while both satellites are within
// +-lat_cut of the equator and within max_range of each other. Above lat_cut the planes
// converge and the relative motion is too fast for pointing, so these links switch off.
// Circular orbits in an inertial frame; Earth rotation does not affect ISLs.
//
// This is a model, not Starlink's actual link schedule. Two caveats to state in the paper:
//  - in-plane rings are always up, so the graph stays connected and every query answers
//    true (connectivity is a building block for routing; it is not routing);
//  - positions use sin/cos, which can differ in the last bit between math libraries, so LEO
//    traces are identified by their hash and archived, not assumed to regenerate on every
//    platform. Same machine and toolchain: bit-identical.

namespace dc::wl {

struct LeoSpec {
    int planes = 72;
    int sats_per_plane = 22;
    int phasing = 1;               // Walker F
    double inclination_deg = 53.0;
    double altitude_km = 550.0;
    double lat_cut_deg = 45.0;     // cross-plane links off above this latitude
    double max_range_km = 5000.0;
    double dt_s = 10.0;            // time step
    int64_t steps = 600;           // ~1 orbit at 550 km is ~573 steps of 10 s
    double p_query = 0.5;          // fraction of operations that are (uniform) queries
};

class LeoModel {
public:
    explicit LeoModel(const LeoSpec& s) : spec(s) {
        if (s.planes < 3 || s.sats_per_plane < 3) throw std::invalid_argument("LEO: need >= 3 planes and >= 3 sats per plane");  // fewer would duplicate links
        const double pi = 3.14159265358979323846;
        const double earth_radius_km = 6371.0, mu = 398600.4418;  // km^3 / s^2
        a = earth_radius_km + s.altitude_km;
        mean_motion = std::sqrt(mu / (a * a * a));
        incl = s.inclination_deg * pi / 180.0;
        z_cut = a * std::sin(s.lat_cut_deg * pi / 180.0);
        total = s.planes * s.sats_per_plane;
        two_pi = 2.0 * pi;
    }

    int num_satellites() const { return total; }
    int id(int plane, int sat) const { return plane * spec.sats_per_plane + sat; }

    // Cross-plane partner of (plane, sat) in plane + 1.
    int partner(int plane, int sat) const {
        int s = spec.sats_per_plane;
        if (plane + 1 < spec.planes) return id(plane + 1, sat);
        return id(0, (sat + spec.phasing) % s);
    }

    void position(int plane, int sat, double t, double& x, double& y, double& z) const {
        double raan = two_pi * plane / spec.planes;
        double theta = two_pi * sat / spec.sats_per_plane + two_pi * spec.phasing * plane / total + mean_motion * t;
        double ct = std::cos(theta), st = std::sin(theta);
        double co = std::cos(raan), so = std::sin(raan);
        double ci = std::cos(incl), si = std::sin(incl);
        x = a * (co * ct - so * st * ci);
        y = a * (so * ct + co * st * ci);
        z = a * (st * si);
    }

    // Up/down state of every cross-plane link at time t; link i belongs to satellite i.
    std::vector<uint8_t> cross_links_up(double t) const {
        std::vector<double> px(total), py(total), pz(total);
        for (int p = 0; p < spec.planes; ++p)
            for (int s = 0; s < spec.sats_per_plane; ++s) position(p, s, t, px[id(p, s)], py[id(p, s)], pz[id(p, s)]);
        std::vector<uint8_t> up(total);
        const double range2 = spec.max_range_km * spec.max_range_km;
        for (int p = 0; p < spec.planes; ++p)
            for (int s = 0; s < spec.sats_per_plane; ++s) {
                int i = id(p, s), j = partner(p, s);
                double dx = px[i] - px[j], dy = py[i] - py[j], dz = pz[i] - pz[j];
                bool polar = std::fabs(pz[i]) > z_cut || std::fabs(pz[j]) > z_cut;
                up[i] = !polar && dx * dx + dy * dy + dz * dz <= range2;
            }
        return up;
    }

    Edge cross_edge(int i) const { return norm(i, partner(i / spec.sats_per_plane, i % spec.sats_per_plane)); }

    std::vector<Edge> ring_edges() const {
        std::vector<Edge> out;
        for (int p = 0; p < spec.planes; ++p)
            for (int s = 0; s < spec.sats_per_plane; ++s) out.push_back(norm(id(p, s), id(p, (s + 1) % spec.sats_per_plane)));
        return out;
    }

private:
    LeoSpec spec;
    double a, mean_motion, incl, z_cut, two_pi;
    int total;
};

// Build phase: rings plus the cross-plane links up at t = 0. Measured phase: at each later
// step, deletes for links that went down and inserts for links that came up (in satellite
// order), each followed by uniform queries with probability p_query each (geometric).
inline Trace leo_trace(std::string meta, uint64_t seed, const LeoSpec& spec) {
    LeoModel model(spec);
    const int n = model.num_satellites();
    Trace t;
    t.num_vertices = static_cast<uint32_t>(n);
    t.seed = seed;
    t.meta = std::move(meta);
    GroundTruth gt(n);
    SplitMix64 upd = stream(seed, Stream::Update), qry = stream(seed, Stream::Query), mix = stream(seed, Stream::Mix);
    auto queries = [&] {
        while (mix.uniform01() < spec.p_query) t.measured.push_back(make_query(QueryKind::Uniform, n, 2, qry, gt));
    };

    for (const Edge& e : model.ring_edges()) {
        gt.insert(e);
        t.build.push_back(update_record(TraceOp::Insert, e, upd));
    }
    std::vector<uint8_t> up = model.cross_links_up(0.0);
    for (int i = 0; i < n; ++i)
        if (up[i]) {
            gt.insert(model.cross_edge(i));
            t.build.push_back(update_record(TraceOp::Insert, model.cross_edge(i), upd));
        }

    for (int64_t step = 1; step <= spec.steps; ++step) {
        std::vector<uint8_t> now = model.cross_links_up(static_cast<double>(step) * spec.dt_s);
        for (int i = 0; i < n; ++i) {
            if (now[i] == up[i]) continue;
            Edge e = model.cross_edge(i);
            if (now[i]) gt.insert(e);
            else gt.erase(e);
            t.measured.push_back(update_record(now[i] ? TraceOp::Insert : TraceOp::Delete, e, upd));
            queries();
        }
        up.swap(now);
    }
    return t;
}

}  // namespace dc::wl
