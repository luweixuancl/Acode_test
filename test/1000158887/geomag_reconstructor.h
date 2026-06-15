// geomag_reconstructor.h
// 基于XGBoost的地磁日变重构系统 — 坐标提取/纬差加权/时间对齐版

#ifndef GEOMAG_RECONSTRUCTOR_H
#define GEOMAG_RECONSTRUCTOR_H

#include <xgboost/c_api.h>
#include <vector>
#include <string>
#include <map>
#include <ctime>
#include <limits>
#include <cmath>

// 目标台站模式
enum class TargetMode { Full, FOnly };

// 原始坐标系
enum class CoordType { DHZ, XYZ, Unknown };

// 输出格式
enum class OutputFormat { DHZ, F };

// 时间基准
enum class TimeBase { UTC, BJT };

// 时间戳
struct TimeStamp {
    int year, month, day, hour, minute, second;
    TimeBase base;
    TimeStamp() : year(0), month(0), day(0), hour(0), minute(0), second(0), base(TimeBase::UTC) {}
    TimeStamp(int y, int mo, int d, int h, int mi, int s, TimeBase b = TimeBase::UTC)
        : year(y), month(mo), day(d), hour(h), minute(mi), second(s), base(b) {}
    time_t ToUnixTime() const;
    static TimeStamp FromUnixTime(time_t t, TimeBase b = TimeBase::UTC);
    TimeStamp ToBase(TimeBase target_base) const;
    std::string ToIAGAString() const;
    std::string ToFString(TimeBase output_base) const;
    bool operator<(const TimeStamp& other) const;
    bool operator==(const TimeStamp& other) const;
};

// 地理坐标
struct GeoCoord {
    float lat;      // 纬度 (度)
    float lon;      // 经度 (度)
    float elev;     // 高程 (m)
    bool valid;
    GeoCoord() : lat(0), lon(0), elev(0), valid(false) {}
    GeoCoord(float la, float lo, float el) : lat(la), lon(lo), elev(el), valid(true) {}
};

// 地磁数据点
struct GeomagPoint {
    // 原始值
    float d, h, z_dhz;
    float x, y, z;
    float f;
    bool valid_d, valid_h, valid_z_dhz;
    bool valid_x, valid_y, valid_z, valid_f;

    // 统一4维特征 [X, Y, Z, F]
    float u_x, u_y, u_z, u_f;
    bool u_valid[4];

    GeomagPoint();
    void ComputeUnified();
};

// 台站数据
struct StationData {
    std::string name;
    std::string iaga_code;
    CoordType coord_type;
    GeoCoord coord;             // 从文件头或配置提取的坐标
    bool coord_from_config;     // 坐标是否来自配置（覆盖文件头）
    float lat_diff_metric;      // 与目标台站的纬差加权度量
    float lon_diff_metric;      // 与目标台站的经差加权度量
    float weight;               // 基于纬差经差的权重
    std::vector<TimeStamp> times;
    std::vector<GeomagPoint> points;

    StationData() : coord_type(CoordType::Unknown), coord_from_config(false),
                    lat_diff_metric(0), lon_diff_metric(0), weight(0) {}
};

// 配置
struct ReconConfig {
    TargetMode target_mode;
    OutputFormat output_format;
    std::vector<StationData> input_stations;
    std::string target_name;
    GeoCoord target_coord;        // 目标台站坐标（FOnly模式下从配置提供）
    bool target_coord_from_config;
    std::string model_x;
    std::string model_y;
    std::string model_z;
    std::string model_f;
    std::string data_dir;

    ReconConfig() : target_mode(TargetMode::Full), output_format(OutputFormat::DHZ),
                    target_coord_from_config(false) {}
};

// 动态特征向量: 4维/台站
struct FeatureVector {
    std::vector<float> features;
    std::vector<bool> valid;
    bool fully_valid;
    int missing_count;
    FeatureVector(int n_stations = 0);
    void Resize(int n_stations);
};

// 坐标转换
class CoordinateConverter {
public:
    static void DHZtoXYZ(float d_deg, float h, float z_in, float& x, float& y, float& z_out);
    static void XYZtoDHZ(float x, float y, float z, float& d_deg, float& h, float& z_out);
    static float XYZtoF(float x, float y, float z);
    static float DHZtoF(float h, float z);
};

// 权重计算：纬差贡献是经差的10倍
class WeightCalculator {
public:
    static float ComputeMetric(const GeoCoord& a, const GeoCoord& b);
    static void ComputeWeights(std::vector<StationData>& stations, const GeoCoord& target);
};

// IAGA-2000解析
class IAGA2000Parser {
public:
    static bool Parse(const std::string& filename, StationData& out_station);
private:
    static bool ParseHeader(std::ifstream& in, StationData& station, std::map<std::string, int>& col_map);
    static bool ParseDataLine(const std::string& line, const std::map<std::string, int>& col_map,
                              CoordType ctype, TimeStamp& ts, GeomagPoint& pt);
    static bool IsMissingValue(float v);
};

// F格式解析
class FFormatParser {
public:
    static bool Parse(const std::string& filename, std::vector<TimeStamp>& times, std::vector<float>& f_values);
};

// 缺失值处理
class MissingValueProcessor {
public:
    bool TemporalInterpolate(StationData& station, int feature_idx, int current_idx);
    std::vector<FeatureVector> Process(const std::vector<StationData>& stations_raw,
                                         std::vector<bool>& reconstructable);
private:
    static const int MAX_GAP_SEC = 300;
    bool SpatialInterpolate(const std::vector<StationData>& stations,
                            int missing_station_idx, int feature_idx,
                            int current_idx, float& result);
};

// XGBoost预测器
class XGBoostGeomagPredictor {
public:
    XGBoostGeomagPredictor();
    ~XGBoostGeomagPredictor();
    bool LoadModels(const ReconConfig& config);
    bool PredictXYZ(const FeatureVector& fv, float& x_out, float& y_out, float& z_out);
    bool PredictF(const FeatureVector& fv, float& f_out);
    bool PredictBatch(const std::vector<FeatureVector>& features,
                      const std::vector<bool>& reconstructable,
                      std::vector<GeomagPoint>& outputs,
                      std::vector<bool>& predicted_flags,
                      TargetMode mode);
private:
    BoosterHandle booster_x_, booster_y_, booster_z_, booster_f_;
    bool initialized_;
    bool PredictComponent(BoosterHandle booster, const FeatureVector& fv, float& output);
};

// 配置解析
class ConfigParser {
public:
    static bool Parse(const std::string& filename, ReconConfig& config);
};

// 时间对齐
class TimeAligner {
public:
    static void AlignToUTC(std::vector<StationData>& stations);
    static void AlignToUTC(std::vector<TimeStamp>& times);
};

// 评估
struct EvaluationResult {
    float mae_x, mae_y, mae_z;
    float mae_d, mae_h, mae_z_dhz;
    float mae_f;
    float overall_xyz_mae;
    int valid_count, missing_count;
    EvaluationResult();
};

EvaluationResult EvaluateXYZ(const std::vector<GeomagPoint>& predicted,
                             const std::vector<GeomagPoint>& actual,
                             const std::vector<bool>& predicted_flags);

EvaluationResult EvaluateF(const std::vector<GeomagPoint>& predicted,
                           const std::vector<float>& actual_f,
                           const std::vector<bool>& predicted_flags);

// 输出
class OutputWriter {
public:
    static bool WriteDHZ(const std::string& filename,
                         const std::vector<TimeStamp>& times,
                         const std::vector<GeomagPoint>& data,
                         const std::vector<bool>& flags,
                         const std::string& station_name,
                         const std::string& iaga_code);
    static bool WriteF(const std::string& filename,
                       const std::vector<TimeStamp>& times,
                       const std::vector<GeomagPoint>& data,
                       const std::vector<bool>& flags,
                       TimeBase output_base);
};

#endif
