#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr double HIGH_ENERGY_CAP = 100000.0;      // K
constexpr double GRID_SPACING_DEFAULT = 0.15;     // A
constexpr double CUTOFF_DEFAULT = 12.8;           // A
constexpr double HARD_CORE_COEFFICIENT = 0.6;
constexpr double ANG_TO_BOHR = 1.8897259886;

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double norm(const Vec3& v) { return std::sqrt(dot(v, v)); }

double wrap_coord(double v, double box) {
    double out = std::fmod(v, box);
    if (out < 0.0) out += box;
    return out;
}

struct Lattice {
    std::array<Vec3, 3> rows{};  // a,b,c
    std::array<double, 3> abc{};
    std::array<double, 3> angles{};  // alpha,beta,gamma degrees

    Vec3 frac_to_cart(const Vec3& f) const {
        return {
            f.x * rows[0].x + f.y * rows[1].x + f.z * rows[2].x,
            f.x * rows[0].y + f.y * rows[1].y + f.z * rows[2].y,
            f.x * rows[0].z + f.y * rows[1].z + f.z * rows[2].z,
        };
    }

    Vec3 translation(int ia, int ib, int ic) const {
        return rows[0] * static_cast<double>(ia) + rows[1] * static_cast<double>(ib) + rows[2] * static_cast<double>(ic);
    }

    double volume() const {
        return std::abs(dot(rows[0], cross(rows[1], rows[2])));
    }

    std::array<double, 3> perpendicular_widths() const {
        const Vec3 a = rows[0], b = rows[1], c = rows[2];
        const double vol = volume();
        const double ha = vol / std::max(norm(cross(b, c)), 1e-12);
        const double hb = vol / std::max(norm(cross(c, a)), 1e-12);
        const double hc = vol / std::max(norm(cross(a, b)), 1e-12);
        return {ha, hb, hc};
    }
};

struct AtomSite {
    std::string symbol;
    std::string label;
    Vec3 frac;
    Vec3 cart;
};

struct Structure {
    Lattice lattice;
    std::vector<AtomSite> sites;
};

struct FFManager {
    std::unordered_map<std::string, std::string> atom_mapping;
    std::unordered_map<std::string, std::pair<double, double>> ff_params;
    bool shifted{false};
    std::string rule_source{"Default (Truncated)"};
};

struct AtomParam {
    Vec3 coord;
    double mixed_eps;
    double sig6;
    double hc_sq;
    double u_shift;
};

struct ArgConfig {
    std::string cif;
    std::string output;
    std::string probe{"Xe"};
    double spacing{GRID_SPACING_DEFAULT};
    double cutoff{CUTOFF_DEFAULT};
    int n_jobs{-1};
    int chunk_size{5000};
    std::string cell_preexpand{"off"};
};

struct CellKey {
    int x;
    int y;
    int z;
    bool operator==(const CellKey& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
};

struct CellKeyHash {
    std::size_t operator()(const CellKey& k) const noexcept {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(k.x) + 0x9e3779b97f4a7c15ull));
        mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(k.y) + 0x9e3779b97f4a7c15ull));
        mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(k.z) + 0x9e3779b97f4a7c15ull));
        return static_cast<std::size_t>(h);
    }
};

class SpatialIndex {
public:
    SpatialIndex() = default;

    void build(const std::vector<Vec3>& coords, double cell_size) {
        coords_ = &coords;
        cell_size_ = std::max(cell_size, 1e-6);
        inv_cell_size_ = 1.0 / cell_size_;
        buckets_.clear();
        buckets_.reserve(coords.size() * 2 + 1);

        for (std::size_t i = 0; i < coords.size(); ++i) {
            CellKey c = to_cell(coords[i]);
            buckets_[c].push_back(i);
        }
    }

    void query_radius(const Vec3& p, double r, std::vector<std::size_t>& out) const {
        out.clear();
        if (!coords_) return;

        const CellKey c0 = to_cell(p);
        const int d = std::max(1, static_cast<int>(std::ceil(r * inv_cell_size_)));

        for (int dx = -d; dx <= d; ++dx) {
            for (int dy = -d; dy <= d; ++dy) {
                for (int dz = -d; dz <= d; ++dz) {
                    CellKey ck{c0.x + dx, c0.y + dy, c0.z + dz};
                    auto it = buckets_.find(ck);
                    if (it == buckets_.end()) continue;
                    const auto& vec = it->second;
                    out.insert(out.end(), vec.begin(), vec.end());
                }
            }
        }
    }

private:
    CellKey to_cell(const Vec3& p) const {
        return {
            static_cast<int>(std::floor(p.x * inv_cell_size_)),
            static_cast<int>(std::floor(p.y * inv_cell_size_)),
            static_cast<int>(std::floor(p.z * inv_cell_size_)),
        };
    }

    const std::vector<Vec3>* coords_{nullptr};
    double cell_size_{1.0};
    double inv_cell_size_{1.0};
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> buckets_;
};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_ws(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> out;
    std::string t;
    while (iss >> t) out.push_back(t);
    return out;
}

std::vector<std::string> split_cif_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    bool in_single = false;
    bool in_double = false;
    for (char ch : line) {
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (!in_single && !in_double && std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

double parse_number(const std::string& token) {
    std::string t = trim(token);
    if (t.empty() || t == "." || t == "?") {
        throw std::runtime_error("Invalid numeric token: '" + token + "'");
    }
    const auto p = t.find('(');
    if (p != std::string::npos) t = t.substr(0, p);
    return std::stod(t);
}

std::string normalize_symbol(std::string raw) {
    raw = trim(raw);
    if (raw.empty()) return raw;

    std::size_t i = 0;
    while (i < raw.size() && !std::isalpha(static_cast<unsigned char>(raw[i]))) i++;
    if (i >= raw.size()) return "";

    std::string out;
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(raw[i]))));
    if (i + 1 < raw.size() && std::islower(static_cast<unsigned char>(raw[i + 1]))) {
        out.push_back(raw[i + 1]);
    }
    return out;
}

Lattice build_lattice(double a, double b, double c, double alpha_deg, double beta_deg, double gamma_deg) {
    const double alpha = alpha_deg * M_PI / 180.0;
    const double beta = beta_deg * M_PI / 180.0;
    const double gamma = gamma_deg * M_PI / 180.0;

    const double ca = std::cos(alpha), cb = std::cos(beta), cg = std::cos(gamma);
    const double sg = std::sin(gamma);
    if (std::abs(sg) < 1e-12) {
        throw std::runtime_error("Invalid lattice gamma angle (sin(gamma) too small)");
    }

    Vec3 va{a, 0.0, 0.0};
    Vec3 vb{b * cg, b * sg, 0.0};

    const double cx = c * cb;
    const double cy = c * (ca - cb * cg) / sg;
    double cz2 = c * c - cx * cx - cy * cy;
    if (cz2 < 0.0 && cz2 > -1e-10) cz2 = 0.0;
    if (cz2 < 0.0) {
        throw std::runtime_error("Invalid lattice metric: c_z^2 < 0");
    }
    Vec3 vc{cx, cy, std::sqrt(cz2)};

    Lattice lat;
    lat.rows = {va, vb, vc};
    lat.abc = {a, b, c};
    lat.angles = {alpha_deg, beta_deg, gamma_deg};
    return lat;
}

Structure parse_cif(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open CIF: " + path);
    }

    std::vector<std::string> lines;
    {
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    double a = 0.0, b = 0.0, c = 0.0;
    double alpha = 0.0, beta = 0.0, gamma = 0.0;

    auto read_tag_value = [&](const std::string& key, double& dst) {
        for (const auto& raw : lines) {
            const std::string line = trim(raw);
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind(key, 0) == 0) {
                auto toks = split_cif_tokens(line);
                if (toks.size() < 2) continue;
                dst = parse_number(toks[1]);
                return;
            }
        }
        throw std::runtime_error("Missing CIF tag: " + key);
    };

    read_tag_value("_cell_length_a", a);
    read_tag_value("_cell_length_b", b);
    read_tag_value("_cell_length_c", c);
    read_tag_value("_cell_angle_alpha", alpha);
    read_tag_value("_cell_angle_beta", beta);
    read_tag_value("_cell_angle_gamma", gamma);

    Structure st;
    st.lattice = build_lattice(a, b, c, alpha, beta, gamma);

    bool found_atom_loop = false;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (trim(lines[i]) != "loop_") continue;

        std::size_t j = i + 1;
        std::vector<std::string> headers;
        while (j < lines.size()) {
            std::string h = trim(lines[j]);
            if (!h.empty() && h[0] == '_') {
                headers.push_back(h);
                ++j;
            } else {
                break;
            }
        }

        if (headers.empty()) continue;

        int idx_fx = -1, idx_fy = -1, idx_fz = -1;
        int idx_label = -1, idx_symbol = -1, idx_occ = -1;

        for (std::size_t k = 0; k < headers.size(); ++k) {
            const std::string& h = headers[k];
            if (h == "_atom_site_fract_x") idx_fx = static_cast<int>(k);
            else if (h == "_atom_site_fract_y") idx_fy = static_cast<int>(k);
            else if (h == "_atom_site_fract_z") idx_fz = static_cast<int>(k);
            else if (h == "_atom_site_label") idx_label = static_cast<int>(k);
            else if (h == "_atom_site_type_symbol") idx_symbol = static_cast<int>(k);
            else if (h == "_atom_site_occupancy") idx_occ = static_cast<int>(k);
        }

        if (idx_fx < 0 || idx_fy < 0 || idx_fz < 0) {
            continue;
        }

        found_atom_loop = true;
        std::size_t k = j;
        for (; k < lines.size(); ++k) {
            std::string line = trim(lines[k]);
            if (line.empty() || line[0] == '#') continue;
            if (line == "loop_" || line[0] == '_') break;
            if (line[0] == ';') {
                throw std::runtime_error("Unsupported CIF multiline atom record in " + path);
            }

            auto toks = split_cif_tokens(line);
            if (toks.size() < headers.size()) continue;

            AtomSite site;
            site.label = (idx_label >= 0) ? toks[idx_label] : "";
            const std::string symbol_raw = (idx_symbol >= 0) ? toks[idx_symbol] : site.label;
            site.symbol = normalize_symbol(symbol_raw);
            if (site.symbol.empty()) {
                throw std::runtime_error("Failed to parse atom symbol from token: " + symbol_raw);
            }

            site.frac.x = parse_number(toks[idx_fx]);
            site.frac.y = parse_number(toks[idx_fy]);
            site.frac.z = parse_number(toks[idx_fz]);

            site.frac.x -= std::floor(site.frac.x);
            site.frac.y -= std::floor(site.frac.y);
            site.frac.z -= std::floor(site.frac.z);

            if (idx_occ >= 0) {
                const double occ = parse_number(toks[idx_occ]);
                if (occ < 0.9999 || occ > 1.0001) {
                    throw std::runtime_error("Disordered/partial occupancy detected at label " + site.label);
                }
            }

            site.cart = st.lattice.frac_to_cart(site.frac);
            st.sites.push_back(site);
        }

        i = k > 0 ? k - 1 : k;
    }

    if (!found_atom_loop || st.sites.empty()) {
        throw std::runtime_error("No atom loop found in CIF: " + path);
    }

    return st;
}

void load_pseudo_file(const std::string& path, FFManager& ff) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto p = split_ws(line);
        if (p.size() >= 5) ff.atom_mapping[p[0]] = p[3];
    }
}

void load_ff_file(const std::string& path, FFManager& ff) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto p = split_ws(line);
        if (p.empty()) continue;

        if (p.size() == 1) {
            if (p[0] == "shifted") {
                ff.shifted = true;
                ff.rule_source = "Line " + std::to_string(line_no) + ": 'shifted'";
            } else if (p[0] == "truncated") {
                ff.shifted = false;
                ff.rule_source = "Line " + std::to_string(line_no) + ": 'truncated'";
            }
        }

        if (p.size() >= 4) {
            bool is_lj = false;
            for (const auto& tok : p) {
                if (tok == "lennard-jones") {
                    is_lj = true;
                    break;
                }
            }
            if (is_lj) {
                ff.ff_params[p[0]] = {parse_number(p[2]), parse_number(p[3])};
            }
        }
    }
}

FFManager load_force_fields(const std::string& pseudo_path, const std::string& ff_path) {
    FFManager ff;
    load_pseudo_file(pseudo_path, ff);
    load_ff_file(ff_path, ff);
    return ff;
}

std::optional<std::string> resolve_atype(const FFManager& ff, const std::string& symbol, const std::string& label) {
    if (!label.empty()) {
        auto it_map = ff.atom_mapping.find(label);
        if (it_map != ff.atom_mapping.end()) {
            auto it_ff = ff.ff_params.find(it_map->second);
            if (it_ff != ff.ff_params.end()) return it_map->second;
        }
        auto it_ff = ff.ff_params.find(label);
        if (it_ff != ff.ff_params.end()) return label;
    }

    auto it_map = ff.atom_mapping.find(symbol);
    if (it_map != ff.atom_mapping.end()) {
        auto it_ff = ff.ff_params.find(it_map->second);
        if (it_ff != ff.ff_params.end()) return it_map->second;
    }

    auto it_ff = ff.ff_params.find(symbol);
    if (it_ff != ff.ff_params.end()) return symbol;

    return std::nullopt;
}

std::pair<double, double> get_probe_params(const FFManager& ff, const std::string& probe) {
    auto it_map = ff.atom_mapping.find(probe);
    if (it_map != ff.atom_mapping.end()) {
        auto it_ff = ff.ff_params.find(it_map->second);
        if (it_ff != ff.ff_params.end()) return it_ff->second;
    }
    auto it_ff = ff.ff_params.find(probe);
    if (it_ff != ff.ff_params.end()) return it_ff->second;

    throw std::runtime_error("Probe '" + probe + "' not found in force-field definitions");
}

Structure ensure_cell_size(const Structure& in, double cutoff, std::array<int, 3>& multipliers) {
    multipliers = {1, 1, 1};
    for (int i = 0; i < 3; ++i) {
        multipliers[i] = std::max(1, static_cast<int>(std::ceil((2.0 * cutoff) / in.lattice.abc[i])));
    }
    if (multipliers[0] == 1 && multipliers[1] == 1 && multipliers[2] == 1) {
        return in;
    }

    Structure out;
    out.lattice = in.lattice;
    out.lattice.rows[0] = out.lattice.rows[0] * static_cast<double>(multipliers[0]);
    out.lattice.rows[1] = out.lattice.rows[1] * static_cast<double>(multipliers[1]);
    out.lattice.rows[2] = out.lattice.rows[2] * static_cast<double>(multipliers[2]);
    out.lattice.abc[0] *= multipliers[0];
    out.lattice.abc[1] *= multipliers[1];
    out.lattice.abc[2] *= multipliers[2];

    out.sites.reserve(in.sites.size() * multipliers[0] * multipliers[1] * multipliers[2]);

    for (int ia = 0; ia < multipliers[0]; ++ia) {
        for (int ib = 0; ib < multipliers[1]; ++ib) {
            for (int ic = 0; ic < multipliers[2]; ++ic) {
                for (const auto& s : in.sites) {
                    AtomSite t = s;
                    t.frac.x = (s.frac.x + ia) / static_cast<double>(multipliers[0]);
                    t.frac.y = (s.frac.y + ib) / static_cast<double>(multipliers[1]);
                    t.frac.z = (s.frac.z + ic) / static_cast<double>(multipliers[2]);
                    t.cart = out.lattice.frac_to_cart(t.frac);
                    out.sites.push_back(std::move(t));
                }
            }
        }
    }

    return out;
}

std::vector<AtomParam> build_active_atoms(const Structure& st, const FFManager& ff, const std::string& probe, double cutoff) {
    const auto [probe_eps, probe_sig] = get_probe_params(ff, probe);

    std::vector<AtomParam> atoms;
    atoms.reserve(st.sites.size());

    for (const auto& s : st.sites) {
        auto atype_opt = resolve_atype(ff, s.symbol, s.label);
        if (!atype_opt) {
            throw std::runtime_error("Strict Check Failed: No parameters for atom '" + s.symbol + "' label '" + s.label + "'");
        }

        const auto& p = ff.ff_params.at(*atype_opt);
        const double eps = p.first;
        const double sig = p.second;

        double mixed_eps = 0.0;
        double mixed_sig = 1.0;
        if (probe_eps > 0.0) {
            mixed_eps = std::sqrt(eps * probe_eps);
            mixed_sig = 0.5 * (sig + probe_sig);
        }

        if (mixed_eps <= 0.0) continue;

        AtomParam a{};
        a.coord = s.cart;
        a.mixed_eps = mixed_eps;
        a.sig6 = std::pow(mixed_sig, 6.0);
        a.hc_sq = std::pow(HARD_CORE_COEFFICIENT * mixed_sig, 2.0);

        if (ff.shifted) {
            const double inv_rc2 = 1.0 / (cutoff * cutoff);
            const double inv_rc6 = inv_rc2 * inv_rc2 * inv_rc2;
            const double term6_c = a.sig6 * inv_rc6;
            const double term12_c = term6_c * term6_c;
            a.u_shift = 4.0 * a.mixed_eps * (term12_c - term6_c);
        } else {
            a.u_shift = 0.0;
        }

        atoms.push_back(a);
    }

    return atoms;
}

bool lattice_is_orthogonal(const Lattice& lat) {
    const double alpha = lat.angles[0] * M_PI / 180.0;
    const double beta = lat.angles[1] * M_PI / 180.0;
    const double gamma = lat.angles[2] * M_PI / 180.0;
    const double dev = std::max({std::abs(std::cos(alpha)), std::abs(std::cos(beta)), std::abs(std::cos(gamma))});
    return dev < 1e-5;
}

double max_offdiag_normalized(const Lattice& lat) {
    double max_off = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double n = std::max(norm(lat.rows[i]), 1e-12);
        const std::array<double, 3> c = {lat.rows[i].x / n, lat.rows[i].y / n, lat.rows[i].z / n};
        for (int j = 0; j < 3; ++j) {
            if (i == j) continue;
            max_off = std::max(max_off, std::abs(c[j]));
        }
    }
    return max_off;
}

class PotentialCalculator {
public:
    explicit PotentialCalculator(double cutoff, bool shifted)
        : cutoff_(cutoff), cutoff_sq_(cutoff * cutoff), shifted_(shifted) {}
    virtual ~PotentialCalculator() = default;

    virtual double potential_at(const Vec3& p) const = 0;
    virtual std::string semantics() const = 0;

protected:
    double lj_sum(const AtomParam& a, double r2) const {
        const double inv_r2 = 1.0 / r2;
        const double inv_r6 = inv_r2 * inv_r2 * inv_r2;
        const double term6 = a.sig6 * inv_r6;
        const double term12 = term6 * term6;
        double u = 4.0 * a.mixed_eps * (term12 - term6);
        if (shifted_) u -= a.u_shift;
        return u;
    }

    double cutoff_;
    double cutoff_sq_;
    bool shifted_;
};

class OrthogonalCalculator final : public PotentialCalculator {
public:
    OrthogonalCalculator(const Lattice& lat, std::vector<AtomParam> atoms, double cutoff, bool shifted)
        : PotentialCalculator(cutoff, shifted), lat_(lat), atoms_(std::move(atoms)) {
        Lx_ = lat_.abc[0];
        Ly_ = lat_.abc[1];
        Lz_ = lat_.abc[2];

        const double h_min = std::min({Lx_, Ly_, Lz_});
        single_image_safe_ = cutoff <= (0.5 * h_min - 1e-6);

        if (single_image_safe_) {
            semantics_ = "minimum_image";
            wrapped_coords_.reserve(atoms_.size());
            for (const auto& a : atoms_) {
                wrapped_coords_.push_back({
                    wrap_coord(a.coord.x, Lx_),
                    wrap_coord(a.coord.y, Ly_),
                    wrap_coord(a.coord.z, Lz_),
                });
            }
            index_.build(wrapped_coords_, cutoff_);
        } else {
            semantics_ = "full_image_sum";
            const int nx = std::max(1, static_cast<int>(std::ceil(cutoff / Lx_)));
            const int ny = std::max(1, static_cast<int>(std::ceil(cutoff / Ly_)));
            const int nz = std::max(1, static_cast<int>(std::ceil(cutoff / Lz_)));

            std::cout << "OrthogonalCalculator: Full-image mode (cutoff=" << std::fixed << std::setprecision(2)
                      << cutoff << " > 0.5*h_min=" << 0.5 * h_min << ")\n";
            std::cout << "  Supercell expansion: " << nx << "x" << ny << "x" << nz << " layers\n";

            const std::size_t total = atoms_.size() * static_cast<std::size_t>((2 * nx + 1) * (2 * ny + 1) * (2 * nz + 1));
            super_coords_.reserve(total);
            super_atoms_.reserve(total);

            for (int i = -nx; i <= nx; ++i) {
                for (int j = -ny; j <= ny; ++j) {
                    for (int k = -nz; k <= nz; ++k) {
                        const Vec3 t = lat_.translation(i, j, k);
                        for (const auto& a : atoms_) {
                            super_coords_.push_back(a.coord + t);
                            super_atoms_.push_back(a);
                        }
                    }
                }
            }

            index_.build(super_coords_, cutoff_);
        }
    }

    std::string semantics() const override { return semantics_; }

    double potential_at(const Vec3& p_raw) const override {
        if (atoms_.empty()) return 0.0;
        if (single_image_safe_) {
            Vec3 p{wrap_coord(p_raw.x, Lx_), wrap_coord(p_raw.y, Ly_), wrap_coord(p_raw.z, Lz_)};
            return potential_single_image(p);
        }
        return potential_full_image(p_raw);
    }

private:
    double potential_single_image(const Vec3& p) const {
        std::array<std::vector<double>, 3> axis_shifts;
        axis_shifts[0].push_back(0.0);
        axis_shifts[1].push_back(0.0);
        axis_shifts[2].push_back(0.0);

        if (p.x < cutoff_) axis_shifts[0].push_back(Lx_);
        if (p.x > Lx_ - cutoff_) axis_shifts[0].push_back(-Lx_);
        if (p.y < cutoff_) axis_shifts[1].push_back(Ly_);
        if (p.y > Ly_ - cutoff_) axis_shifts[1].push_back(-Ly_);
        if (p.z < cutoff_) axis_shifts[2].push_back(Lz_);
        if (p.z > Lz_ - cutoff_) axis_shifts[2].push_back(-Lz_);

        std::unordered_set<std::size_t> seen;
        seen.reserve(64);
        std::vector<std::size_t> candidates;

        double total = 0.0;

        for (double sx : axis_shifts[0]) {
            for (double sy : axis_shifts[1]) {
                for (double sz : axis_shifts[2]) {
                    const Vec3 q{p.x + sx, p.y + sy, p.z + sz};
                    index_.query_radius(q, cutoff_, candidates);
                    for (std::size_t idx : candidates) {
                        if (!seen.insert(idx).second) continue;

                        const Vec3& ac = wrapped_coords_[idx];
                        double dx = p.x - ac.x;
                        double dy = p.y - ac.y;
                        double dz = p.z - ac.z;

                        dx -= std::round(dx / Lx_) * Lx_;
                        dy -= std::round(dy / Ly_) * Ly_;
                        dz -= std::round(dz / Lz_) * Lz_;

                        const double r2 = dx * dx + dy * dy + dz * dz;
                        if (r2 >= cutoff_sq_) continue;

                        const AtomParam& a = atoms_[idx];
                        if (r2 < a.hc_sq) return HIGH_ENERGY_CAP;
                        total += lj_sum(a, r2);
                    }
                }
            }
        }

        return std::min(total, HIGH_ENERGY_CAP);
    }

    double potential_full_image(const Vec3& p) const {
        std::vector<std::size_t> candidates;
        index_.query_radius(p, cutoff_, candidates);

        double total = 0.0;
        for (std::size_t idx : candidates) {
            const Vec3& ac = super_coords_[idx];
            const AtomParam& a = super_atoms_[idx];
            const double dx = p.x - ac.x;
            const double dy = p.y - ac.y;
            const double dz = p.z - ac.z;
            const double r2 = dx * dx + dy * dy + dz * dz;
            if (r2 >= cutoff_sq_) continue;
            if (r2 < a.hc_sq) return HIGH_ENERGY_CAP;
            total += lj_sum(a, r2);
        }

        return std::min(total, HIGH_ENERGY_CAP);
    }

private:
    Lattice lat_;
    std::vector<AtomParam> atoms_;
    std::vector<Vec3> wrapped_coords_;
    std::vector<Vec3> super_coords_;
    std::vector<AtomParam> super_atoms_;

    double Lx_{0.0}, Ly_{0.0}, Lz_{0.0};
    bool single_image_safe_{false};
    std::string semantics_;
    SpatialIndex index_;
};

class TriclinicCalculator final : public PotentialCalculator {
public:
    TriclinicCalculator(const Lattice& lat, std::vector<AtomParam> atoms, double cutoff, bool shifted)
        : PotentialCalculator(cutoff, shifted), lat_(lat), atoms_(std::move(atoms)) {
        const auto widths = lat_.perpendicular_widths();
        const int na = std::max(1, static_cast<int>(std::ceil(cutoff / widths[0])));
        const int nb = std::max(1, static_cast<int>(std::ceil(cutoff / widths[1])));
        const int nc = std::max(1, static_cast<int>(std::ceil(cutoff / widths[2])));

        std::cout << "Triclinic Expansion: " << na << "x" << nb << "x" << nc
                  << " layers (based on widths: " << std::fixed << std::setprecision(2)
                  << widths[0] << ", " << widths[1] << ", " << widths[2] << " A)\n";

        const std::size_t n_images = static_cast<std::size_t>((2 * na + 1) * (2 * nb + 1) * (2 * nc + 1));
        if (n_images > 500) {
            std::cout << "Warning: Large supercell expansion (" << n_images << " images). Performance may degrade.\n";
        }

        super_coords_.reserve(atoms_.size() * n_images);
        super_atoms_.reserve(super_coords_.capacity());

        for (int ia = -na; ia <= na; ++ia) {
            for (int ib = -nb; ib <= nb; ++ib) {
                for (int ic = -nc; ic <= nc; ++ic) {
                    const Vec3 t = lat_.translation(ia, ib, ic);
                    for (const auto& a : atoms_) {
                        super_coords_.push_back(a.coord + t);
                        super_atoms_.push_back(a);
                    }
                }
            }
        }

        index_.build(super_coords_, cutoff_);
    }

    std::string semantics() const override { return "full_image_sum"; }

    double potential_at(const Vec3& p) const override {
        if (atoms_.empty()) return 0.0;

        std::vector<std::size_t> candidates;
        index_.query_radius(p, cutoff_, candidates);

        double total = 0.0;
        for (std::size_t idx : candidates) {
            const Vec3& ac = super_coords_[idx];
            const AtomParam& a = super_atoms_[idx];
            const double dx = p.x - ac.x;
            const double dy = p.y - ac.y;
            const double dz = p.z - ac.z;
            const double r2 = dx * dx + dy * dy + dz * dz;
            if (r2 >= cutoff_sq_) continue;
            if (r2 < a.hc_sq) return HIGH_ENERGY_CAP;
            total += lj_sum(a, r2);
        }

        return std::min(total, HIGH_ENERGY_CAP);
    }

private:
    Lattice lat_;
    std::vector<AtomParam> atoms_;
    std::vector<Vec3> super_coords_;
    std::vector<AtomParam> super_atoms_;
    SpatialIndex index_;
};

std::unique_ptr<PotentialCalculator> make_calculator(const Structure& st, const FFManager& ff, const ArgConfig& cfg,
                                                     const std::vector<AtomParam>& active_atoms,
                                                     std::string& calc_type) {
    const bool is_orth = lattice_is_orthogonal(st.lattice);
    const double offdiag = max_offdiag_normalized(st.lattice);
    const bool axis_aligned = offdiag < 1e-3;
    const double min_width = std::min({st.lattice.abc[0], st.lattice.abc[1], st.lattice.abc[2]});
    const bool large_enough = min_width >= (2.0 * cfg.cutoff);

    if (is_orth && large_enough && axis_aligned) {
        calc_type = "OrthogonalCalculator";
        return std::make_unique<OrthogonalCalculator>(st.lattice, active_atoms, cfg.cutoff, ff.shifted);
    }

    std::string reason;
    if (!is_orth) reason = "Non-orthogonal";
    else if (!large_enough) reason = "Small Cell (min_w=" + std::to_string(min_width) + " < 2*cutoff)";
    else reason = "Not axis-aligned";

    std::cout << "Factory: Using TriclinicCalculator (" << reason << ")\n";
    calc_type = "TriclinicCalculator";
    return std::make_unique<TriclinicCalculator>(st.lattice, active_atoms, cfg.cutoff, ff.shifted);
}

void write_cube(const std::string& path, const std::vector<double>& data, int nx, int ny, int nz, const Lattice& lat) {
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("Cannot write output cube: " + path);
    }

    std::array<Vec3, 3> vecs = {
        lat.rows[0] * ANG_TO_BOHR,
        lat.rows[1] * ANG_TO_BOHR,
        lat.rows[2] * ANG_TO_BOHR,
    };

    Vec3 vox_a = vecs[0] * (1.0 / static_cast<double>(nx));
    Vec3 vox_b = vecs[1] * (1.0 / static_cast<double>(ny));
    Vec3 vox_c = vecs[2] * (1.0 / static_cast<double>(nz));
    Vec3 origin = (vox_a + vox_b + vox_c) * 0.5;

    out << "Generated by PES Grid v9 (Strict+Robust)\n";
    out << "Potential Energy [K] -- Cap at " << std::uppercase << std::scientific << std::setprecision(1)
        << HIGH_ENERGY_CAP << "\n";

    out << std::defaultfloat << std::setprecision(6);
    out << std::setw(5) << 0 << " "
        << std::setw(12) << std::fixed << std::setprecision(6) << origin.x << " "
        << std::setw(12) << origin.y << " "
        << std::setw(12) << origin.z << "\n";

    auto write_axis = [&](int n, const Vec3& v) {
        out << std::setw(5) << n << " "
            << std::setw(12) << std::fixed << std::setprecision(6) << v.x << " "
            << std::setw(12) << v.y << " "
            << std::setw(12) << v.z << "\n";
    };

    write_axis(nx, vox_a);
    write_axis(ny, vox_b);
    write_axis(nz, vox_c);

    out << std::uppercase << std::scientific << std::setprecision(5);
    for (std::size_t i = 0; i < data.size(); i += 6) {
        const std::size_t end = std::min(data.size(), i + 6);
        for (std::size_t j = i; j < end; ++j) {
            if (j > i) out << " ";
            out << std::setw(13) << data[j];
        }
        out << "\n";
    }
}

void write_json_sidecar(const std::string& out_cube, const ArgConfig& cfg, const Structure& st,
                        const FFManager& ff, bool expanded, const std::array<int, 3>& supercell,
                        int nx, int ny, int nz, const std::vector<double>& grid_data,
                        const std::string& calc_type, const std::string& semantics) {
    const std::string json_path = out_cube + ".json";
    std::ofstream out(json_path);
    if (!out.is_open()) {
        throw std::runtime_error("Cannot write JSON sidecar: " + json_path);
    }

    double minv = std::numeric_limits<double>::infinity();
    double maxv = -std::numeric_limits<double>::infinity();
    std::size_t blocked = 0;
    for (double v : grid_data) {
        minv = std::min(minv, v);
        maxv = std::max(maxv, v);
        if (v >= HIGH_ENERGY_CAP) blocked++;
    }

    out << "{\n";
    out << "  \"provenance\": {\n";
    out << "    \"cif_file\": \"" << cfg.cif << "\",\n";
    out << "    \"supercell_expanded\": " << (expanded ? "true" : "false") << ",\n";
    if (expanded) {
        out << "    \"supercell_matrix\": [[" << supercell[0] << ",0,0],[0," << supercell[1] << ",0],[0,0," << supercell[2] << "]]\n";
    } else {
        out << "    \"supercell_matrix\": null\n";
    }
    out << "  },\n";

    out << "  \"settings\": {\n";
    out << "    \"probe\": \"" << cfg.probe << "\",\n";
    out << "    \"cutoff\": " << cfg.cutoff << ",\n";
    out << "    \"spacing\": " << cfg.spacing << ",\n";
    out << "    \"shifted_potential\": " << (ff.shifted ? "true" : "false") << "\n";
    out << "  },\n";

    out << "  \"grid_stats\": {\n";
    out << "    \"dimensions\": [" << nx << "," << ny << "," << nz << "],\n";
    out << "    \"min_val\": " << minv << ",\n";
    out << "    \"max_val\": " << maxv << ",\n";
    out << "    \"blocked_fraction\": " << static_cast<double>(blocked) / std::max<std::size_t>(1, grid_data.size()) << "\n";
    out << "  },\n";

    out << "  \"validation\": {\n";
    out << "    \"reference_validation_passed\": false,\n";
    out << "    \"calculator_type\": \"" << calc_type << "\",\n";
    out << "    \"calculator_semantics\": \"" << semantics << "\"\n";
    out << "  },\n";

    out << "  \"lattice\": {\n";
    out << "    \"a\": " << st.lattice.abc[0] << ", \"b\": " << st.lattice.abc[1] << ", \"c\": " << st.lattice.abc[2] << "\n";
    out << "  }\n";
    out << "}\n";
}

ArgConfig parse_args(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error(
            "Usage: generate_pes_grid_v10_cpp <cif> <output.cube> [--probe Xe] [--spacing 0.15] [--cutoff 12.8] "
            "[--n_jobs -1] [--chunk_size 5000] [--cell_preexpand off|auto]");
    }

    ArgConfig cfg;
    cfg.cif = argv[1];
    cfg.output = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string k = argv[i];
        auto require_value = [&](const std::string& key) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for argument " + key);
            return argv[++i];
        };

        if (k == "--probe") cfg.probe = require_value(k);
        else if (k == "--spacing") cfg.spacing = parse_number(require_value(k));
        else if (k == "--cutoff") cfg.cutoff = parse_number(require_value(k));
        else if (k == "--n_jobs") cfg.n_jobs = static_cast<int>(parse_number(require_value(k)));
        else if (k == "--chunk_size") cfg.chunk_size = static_cast<int>(parse_number(require_value(k)));
        else if (k == "--cell_preexpand") cfg.cell_preexpand = require_value(k);
        else throw std::runtime_error("Unknown argument: " + k);
    }

    if (cfg.spacing <= 0.0) throw std::runtime_error("--spacing must be > 0");
    if (cfg.cutoff <= 0.0) throw std::runtime_error("--cutoff must be > 0");
    if (cfg.cell_preexpand != "off" && cfg.cell_preexpand != "auto") {
        throw std::runtime_error("--cell_preexpand must be one of: off, auto");
    }

    return cfg;
}

Vec3 point_from_linear_index(std::size_t idx, int nx, int ny, int nz, const Lattice& lat) {
    const std::size_t yz = static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz);

    const std::size_t ix = idx / yz;
    const std::size_t rem = idx - ix * yz;
    const std::size_t iy = rem / static_cast<std::size_t>(nz);
    const std::size_t iz = rem - iy * static_cast<std::size_t>(nz);

    Vec3 frac{
        (static_cast<double>(ix) + 0.5) / static_cast<double>(nx),
        (static_cast<double>(iy) + 0.5) / static_cast<double>(ny),
        (static_cast<double>(iz) + 0.5) / static_cast<double>(nz),
    };

    return lat.frac_to_cart(frac);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const ArgConfig cfg = parse_args(argc, argv);

        std::cout << "Generate PES Grid v10 (C++ aligned)\n";
        std::cout << "Structure: " << cfg.cif << "\n";

        const std::string base_dir = [&]() {
            const auto pos = cfg.cif.find_last_of('/');
            return (pos == std::string::npos) ? std::string(".") : cfg.cif.substr(0, pos);
        }();

        std::string ff_file = base_dir + "/force_field_mixing_rules.def";
        std::string pseudo_file = base_dir + "/pseudo_atoms.def";

        {
            std::ifstream t(ff_file);
            if (!t.good()) ff_file = "force_field_mixing_rules.def";
        }
        {
            std::ifstream t(pseudo_file);
            if (!t.good()) pseudo_file = "pseudo_atoms.def";
        }

        FFManager ff = load_force_fields(pseudo_file, ff_file);
        if (ff.atom_mapping.empty() || ff.ff_params.empty()) {
            throw std::runtime_error("Failed to load force fields (pseudo or FF missing/invalid)");
        }

        std::cout << "ForceField: " << ff_file << "\n";
        std::cout << "Mode: " << (ff.shifted ? "Shifted" : "Truncated") << " (" << ff.rule_source << ")\n";
        std::cout << "Loaded " << ff.atom_mapping.size() << " atom mappings, " << ff.ff_params.size() << " FF parameters\n";

        Structure st0 = parse_cif(cfg.cif);
        Structure st = st0;
        std::array<int, 3> supercell{1, 1, 1};
        bool expanded = false;

        if (cfg.cell_preexpand == "auto") {
            st = ensure_cell_size(st0, cfg.cutoff, supercell);
            expanded = !(supercell[0] == 1 && supercell[1] == 1 && supercell[2] == 1);
            std::cout << "Cell pre-expansion policy: auto (legacy)\n";
            if (expanded) {
                std::cout << "Cell Expansion: " << supercell[0] << "x" << supercell[1] << "x" << supercell[2]
                          << " (expanded abc: "
                          << std::fixed << std::setprecision(2)
                          << st.lattice.abc[0] << " x " << st.lattice.abc[1] << " x " << st.lattice.abc[2] << " A)\n";
            }
        } else {
            std::cout << "Cell pre-expansion policy: off (fast path)\n";
        }

        auto probe_params = get_probe_params(ff, cfg.probe);
        std::cout << "Probe: " << cfg.probe << " (eps=" << std::fixed << std::setprecision(4)
                  << probe_params.first << " K, sig=" << probe_params.second << " A)\n";

        std::vector<AtomParam> active_atoms = build_active_atoms(st, ff, cfg.probe, cfg.cutoff);
        std::cout << "Active interacting atoms: " << active_atoms.size() << " / " << st.sites.size() << "\n";

        std::string calc_type;
        auto calc = make_calculator(st, ff, cfg, active_atoms, calc_type);

        const int nx = std::max(1, static_cast<int>(std::ceil(st.lattice.abc[0] / cfg.spacing)));
        const int ny = std::max(1, static_cast<int>(std::ceil(st.lattice.abc[1] / cfg.spacing)));
        const int nz = std::max(1, static_cast<int>(std::ceil(st.lattice.abc[2] / cfg.spacing)));

        const double sx = st.lattice.abc[0] / static_cast<double>(nx);
        const double sy = st.lattice.abc[1] / static_cast<double>(ny);
        const double sz = st.lattice.abc[2] / static_cast<double>(nz);

        std::cout << "Grid: " << nx << "x" << ny << "x" << nz << " (Target: " << cfg.spacing
                  << " A, Actual: " << std::fixed << std::setprecision(3)
                  << sx << " x " << sy << " x " << sz << " A)\n";

        const std::size_t total_points = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz);
        std::vector<double> grid(total_points, 0.0);

        int n_threads = cfg.n_jobs;
        if (n_threads <= 0) {
#ifdef _OPENMP
            n_threads = omp_get_max_threads();
#else
            n_threads = static_cast<int>(std::thread::hardware_concurrency());
            if (n_threads <= 0) n_threads = 1;
#endif
        }

#ifdef _OPENMP
        omp_set_num_threads(n_threads);
#endif

        std::cout << "Calculating on " << n_threads << " threads...\n";

#pragma omp parallel for schedule(dynamic, 256)
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(total_points); ++i) {
            const Vec3 p = point_from_linear_index(static_cast<std::size_t>(i), nx, ny, nz, st.lattice);
            grid[static_cast<std::size_t>(i)] = calc->potential_at(p);
        }

        std::size_t blocked = 0;
        for (double v : grid) {
            if (v >= HIGH_ENERGY_CAP) blocked++;
        }
        std::cout << "Blocked Volume: " << std::fixed << std::setprecision(2)
                  << 100.0 * static_cast<double>(blocked) / std::max<std::size_t>(1, total_points) << "%\n";

        write_cube(cfg.output, grid, nx, ny, nz, st.lattice);
        write_json_sidecar(cfg.output, cfg, st, ff, expanded, supercell, nx, ny, nz, grid,
                           calc_type, calc->semantics());

        std::cout << "Wrote cube: " << cfg.output << "\n";
        std::cout << "Metadata written to " << cfg.output << ".json\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
