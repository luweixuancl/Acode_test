// main.cpp
// 可变台站/混合坐标系/双目标模式 地磁日变重构主程序

#include "geomag_reconstructor.h"
#include <iostream>
#include <cstdio>

int main(int argc, char* argv[]) {
    std::string config_file = "config.ini";
    if (argc > 1) config_file = argv[1];

    // 1. 读取配置
    ReconConfig config;
    if (!ConfigParser::Parse(config_file, config)) {
        std::cerr << "Failed to parse config: " << config_file << std::endl;
        return 1;
    }

    std::cout << "TargetMode: " << (config.target_mode == TargetMode::FOnly ? "FOnly" : "Full") << std::endl;
    std::cout << "OutputFormat: " << (config.output_format == OutputFormat::F ? "F" : "DHZ") << std::endl;
    std::cout << "Input stations: " << config.input_stations.size() << std::endl;
    for (const auto& s : config.input_stations) {
        std::cout << "  " << s.name << " (" << (s.coord_type == CoordType::XYZ ? "XYZ" : "DHZ")
                  << "), dist=" << s.distance_km << "km, weight=" << s.weight << std::endl;
    }

    // 2. 加载输入台站数据
    std::vector<StationData> stations;
    for (auto& cfg_station : config.input_stations) {
        StationData sd;
        std::string fname = config.data_dir + "/" + cfg_station.name + ".iaga";
        if (!IAGA2000Parser::Parse(fname, sd)) {
            std::cerr << "Failed to load station: " << cfg_station.name << std::endl;
            return 1;
        }
        sd.name = cfg_station.name;
        sd.distance_km = cfg_station.distance_km;
        sd.weight = cfg_station.weight;
        stations.push_back(sd);
    }

    int n = stations[0].points.size();
    for (size_t s = 1; s < stations.size(); ++s) {
        if ((int)stations[s].points.size() != n) {
            std::cerr << "Data length mismatch" << std::endl;
            return 1;
        }
    }
    std::cout << "Loaded " << n << " samples per station" << std::endl;

    // 3. 缺失值处理
    MissingValueProcessor processor;
    std::vector<bool> reconstructable;
    std::vector<FeatureVector> features = processor.Process(stations, reconstructable);

    int unrecoverable = 0;
    for (bool r : reconstructable) if (!r) unrecoverable++;
    std::cout << "Unrecoverable moments: " << unrecoverable << " / " << n << std::endl;

    // 4. 加载模型并预测
    XGBoostGeomagPredictor predictor;
    if (!predictor.LoadModels(config)) {
        std::cerr << "Failed to load XGBoost models" << std::endl;
        return 1;
    }

    std::vector<GeomagPoint> predicted;
    std::vector<bool> predicted_flags;
    predictor.PredictBatch(features, reconstructable, predicted, predicted_flags, config.target_mode);

    // 5. 验证
    if (config.target_mode == TargetMode::Full) {
        StationData target_sd;
        std::string tfile = config.data_dir + "/" + config.target_name + ".iaga";
        if (IAGA2000Parser::Parse(tfile, target_sd)) {
            EvaluationResult eval = EvaluateXYZ(predicted, target_sd.points, predicted_flags);
            std::cout << "=== XYZ Evaluation ===" << std::endl;
            std::cout << "Valid: " << eval.valid_count << std::endl;
            std::cout << "MAE X: " << eval.mae_x << " nT" << std::endl;
            std::cout << "MAE Y: " << eval.mae_y << " nT" << std::endl;
            std::cout << "MAE Z: " << eval.mae_z << " nT" << std::endl;
            std::cout << "Overall XYZ MAE: " << eval.overall_xyz_mae << " nT" << std::endl;
        }
    } else {
        std::vector<TimeStamp> f_times;
        std::vector<float> f_vals;
        std::string tfile = config.data_dir + "/" + config.target_name + "_f.txt";
        if (FFormatParser::Parse(tfile, f_times, f_vals)) {
            EvaluationResult eval = EvaluateF(predicted, f_vals, predicted_flags);
            std::cout << "=== F Evaluation ===" << std::endl;
            std::cout << "Valid: " << eval.valid_count << std::endl;
            std::cout << "MAE F: " << eval.mae_f << " nT" << std::endl;
        }
    }

    // 6. 输出
    std::string out_file = "reconstructed_" + config.target_name;
    if (config.output_format == OutputFormat::DHZ) {
        out_file += ".iaga";
        OutputWriter::WriteDHZ(out_file, stations[0].times, predicted, predicted_flags,
                               config.target_name, "RGN");
    } else {
        out_file += "_f.txt";
        OutputWriter::WriteF(out_file, stations[0].times, predicted, predicted_flags, true);
    }
    std::cout << "Output: " << out_file << std::endl;

    return 0;
}
