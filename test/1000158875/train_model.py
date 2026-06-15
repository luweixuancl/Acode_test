"""
train_model.py
可变台站/混合坐标系 XGBoost 模型训练脚本
输入特征: 各台站统一4维 [X, Y, Z, F]
目标模式:
  Full:  训练X/Y/Z三个模型，目标台站y为XYZ
  FOnly: 训练F一个模型，目标台站y为F，特征仍含F
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


def load_station_data(filepath, coord_type):
    """加载台站数据，返回统一4维 [X, Y, Z, F]"""
    times = []
    x_vals, y_vals, z_vals, f_vals = [], [], [], []

    with open(filepath, 'r') as f:
        lines = f.readlines()

    col_map = {}
    data_start = 0
    for i, line in enumerate(lines):
        if line.startswith('# Reported'):
            parts = line.strip().split()
            for j, p in enumerate(parts[2:]):
                col_map[p] = j + 3
        elif line.startswith('DATE') or line.startswith('DATE       TIME'):
            data_start = i + 1
            parts = line.strip().split()
            for j, p in enumerate(parts):
                if p in ['D','H','Z','X','Y','F']:
                    col_map[p] = j

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

    return times, x_vals, y_vals, z_vals, f_vals


def build_features(station_configs):
    """构建特征矩阵: N x (4*M)"""
    all_data = []
    weights = []
    total_inv_sq = 0.0

    for cfg in station_configs:
        _, xv, yv, zv, fv = load_station_data(cfg['file'], cfg['coord_type'])
        all_data.append((xv, yv, zv, fv))
        inv_sq = 1.0 / (cfg['distance_km'] ** 2)
        weights.append(inv_sq)
        total_inv_sq += inv_sq

    weights = [w / total_inv_sq for w in weights]
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

    return X, weights


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

    X, weights = build_features(input_stations)
    print(f"Feature shape: {X.shape}")
    print(f"Weights: {weights}")

    # 加载目标台站数据
    _, tx, ty, tz, tf = load_station_data(target['file'], target['coord_type'])

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
