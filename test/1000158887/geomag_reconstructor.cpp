// geomag_reconstructor.cpp

#include "geomag_reconstructor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>

// ---------- TimeStamp ----------
time_t TimeStamp::ToUnixTime() const {
    struct tm t = {};
    t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
    t.tm_hour = hour; t.tm_min = minute; t.tm_sec = second;
    time_t unixt = mktime(&t);
    if (base == TimeBase::BJT) unixt -= 8 * 3600;
    return unixt;
}

TimeStamp TimeStamp::FromUnixTime(time_t t, TimeBase b) {
    if (b == TimeBase::BJT) t += 8 * 3600;
    struct tm* gt = gmtime(&t);
    return TimeStamp(gt->tm_year + 1900, gt->tm_mon + 1, gt->tm_mday,
                     gt->tm_hour, gt->tm_min, gt->tm_sec, b);
}

TimeStamp TimeStamp::ToBase(TimeBase target_base) const {
    if (base == target_base) return *this;
    time_t ut = ToUnixTime();
    return FromUnixTime(ut, target_base);
}

std::string TimeStamp::ToIAGAString() const {
    char buf[32];
    std::sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d.000", year, month, day, hour, minute, second);
    return std::string(buf);
}

std::string TimeStamp::ToFString(TimeBase output_base) const {
    TimeStamp out = ToBase(output_base);
    char buf[32];
    std::sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d", out.year, out.month, out.day,
                 out.hour, out.minute, out.second);
    return std::string(buf);
}

bool TimeStamp::operator<(const TimeStamp& other) const {
    return ToUnixTime() < other.ToUnixTime();
}

bool TimeStamp::operator==(const TimeStamp& other) const {
    return year == other.year && month == other.month && day == other.day &&
           hour == other.hour && minute == other.minute && second == other.second;
}

// ---------- GeomagPoint ----------
GeomagPoint::GeomagPoint()
    : d(0), h(0), z_dhz(0), x(0), y(0), z(0), f(0),
      valid_d(false), valid_h(false), valid_z_dhz(false),
      valid_x(false), valid_y(false), valid_z(false), valid_f(false),
      u_x(0), u_y(0), u_z(0), u_f(0) {
    for (int i = 0; i < 4; ++i) u_valid[i] = false;
}

void GeomagPoint::ComputeUnified() {
    if (valid_x && valid_y && valid_z) {
        u_x = x; u_y = y; u_z = z;
        u_valid[0] = u_valid[1] = u_valid[2] = true;
        if (valid_f) { u_f = f; u_valid[3] = true; }
        else { u_f = CoordinateConverter::XYZtoF(x, y, z); u_valid[3] = true; }
    } else if (valid_d && valid_h && valid_z_dhz) {
        CoordinateConverter::DHZtoXYZ(d, h, z_dhz, u_x, u_y, u_z);
        u_valid[0] = u_valid[1] = u_valid[2] = true;
        if (valid_f) { u_f = f; u_valid[3] = true; }
        else { u_f = CoordinateConverter::DHZtoF(h, z_dhz); u_valid[3] = true; }
    }
}

// ---------- CoordinateConverter ----------
void CoordinateConverter::DHZtoXYZ(float d_deg, float h, float z_in, float& x, float& y, float& z_out) {
    float rad = d_deg * 3.14159265358979f / 180.0f;
    x = h * std::cos(rad);
    y = h * std::sin(rad);
    z_out = z_in;
}

void CoordinateConverter::XYZtoDHZ(float x, float y, float z, float& d_deg, float& h, float& z_out) {
    h = std::sqrt(x * x + y * y);
    d_deg = std::atan2(y, x) * 180.0f / 3.14159265358979f;
    if (d_deg < 0) d_deg += 360.0f;
    z_out = z;
}

float CoordinateConverter::XYZtoF(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

float CoordinateConverter::DHZtoF(float h, float z) {
    return std::sqrt(h * h + z * z);
}

// ---------- WeightCalculator ----------
float WeightCalculator::ComputeMetric(const GeoCoord& a, const GeoCoord& b) {
    if (!a.valid || !b.valid) return 1e6f;
    float dlat = a.lat - b.lat;
    float dlon = a.lon - b.lon;
    // 纬度差贡献是经度差的10倍
    return std::sqrt((10.0f * dlat) * (10.0f * dlat) + dlon * dlon);
}

void WeightCalculator::ComputeWeights(std::vector<StationData>& stations, const GeoCoord& target) {
    float total_inv = 0.0f;
    for (auto& s : stations) {
        s.lat_diff_metric = std::abs(s.coord.lat - target.lat);
        s.lon_diff_metric = std::abs(s.coord.lon - target.lon);
        float metric = ComputeMetric(s.coord, target);
        if (metric > 0.001f) {
            s.weight = 1.0f / metric;
            total_inv += s.weight;
        } else {
            s.weight = 0.0f;
        }
    }
    if (total_inv > 0.0f) {
        for (auto& s : stations) s.weight /= total_inv;
    }
}

// ---------- IAGA2000Parser ----------
bool IAGA2000Parser::Parse(const std::string& filename, StationData& out_station) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return false;
    }

    std::map<std::string, int> col_map;
    if (!ParseHeader(in, out_station, col_map)) {
        std::cerr << "Failed to parse header: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        TimeStamp ts;
        GeomagPoint pt;
        if (ParseDataLine(line, col_map, out_station.coord_type, ts, pt)) {
            out_station.times.push_back(ts);
            out_station.points.push_back(pt);
        }
    }

    for (auto& pt : out_station.points) {
        pt.ComputeUnified();
    }

    return !out_station.points.empty();
}

bool IAGA2000Parser::ParseHeader(std::ifstream& in, StationData& station, std::map<std::string, int>& col_map) {
    std::string line;
    bool reported_found = false;
    std::vector<std::string> reported_cols;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::istringstream iss(line.substr(1));
            std::string key;
            iss >> key;
            if (key == "Reported") {
                std::string col;
                while (iss >> col) reported_cols.push_back(col);
                reported_found = true;
            } else if (key == "Station") {
                std::string name;
                std::getline(iss, name);
                station.name = name;
            } else if (key == "IAGA") {
                std::string code;
                iss >> code;
                station.iaga_code = code;
            } else if (key == "Geodetic") {
                std::string subkey;
                iss >> subkey;
                float val;
                iss >> val;
                if (subkey == "Latitude") station.coord.lat = val;
                else if (subkey == "Longitude") station.coord.lon = val;
            } else if (key == "Elevation") {
                float val;
                iss >> val;
                station.coord.elev = val;
            }
        } else {
            if (line.find("DATE") != std::string::npos || line.find("DOY") != std::string::npos) {
                std::istringstream iss(line);
                std::string token;
                int idx = 0;
                while (iss >> token) {
                    if (token == "D" || token == "H" || token == "Z" ||
                        token == "X" || token == "Y" || token == "F") {
                        col_map[token] = idx;
                    }
                    idx++;
                }
                if (!reported_found && !col_map.empty()) {
                    if (col_map.count("X") || col_map.count("Y")) station.coord_type = CoordType::XYZ;
                    else if (col_map.count("D") || col_map.count("H")) station.coord_type = CoordType::DHZ;
                }
                // 若文件头已提供坐标，标记为有效
                if (station.coord.lat != 0 || station.coord.lon != 0) {
                    station.coord.valid = true;
                }
                return true;
            }
            if (reported_found && !line.empty() && line[0] != '#') {
                int base_idx = 3;
                for (size_t i = 0; i < reported_cols.size(); ++i) {
                    col_map[reported_cols[i]] = base_idx + i;
                }
                if (col_map.count("X") || col_map.count("Y")) station.coord_type = CoordType::XYZ;
                else if (col_map.count("D") || col_map.count("H")) station.coord_type = CoordType::DHZ;
                if (station.coord.lat != 0 || station.coord.lon != 0) {
                    station.coord.valid = true;
                }
                return true;
            }
        }
    }
    return false;
}

bool IAGA2000Parser::ParseDataLine(const std::string& line, const std::map<std::string, int>& col_map,
                                   CoordType ctype, TimeStamp& ts, GeomagPoint& pt) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    if (tokens.size() < 4) return false;

    std::string date_str = tokens[0];
    std::string time_str = tokens[1];

    int y, m, d, H, M;
    float S;
    if (std::sscanf(date_str.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
    if (std::sscanf(time_str.c_str(), "%d:%d:%f", &H, &M, &S) != 3) return false;

    ts = TimeStamp(y, m, d, H, M, (int)S, TimeBase::UTC);

    auto getv = [&](const std::string& key, bool& ok) -> float {
        ok = false;
        auto it = col_map.find(key);
        if (it == col_map.end()) return 0;
        int idx = it->second;
        if (idx >= (int)tokens.size()) return 0;
        try {
            float v = std::stof(tokens[idx]);
            if (!IsMissingValue(v)) { ok = true; return v; }
        } catch (...) {}
        return 0;
    };

    bool ok;
    if (ctype == CoordType::DHZ) {
        bool vd = false, vh = false, vz = false, vf = false;
        float dv = getv("D", vd);
        float hv = getv("H", vh);
        float zv = getv("Z", vz);
        float fv = getv("F", vf);
        if (vd) { pt.d = dv; pt.valid_d = true; }
        if (vh) { pt.h = hv; pt.valid_h = true; }
        if (vz) { pt.z_dhz = zv; pt.valid_z_dhz = true; }
        if (vf) { pt.f = fv; pt.valid_f = true; }
    } else if (ctype == CoordType::XYZ) {
        bool vx = false, vy = false, vz = false, vf = false;
        float xv = getv("X", vx);
        float yv = getv("Y", vy);
        float zv = getv("Z", vz);
        float fv = getv("F", vf);
        if (vx) { pt.x = xv; pt.valid_x = true; }
        if (vy) { pt.y = yv; pt.valid_y = true; }
        if (vz) { pt.z = zv; pt.valid_z = true; }
        if (vf) { pt.f = fv; pt.valid_f = true; }
    }

    return true;
}

bool IAGA2000Parser::IsMissingValue(float v) {
    return (std::abs(v - 99999.0f) < 0.5f || std::abs(v - 88888.0f) < 0.5f ||
            std::abs(v - 99999.99f) < 0.5f || std::abs(v) > 99990.0f);
}

// ---------- FFormatParser ----------
bool FFormatParser::Parse(const std::string& filename,
                          std::vector<TimeStamp>& times,
                          std::vector<float>& f_values) {
    std::ifstream in(filename);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        int y, m, d, H, M, S;
        float f;
        if (std::sscanf(line.c_str(), "%d/%d/%d %d:%d:%d %f", &y, &m, &d, &H, &M, &S, &f) == 7) {
            times.emplace_back(y, m, d, H, M, S, TimeBase::BJT);
            f_values.push_back(f);
        }
    }
    return !times.empty();
}

// ---------- FeatureVector ----------
FeatureVector::FeatureVector(int n_stations) { Resize(n_stations); }

void FeatureVector::Resize(int n_stations) {
    int n = n_stations * 4;
    features.assign(n, std::numeric_limits<float>::quiet_NaN());
    valid.assign(n, false);
    fully_valid = false;
    missing_count = 0;
}

// ---------- MissingValueProcessor ----------
bool MissingValueProcessor::TemporalInterpolate(StationData& station, int feature_idx, int current_idx) {
    int n = station.points.size();
    if (n < 2) return false;

    auto is_valid = [&](int idx) -> bool {
        if (idx < 0 || idx >= n) return false;
        return station.points[idx].u_valid[feature_idx] && !std::isnan(
            (feature_idx == 0) ? station.points[idx].u_x :
            (feature_idx == 1) ? station.points[idx].u_y :
            (feature_idx == 2) ? station.points[idx].u_z : station.points[idx].u_f);
    };

    auto getv = [&](int idx) -> float {
        const GeomagPoint& pt = station.points[idx];
        if (feature_idx == 0) return pt.u_x;
        if (feature_idx == 1) return pt.u_y;
        if (feature_idx == 2) return pt.u_z;
        return pt.u_f;
    };

    auto setv = [&](int idx, float v) {
        GeomagPoint& pt = station.points[idx];
        if (feature_idx == 0) { pt.u_x = v; pt.u_valid[0] = true; }
        else if (feature_idx == 1) { pt.u_y = v; pt.u_valid[1] = true; }
        else if (feature_idx == 2) { pt.u_z = v; pt.u_valid[2] = true; }
        else { pt.u_f = v; pt.u_valid[3] = true; }
    };

    int prev_idx = -1;
    for (int i = current_idx - 1; i >= 0; --i) { if (is_valid(i)) { prev_idx = i; break; } }

    int next_idx = -1;
    for (int i = current_idx + 1; i < n; ++i) { if (is_valid(i)) { next_idx = i; break; } }

    if (prev_idx >= 0 && next_idx >= 0) {
        int gap = next_idx - prev_idx;
        if (gap <= MAX_GAP_SEC) {
            float t = (float)(current_idx - prev_idx) / gap;
            setv(current_idx, getv(prev_idx) + t * (getv(next_idx) - getv(prev_idx)));
            return true;
        }
    }
    if (prev_idx >= 0 && (current_idx - prev_idx) <= MAX_GAP_SEC) {
        setv(current_idx, getv(prev_idx)); return true;
    }
    if (next_idx >= 0 && (next_idx - current_idx) <= MAX_GAP_SEC) {
        setv(current_idx, getv(next_idx)); return true;
    }
    return false;
}

bool MissingValueProcessor::SpatialInterpolate(const std::vector<StationData>& stations,
                                               int missing_station_idx, int feature_idx,
                                               int current_idx, float& result) {
    float weighted_sum = 0.0f;
    float weight_sum = 0.0f;
    int valid_count = 0;

    for (size_t i = 0; i < stations.size(); ++i) {
        if ((int)i == missing_station_idx) continue;
        const GeomagPoint& pt = stations[i].points[current_idx];
        if (!pt.u_valid[feature_idx]) continue;
        float v = (feature_idx == 0) ? pt.u_x : (feature_idx == 1) ? pt.u_y :
                  (feature_idx == 2) ? pt.u_z : pt.u_f;
        if (std::isnan(v)) continue;
        float w = stations[i].weight;
        weighted_sum += w * v;
        weight_sum += w;
        valid_count++;
    }

    if (valid_count >= 2 && weight_sum > 0) {
        result = weighted_sum / weight_sum;
        return true;
    }
    return false;
}

std::vector<FeatureVector> MissingValueProcessor::Process(const std::vector<StationData>& stations_raw,
                                                            std::vector<bool>& reconstructable) {
    int n_stations = stations_raw.size();
    if (n_stations == 0 || stations_raw[0].points.empty()) {
        reconstructable.clear();
        return {};
    }

    int n = stations_raw[0].points.size();
    for (size_t s = 1; s < stations_raw.size(); ++s) {
        if ((int)stations_raw[s].points.size() != n) {
            std::cerr << "Data length mismatch" << std::endl;
            reconstructable.clear();
            return {};
        }
    }

    std::vector<StationData> stations = stations_raw;

    for (int t = 0; t < n; ++t) {
        for (int s = 0; s < n_stations; ++s) {
            for (int f = 0; f < 4; ++f) {
                if (!stations[s].points[t].u_valid[f]) {
                    TemporalInterpolate(stations[s], f, t);
                }
            }
        }
    }

    std::vector<FeatureVector> features(n, FeatureVector(n_stations));
    reconstructable.assign(n, true);

    for (int t = 0; t < n; ++t) {
        int missing_count = 0;
        for (int s = 0; s < n_stations; ++s) {
            for (int f = 0; f < 4; ++f) {
                int idx = s * 4 + f;
                const GeomagPoint& pt = stations[s].points[t];
                bool ok = pt.u_valid[f];
                float v = (f == 0) ? pt.u_x : (f == 1) ? pt.u_y : (f == 2) ? pt.u_z : pt.u_f;
                if (ok && !std::isnan(v)) {
                    features[t].features[idx] = v;
                    features[t].valid[idx] = true;
                } else {
                    float interpolated;
                    if (SpatialInterpolate(stations, s, f, t, interpolated)) {
                        features[t].features[idx] = interpolated;
                        features[t].valid[idx] = false;
                        missing_count++;
                    } else {
                        features[t].features[idx] = std::numeric_limits<float>::quiet_NaN();
                        features[t].valid[idx] = false;
                        missing_count++;
                        reconstructable[t] = false;
                    }
                }
            }
        }
        features[t].missing_count = missing_count;
        features[t].fully_valid = (missing_count == 0);
    }

    return features;
}

// ---------- XGBoostGeomagPredictor ----------
XGBoostGeomagPredictor::XGBoostGeomagPredictor()
    : booster_x_(nullptr), booster_y_(nullptr), booster_z_(nullptr), booster_f_(nullptr),
      initialized_(false) {}

XGBoostGeomagPredictor::~XGBoostGeomagPredictor() {
    if (booster_x_) XGBoosterFree(booster_x_);
    if (booster_y_) XGBoosterFree(booster_y_);
    if (booster_z_) XGBoosterFree(booster_z_);
    if (booster_f_) XGBoosterFree(booster_f_);
}

bool XGBoostGeomagPredictor::LoadModels(const ReconConfig& config) {
    if (config.target_mode == TargetMode::Full) {
        if (XGBoosterCreate(nullptr, 0, &booster_x_) != 0) return false;
        if (XGBoosterLoadModel(booster_x_, config.model_x.c_str()) != 0) return false;
        if (XGBoosterCreate(nullptr, 0, &booster_y_) != 0) return false;
        if (XGBoosterLoadModel(booster_y_, config.model_y.c_str()) != 0) return false;
        if (XGBoosterCreate(nullptr, 0, &booster_z_) != 0) return false;
        if (XGBoosterLoadModel(booster_z_, config.model_z.c_str()) != 0) return false;
    } else {
        if (XGBoosterCreate(nullptr, 0, &booster_f_) != 0) return false;
        if (XGBoosterLoadModel(booster_f_, config.model_f.c_str()) != 0) return false;
    }
    initialized_ = true;
    return true;
}

bool XGBoostGeomagPredictor::PredictComponent(BoosterHandle booster, const FeatureVector& fv, float& output) {
    if (!initialized_) return false;
    for (size_t i = 0; i < fv.features.size(); ++i) {
        if (std::isnan(fv.features[i])) return false;
    }

    DMatrixHandle dmat;
    if (XGDMatrixCreateFromMat(fv.features.data(), 1, fv.features.size(), -1.0f, &dmat) != 0) return false;

    bst_ulong out_len;
    const float* out_result;
    if (XGBoosterPredict(booster, dmat, 0, 0, 0, &out_len, &out_result) != 0) {
        XGDMatrixFree(dmat);
        return false;
    }
    output = out_result[0];
    XGDMatrixFree(dmat);
    return true;
}

bool XGBoostGeomagPredictor::PredictXYZ(const FeatureVector& fv, float& x_out, float& y_out, float& z_out) {
    bool ok = true;
    ok &= PredictComponent(booster_x_, fv, x_out);
    ok &= PredictComponent(booster_y_, fv, y_out);
    ok &= PredictComponent(booster_z_, fv, z_out);
    return ok;
}

bool XGBoostGeomagPredictor::PredictF(const FeatureVector& fv, float& f_out) {
    return PredictComponent(booster_f_, fv, f_out);
}

bool XGBoostGeomagPredictor::PredictBatch(const std::vector<FeatureVector>& features,
                                          const std::vector<bool>& reconstructable,
                                          std::vector<GeomagPoint>& outputs,
                                          std::vector<bool>& predicted_flags,
                                          TargetMode mode) {
    int n = features.size();
    outputs.resize(n);
    predicted_flags.resize(n, false);

    for (int i = 0; i < n; ++i) {
        if (!reconstructable[i]) {
            outputs[i] = GeomagPoint();
            predicted_flags[i] = false;
            continue;
        }

        if (mode == TargetMode::Full) {
            float x, y, z;
            if (PredictXYZ(features[i], x, y, z)) {
                outputs[i].u_x = x; outputs[i].u_y = y; outputs[i].u_z = z;
                outputs[i].u_valid[0] = outputs[i].u_valid[1] = outputs[i].u_valid[2] = true;
                outputs[i].u_f = CoordinateConverter::XYZtoF(x, y, z);
                outputs[i].u_valid[3] = true;
                predicted_flags[i] = true;
            } else {
                predicted_flags[i] = false;
            }
        } else {
            float f;
            if (PredictF(features[i], f)) {
                outputs[i].u_f = f;
                outputs[i].u_valid[3] = true;
                predicted_flags[i] = true;
            } else {
                predicted_flags[i] = false;
            }
        }
    }
    return true;
}

// ---------- ConfigParser ----------
bool ConfigParser::Parse(const std::string& filename, ReconConfig& config) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        std::cerr << "Failed to open config: " << filename << std::endl;
        return false;
    }

    std::string line;
    std::string section;

    while (std::getline(in, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) section = line.substr(1, end - 1);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            if (a == std::string::npos) s = "";
            else s = s.substr(a, b - a + 1);
        };
        trim(key); trim(val);

        if (section == "General") {
            if (key == "TargetMode") {
                if (val == "FOnly") config.target_mode = TargetMode::FOnly;
                else config.target_mode = TargetMode::Full;
            } else if (key == "OutputFormat") {
                if (val == "F") config.output_format = OutputFormat::F;
                else config.output_format = OutputFormat::DHZ;
            } else if (key == "DataDir") {
                config.data_dir = val;
            }
        } else if (section == "InputStations") {
            if (key.substr(0, 7) == "Station") {
                std::istringstream iss(val);
                std::string name, ctype_str;
                float lat = 0, lon = 0, elev = 0;
                std::getline(iss, name, ',');
                std::getline(iss, ctype_str, ',');
                // 尝试读取可选的坐标（覆盖文件头）
                std::string rest;
                std::getline(iss, rest);
                if (!rest.empty()) {
                    std::istringstream iss2(rest);
                    iss2 >> lat >> lon >> elev;
                }
                auto trim2 = [](std::string& s) {
                    size_t a = s.find_first_not_of(" \t");
                    size_t b = s.find_last_not_of(" \t");
                    if (a != std::string::npos) s = s.substr(a, b - a + 1);
                };
                trim2(name); trim2(ctype_str);
                StationData sd;
                sd.name = name;
                if (ctype_str == "XYZ") sd.coord_type = CoordType::XYZ;
                else sd.coord_type = CoordType::DHZ;
                if (lat != 0 || lon != 0) {
                    sd.coord = GeoCoord(lat, lon, elev);
                    sd.coord_from_config = true;
                }
                config.input_stations.push_back(sd);
            }
        } else if (section == "TargetStation") {
            if (key == "Name") config.target_name = val;
            else if (key == "Lat") { config.target_coord.lat = std::stof(val); config.target_coord_from_config = true; }
            else if (key == "Lon") { config.target_coord.lon = std::stof(val); config.target_coord_from_config = true; }
            else if (key == "Elev") { config.target_coord.elev = std::stof(val); }
            else if (key == "ModelX") config.model_x = val;
            else if (key == "ModelY") config.model_y = val;
            else if (key == "ModelZ") config.model_z = val;
            else if (key == "ModelF") config.model_f = val;
        }
    }

    return !config.input_stations.empty() &&
           ((config.target_mode == TargetMode::Full && !config.model_x.empty()) ||
            (config.target_mode == TargetMode::FOnly && !config.model_f.empty()));
}

// ---------- TimeAligner ----------
void TimeAligner::AlignToUTC(std::vector<StationData>& stations) {
    for (auto& st : stations) {
        for (auto& t : st.times) {
            if (t.base == TimeBase::BJT) {
                t = t.ToBase(TimeBase::UTC);
            }
        }
    }
}

void TimeAligner::AlignToUTC(std::vector<TimeStamp>& times) {
    for (auto& t : times) {
        if (t.base == TimeBase::BJT) {
            t = t.ToBase(TimeBase::UTC);
        }
    }
}

// ---------- EvaluationResult ----------
EvaluationResult::EvaluationResult()
    : mae_x(0), mae_y(0), mae_z(0), mae_d(0), mae_h(0), mae_z_dhz(0), mae_f(0),
      overall_xyz_mae(0), valid_count(0), missing_count(0) {}

EvaluationResult EvaluateXYZ(const std::vector<GeomagPoint>& predicted,
                             const std::vector<GeomagPoint>& actual,
                             const std::vector<bool>& predicted_flags) {
    EvaluationResult res;
    float sum_x = 0, sum_y = 0, sum_z = 0;

    for (size_t i = 0; i < predicted.size(); ++i) {
        if (!predicted_flags[i]) { res.missing_count++; continue; }
        if (!actual[i].u_valid[0] || !actual[i].u_valid[1] || !actual[i].u_valid[2]) continue;

        sum_x += std::abs(predicted[i].u_x - actual[i].u_x);
        sum_y += std::abs(predicted[i].u_y - actual[i].u_y);
        sum_z += std::abs(predicted[i].u_z - actual[i].u_z);
        res.valid_count++;
    }

    if (res.valid_count > 0) {
        res.mae_x = sum_x / res.valid_count;
        res.mae_y = sum_y / res.valid_count;
        res.mae_z = sum_z / res.valid_count;
        res.overall_xyz_mae = (sum_x + sum_y + sum_z) / (3.0f * res.valid_count);
    }
    return res;
}

EvaluationResult EvaluateF(const std::vector<GeomagPoint>& predicted,
                           const std::vector<float>& actual_f,
                           const std::vector<bool>& predicted_flags) {
    EvaluationResult res;
    float sum_f = 0;

    for (size_t i = 0; i < predicted.size(); ++i) {
        if (!predicted_flags[i]) { res.missing_count++; continue; }
        if (std::isnan(actual_f[i])) continue;

        sum_f += std::abs(predicted[i].u_f - actual_f[i]);
        res.valid_count++;
    }

    if (res.valid_count > 0) {
        res.mae_f = sum_f / res.valid_count;
    }
    return res;
}

// ---------- OutputWriter ----------
bool OutputWriter::WriteDHZ(const std::string& filename,
                            const std::vector<TimeStamp>& times,
                            const std::vector<GeomagPoint>& data,
                            const std::vector<bool>& flags,
                            const std::string& station_name,
                            const std::string& iaga_code) {
    std::ofstream out(filename);
    if (!out.is_open()) return false;

    out << "Format IAGA-2002\n";
    out << "Source of Data XGBoost-Reconstruction\n";
    out << "Station Name " << station_name << "\n";
    out << "IAGA CODE " << iaga_code << "\n";
    out << "Geodetic Latitude 0.00\n";
    out << "Geodetic Longitude 0.00\n";
    out << "Elevation 0.00\n";
    out << "Reported D H Z\n";
    out << "DATE       TIME         DOY     D        H        Z\n";

    for (size_t i = 0; i < data.size(); ++i) {
        if (!flags[i] || !data[i].u_valid[0] || !data[i].u_valid[1] || !data[i].u_valid[2]) continue;
        float d, h, z;
        CoordinateConverter::XYZtoDHZ(data[i].u_x, data[i].u_y, data[i].u_z, d, h, z);
        int doy = 1;
        out << times[i].ToIAGAString() << " " << std::setw(3) << doy << " "
            << std::fixed << std::setprecision(2)
            << std::setw(8) << d << " "
            << std::setw(8) << h << " "
            << std::setw(8) << z << "\n";
    }
    return true;
}

bool OutputWriter::WriteF(const std::string& filename,
                          const std::vector<TimeStamp>& times,
                          const std::vector<GeomagPoint>& data,
                          const std::vector<bool>& flags,
                          TimeBase output_base) {
    std::ofstream out(filename);
    if (!out.is_open()) return false;

    for (size_t i = 0; i < data.size(); ++i) {
        if (!flags[i] || !data[i].u_valid[3]) continue;
        out << times[i].ToFString(output_base) << "  "
            << std::fixed << std::setprecision(3) << data[i].u_f << "\n";
    }
    return true;
}
