# 地磁日变重构系统（全要素4维特征/双目标模式）

## 核心设计

### 输入特征统一
每个输入台站无论原始坐标系（DHZ或XYZ），均提取完整4维特征 `[X, Y, Z, F]`：
- DHZ原始文件：D/H/Z转X/Y/Z，F直接提取（无F则计算）
- XYZ原始文件：X/Y/Z/F直接提取
- 特征矩阵维度：`N_samples × (4 × N_stations)`

### 双目标模式

| 模式 | 训练目标 | 模型数量 | 输出 |
|------|---------|---------|------|
| Full | 目标台站 X, Y, Z | 3个 | DHZ(IAGA-2000) 或 F(计算) |
| FOnly | 目标台站 F | 1个 | F(北京时间行格式) |

### 关键原则
- 始终学XYZ（Full模式）或F（FOnly模式）
- FOnly模式下，模型输入仍含各台站F特征（4维），充分利用多分量信息
- 输出阶段按需转换：Full模式预测XYZ后，可选转DHZ或计算F

## 文件结构
- geomag_reconstructor.h/cpp：核心库
- main.cpp：主程序
- train_model.py：训练脚本
- train_config.json：训练配置
- config.ini：运行配置

## 使用流程
1. 编辑 train_config.json，设置mode（Full/FOnly），配置输入台站
2. python train_model.py 训练模型
3. 编辑 config.ini（TargetMode/OutputFormat与训练一致）
4. CMake编译，运行 geomag_recon config.ini
