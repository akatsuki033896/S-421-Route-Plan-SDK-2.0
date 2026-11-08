# tests/data — 回环测试样本说明

官方 S-421 样本文件（`*.gml` / `*.s421` / `*.s421p`）受版权/分发限制，**不进仓库**。

两种用法：

1. 命令行参数（推荐，任意路径均可）：

   ```sh
   ./build/bin/s421_tests path/to/RTE-TEST-GBASIC_20260604.gml path/to/xxx.s421p
   ```

2. 放入本目录后按相对路径传入（测试可执行文件在 `build/bin` 下运行时）：

   ```sh
   ./build/bin/s421_tests ../../tests/data/xxx.gml
   ```

传入的每个文件都会执行：解析 → 序列化 → 再解析 → 与首次解析结果逐字段比对
（`DatasetCompare`），不一致时打印首个差异路径。内置样本覆盖不到的真实数据
特征（空元素、厂商扩展、多调度等）建议尽量多放几份进来自动回归。
