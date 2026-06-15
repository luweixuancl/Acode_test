"""
train_model.py
坐标提取/纬差加权/双目标模式 XGBoost训练脚本
输入特征: 各台站统一4维 [X, Y, Z, F]
权重计算: 纬差贡献是经差的10倍
"""

import numpy as np
import xgboost as xgb
from sklearn.model_selection import KFold
from skopt import BayesSearchCV
from skopt.space import Real, Integer
import json
import math


def dhz_to_xyz(d_deg, h, z):
    rad = math.radians(d_deg)
    x = h * math.cos(rad)
    y = h * math.sin(rad)
    return x, y, z


def xyz_to_f(x, y, z):
    return math.sqrt(x*x + y*y + z*z)


def dhz_to_f(h, z):
    return math.sqrt(h*h + z*z)


def parse_iaga_header(filepath):
    """解析IAGA文件头，提取坐标和坐标系"""
    coord = {'lat': None, 'lon': None, 'elev': None}
    coord_type = None
    col_map = {}
    reported_cols = []
    data_start = 0

    with open(filepath, 'r') as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        line = line.strip()
        if not line:
            continue
        if line.startswith('#'):
            parts = line[1:].strip().split()
            if len(parts) >= 2:
                if parts[0] == 'Geodetic' and parts[1] == 'Latitude':
                    coord['lat'] = float(parts[2]) if len(parts) > 2 else None
                elif parts[0] == 'Geodetic' and parts[1] == 'Longitude':
                    coord['lon'] = float(parts[2]) if len(parts) > 2 else None
                elif parts[0] == 'Elevation':
                    coord['elev'] = float(parts[1]) if len(parts) > 1 else None
                elif parts[0] == 'Reported':
                    reported_cols = parts[1:]
                    if 'X' in reported_cols or 'Y' in reported_cols:
                        coord_type = 'XYZ'
                    elif 'D' in reported_cols or 'H' in reported_cols:
                        coord_type = 'DHZ'
        elif line.startswith('DATE') or line.startswith('DATE       TIME'):
            data_start = i + 1
            parts = line.split()
            for j, p in enumerate(parts):
                if p in ['D','H','Z','X','Y','F']:
                    col_map[p] = j
            if coord_type is None:
                if 'X' in col_map or 'Y' in col_map:
                    coord_type = 'XYZ'
                elif 'D' in col_map or 'H' in col_map:
                    coord_type = 'DHZ'
            break

    return coord, coord_type, col_map, data_start, lines


def load_station_data(filepath, coord_type_override=None, coord_override=None):
    """加载台站数据，返回统一4维 [X, Y, Z, F] 和坐标"""
    coord, coord_type, col_map, data_start, lines = parse_iaga_header(filepath)

    # 配置覆盖文件头坐标
    if coord_override and coord_override.get('lat') is not None:
        coord = coord_override

    if coord_type_override:
        coord_type = coord_type_override

    times = []
    x_vals, y_vals, z_vals, f_vals = [], [], [], []

    for line in lines[data_start:]:
        line = line.strip()
        if not line or line.startswith('#'): continue
        parts = line.split()
        if len(parts) < 4: continue

        times.append(parts[0] + ' ' + parts[1])

        def getv(key):
            idx = col_map.get(key, -1)
            if idx < 0 or idx >= len(parts): return None
            try:
                v = float(parts[idx])
                if abs(v - 99999.0) < 0.5 or abs(v) > 99990: return None
                return v
            except:
                return None

        if coord_type == 'DHZ':
            d = getv('D'); h = getv('H'); z = getv('Z'); f = getv('F')
            if d is not None and h is not None and z is not None:
                xv, yv, zv = dhz_to_xyz(d, h, z)
                x_vals.append(xv); y_vals.append(yv); z_vals.append(zv)
                if f is not None:
                    f_vals.append(f)
                else:
                    f_vals.append(dhz_to_f(h, z))
            else:
                x_vals.append(None); y_vals.append(None); z_vals.append(None); f_vals.append(None)
        else:
            x = getv('X'); y = getv('Y'); z = getv('Z'); f = getv('F')
            if x is not None and y is not None and z is not None:
                x_vals.append(x); y_vals.append(y); z_vals.append(z)
                if f is not None:
                    f_vals.append(f)
                else:
                    f_vals.append(xyz_to_f(x, y, z))
            else:
                x_vals.append(None); y_vals.append(None); z_vals.append(None); f_vals.append(None)

    return times, x_vals, y_vals, z_vals, f_vals, coord


def compute_weights(station_coords, target_coord):
    """纬差贡献是经度差的10倍"""
    weights = []
    total_inv = 0.0
    for sc in station_coords:
        dlat = abs(sc['lat'] - target_coord['lat'])
        dlon = abs(sc['lon'] - target_coord['lon'])
        metric = math.sqrt((10.0 * dlat)**2 + dlon**2)
        if metric > 0.001:
            w = 1.0 / metric
            weights.append(w)
            total_inv += w
        else:
            weights.append(0.0)
    if total_inv > 0:
        weights = [w / total_inv for w in weights]
    return weights


def build_features(station_configs):
    """构建特征矩阵: N x (4*M)"""
    all_data = []
    station_coords = []

    for cfg in station_configs:
        coord_override = None
        if 'lat' in cfg and 'lon' in cfg:
            coord_override = {'lat': cfg['lat'], 'lon': cfg['lon'], 'elev': cfg.get('elev', 0)}
        _, xv, yv, zv, fv, coord = load_station_data(cfg['file'], cfg.get('coord_type'), coord_override)
        all_data.append((xv, yv, zv, fv))
        station_coords.append(coord)

    n = len(all_data[0][0])
    m = len(station_configs)
    X = np.zeros((n, m * 4), dtype=np.float32)

    for i in range(n):
        for s in range(m):
            xv, yv, zv, fv = all_data[s]
            X[i, s*4 + 0] = xv[i] if xv[i] is not None else np.nan
            X[i, s*4 + 1] = yv[i] if yv[i] is not None else np.nan
            X[i, s*4 + 2] = zv[i] if zv[i] is not None else np.nan
            X[i, s*4 + 3] = fv[i] if fv[i] is not None else np.nan

    return X, station_coords


def train_model(X, y, component_name, output_path):
    params = {
        'booster': 'gbtree',
        'max_depth': 3,
        'learning_rate': 0.05,
        'n_estimators': 300,
        'subsample': 0.8,
        'colsample_bytree': 0.2,
        'objective': 'reg:squarederror',
        'eval_metric': 'rmse',
        'nthread': 5,
        'random_state': 42
    }

    valid_mask = ~np.isnan(y)
    Xv = X[valid_mask]
    yv = y[valid_mask]

    kf = KFold(n_splits=5, shuffle=True, random_state=42)
    opt = BayesSearchCV(
        xgb.XGBRegressor(**params),
        {
            'max_depth': Integer(2, 6),
            'learning_rate': Real(0.01, 0.3, prior='log-uniform'),
            'n_estimators': Integer(100, 500),
        },
        n_iter=50,
        cv=kf,
        scoring='neg_mean_squared_error',
        random_state=42,
        n_jobs=5
    )

    opt.fit(Xv, yv)
    print(f"[{component_name}] Best params: {opt.best_params_}")
    print(f"[{component_name}] Best CV RMSE: {np.sqrt(-opt.best_score_):.4f} nT")

    opt.best_estimator_.save_model(output_path)
    print(f"[{component_name}] Saved: {output_path}")
    return opt.best_estimator_


def main():
    with open('train_config.json', 'r') as f:
        cfg = json.load(f)

    mode = cfg.get('mode', 'Full')
    input_stations = cfg['input_stations']
    target = cfg['target_station']

    print(f"Training mode: {mode}")
    print(f"Input stations: {len(input_stations)}")

    X, station_coords = build_features(input_stations)
    print(f"Feature shape: {X.shape}")

    # 获取目标台站坐标
    target_coord = None
    if 'lat' in target and 'lon' in target:
        target_coord = {'lat': target['lat'], 'lon': target['lon']}
    else:
        # 从目标文件提取
        _, _, _, _, _, coord = load_station_data(target['file'], target.get('coord_type'))
        target_coord = coord

    weights = compute_weights(station_coords, target_coord)
    print(f"Weights: {weights}")

    # 加载目标台站数据
    _, tx, ty, tz, tf, _ = load_station_data(target['file'], target.get('coord_type'))

    if mode == 'Full':
        y_x = np.array([v if v is not None else np.nan for v in tx], dtype=np.float32)
        y_y = np.array([v if v is not None else np.nan for v in ty], dtype=np.float32)
        y_z = np.array([v if v is not None else np.nan for v in tz], dtype=np.float32)

        train_model(X, y_x, 'X', target['model_x'])
        train_model(X, y_y, 'Y', target['model_y'])
        train_model(X, y_z, 'Z', target['model_z'])
    else:
        y_f = np.array([v if v is not None else np.nan for v in tf], dtype=np.float32)
        train_model(X, y_f, 'F', target['model_f'])

    print("Training completed.")


if __name__ == '__main__':
    main()
