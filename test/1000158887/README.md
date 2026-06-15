# 地磁日变重构系统（坐标提取/纬差加权/时间对齐版）

## 核心改进

### 坐标自动提取与覆盖
- 从IAGA-2000文件头自动提取 `Geodetic Latitude`、`Geodetic Longitude`、`Elevation`
- 配置文件可手动提供坐标，优先级高于文件头（覆盖机制）
- 无需手动填写距离，系统自动计算

### 纬差加权权重
- 不再使用球面距离，采用纬差和经差组合度量
- **纬度差贡献是经度差的10倍**：`metric = sqrt((10*dlat)^2 + dlon^2)`
- 权重 = `1 / metric`，归一化后用于空间插值

### 时间对齐
- IAGA-2000文件时间基准为UTC
- F格式文件时间基准为北京时（BJT）
- FOnly模式下，F格式数据自动转换为UTC进行对齐，输出时可选转回BJT

### 双目标模式
- **Full模式**：训练X/Y/Z模型，预测XYZ，输出阶段转DHZ或计算F
- **FOnly模式**：训练F模型，特征仍含完整4维[X,Y,Z,F]，输出F

## 配置示例

### 输入台站（坐标可选）
```ini
Station0 = TaiAn, DHZ, 36.20, 117.10, 0
Station1 = YuLin, DHZ
```
- 提供坐标则覆盖文件头值
- 不提供则从IAGA文件头自动提取

### 目标台站（FOnly模式必须提供坐标）
```ini
[TargetStation]
Lat = 36.50
Lon = 114.50
```

## 文件结构
- geomag_reconstructor.h/cpp：核心库
- main.cpp：主程序
- train_model.py：训练脚本（同样支持坐标提取与纬差加权）
- train_config.json：训练配置
- config.ini：运行配置

## 使用流程
1. 编辑 train_config.json，设置mode，配置输入台站（无需距离）
2. python train_model.py 训练模型
3. 编辑 config.ini（TargetMode/OutputFormat与训练一致）
4. CMake编译，运行 geomag_recon config.ini
