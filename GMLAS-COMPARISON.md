# S-421 SDK 对齐 GDAL GMLAS 的改造记录

> 本文档记录"模仿 GDAL GMLAS 实现"分支上的全部改造内容:本次对话完成的四个工作包、
> 每项与 GDAL/GMLAS 具体组件的对应关系、行为变化清单,以及尚未落地的对齐项(roadmap)。
>
> 对比基准:GDAL GMLAS 驱动(`gdal/ogr/ogrsf_frmts/gmlas/`,依赖 Xerces-C),
> 核心信条:**schema 驱动一切,XML 文档只是数据载体;读写共享同一份映射;错误结构化上报。**

---

## 一、改造内容(四个工作包)

### 1. 对齐 Edition 2.0 数据模型

2.0 数据模型(`S421DataModel.h`)重构后,三个实现文件仍使用旧模型接口,编译失败。全部机械对齐:

| 文件 | 改动 |
|---|---|
| `src/S421Serializer.cpp` | 容器从指针向量改为 `unique_ptr<容器>` + 值向量;`RouteSchedule` 的 `manual/calculated/recommended` 三个 unique_ptr 子对象取代旧 `manualElements` 平铺;`Route::info` 按值化;extensions 从列表改为单数 `optional<RouteExtensions>`;`typeOfShip`→`vesselType`;`ap.radius` 字段;删除 `targetWaypointRef` 残留 |
| `src/S421Validator.cpp` | 同构对齐;`editionTime/vesselMMSI/validity*/planSOG/etd/eta` 按值化(空=未设置);调度元素引用检查重构为 lambda + 指针传参(消除三份重复与向量拷贝) |
| `src/S421Formatter.cpp` | verbose 输出/统计/`summarizeRoute` 对齐新模型 |

同时确立**序列化约定**(后被 FieldSpec 表继承):required(XSD minOccurs≥1)恒写;否则按值成员空串/0/false 视为未设置不写,optional 有值才写。

### 2. Schema 枚举对齐与解析修复

- 经确认,2.0 版 `RouteInfoStatus` 枚举以 9 个值为准(XSD 文本中的 `Service started/ended` 不收录),`S421Types.h` 维持 9 值状态。
- 修复 `parseExchangeSet` 兜底扫描条件写反的 bug:原来要求文件名**必须含** `.s421p`,导致纯 `.s421` 文件永远匹配不到;改为**不含** `.s421p`。

### 3. Roundtrip 回环测试基建

| 文件 | 作用 |
|---|---|
| `tests/DatasetCompare.h/.cpp` | Dataset 深度逐字段比较器:首个差异即停并报"路径: [A] vs [B]";浮点带容差(默认 1e-6,覆盖坐标 6 位小数精度);extensions 按"空串=未设置"归一 |
| `tests/test_main.cpp` | 4 个零依赖用例:`modelRoundtrip`(模型→XML→模型)、`zipRoundtrip`(.s421p)、`serializeDeterministic`(两次序列化字节一致)、`sampleFiles`(argv 传入官方样本回归) |
| `tests/data/README.md` | 官方样本不进仓库,argv 传入的用法说明 |
| `CMakeLists.txt` | `S421_BUILD_TESTS`(默认 ON)+ `s421_tests` 目标 |
| `README.md` | Tests 章节 |

样本字段全覆盖(含 schedule 级 extensions),取值对齐序列化精度使回环零损失。

### 4. FieldSpec 映射表 + XSD 代码生成(核心:消除读写镜像重复)

| 文件 | 角色 |
|---|---|
| `src/S421FieldSpec.h` | 框架(库内私有):`FieldSpec` 行结构(元素名/类别/required/精度/类型擦除成员访问器)、`readFields`/`writeFields` 两个编解码函数,**Parser 与 Serializer 共用** |
| `src/S421FieldTable.h` | **生成产物,勿手改**:10 张表 / 88 行,行序 = XSD `xs:sequence` 顺序(按表写出即满足 schema 顺序),全部行经 XSD 交叉校验 |
| `tools/fieldspecGenerate.py` | 生成器 + 漂移硬校验:映射元素在 XSD 缺失→硬错;XSD 新增简单字段未映射→硬错;类型/required 不符→硬错;`--check` 供 CI |
| `src/S421Parser.cpp` | 8 个 parse* 函数改为"默认构造 + readFields";几何/xlink 引用等对象图结构保持手写;保留 2 处历史容错(`TimeToAct` 大写、`routeWaypointId` 小写);公共头不变 |
| `src/S421Serializer.cpp` | 同构走表(`writeRouteInfo` 约 50 行手写变 3 行);公共头不变 |
| `CMakeLists.txt` | 可选 `s421_codegen_check` 目标(不进默认构建) |
| `README.md` | Schema Codegen 章节 |

schema 升级(2.0→2.1)流程:**换 XSD → 跑生成器(漂移逐条报错)→ 跑 roundtrip 测试**。
"更新数据模型导致 Parser/Serializer/Validator/Types 四处出错"的事故模式从机制上关闭。

---

## 二、与 GDAL GMLAS 的对应关系(总表)

| 本项目实现 | GDAL/GMLAS 对应物 | 说明 |
|---|---|---|
| `src/S421FieldTable.h` 字段映射表 | `GMLASFeatureClass` / `GMLASFieldDefn`(由 `ogrgmlasxsdparser.cpp` 基于 Xerces 的**运行时 schema 分析**构建) | GMLAS 打开文件时解析 XSD 动态生成"元素名→字段(类型/nullable/required)";我们等价物是构建期生成、类型化、零运行时开销 |
| `tools/fieldspecGenerate.py` + `--check` | **XSDCache**(schema 缓存)+ "schemas must be fully valid" 前置校验 + `VALIDATE` / `FAIL_IF_VALIDATION_ERROR` fail-fast 语义的构建期版本 | schema 只处理一次(生成物入库),schema 与消费者不一致时 fail-fast,错误在生成期而非联调期暴露 |
| `S421FieldSpec.h` 的 `readFields`/`writeFields` 单一 codec | `ogrgmlasreader.cpp` 与 `ogrgmlaswriter.cpp` 共享同一份 schema 分析结果驱动读写 | 消除 Parser/Serializer 镜像重复——读写只有一个映射定义点 |
| 类型别名表(`xs:dateTime`/`xs:anyURI`→String 等) | GMLAS 的 **XSD 类型 → OGR 字段类型映射表** | 同为"schema 类型系统 → 消费者类型系统"的显式登记 |
| 结构性元素跳过(`gml:ReferenceType` 引用 / `geometry` 不进字段表) | GMLAS 对 **xlink 属性与几何列**的特殊处理(不当作普通字段) | 对象图结构(引用/几何/容器)由专门代码处理,字段表只管简单字段 |
| `tests/test_main.cpp` 的 `modelRoundtrip` / `sampleFiles` | `gdal/autotest/gdrivers/gmlas.py` 的 **write/roundtrip 用例**(GML→OGR(如 SQLite)→GML 工作流:写出后重读与源逐 feature 比对) | 中间层从 OGR 表换成内存模型,验证目标相同:**写出器不得静默丢弃输入信息** |
| `tests/DatasetCompare.cpp` 逐字段比较器 | 上述 autotest 中的 **feature 逐字段比对** | 含容差与"presence 语义"的实现差异 |
| `tests/data/` 样本不进仓库 + argv 传入 | GDAL autotest 的 **sample-data 回归模式** | 官方样本受分发限制,同构组织方式 |
| 序列化约定(required 恒写 / 空值省略) | GMLAS 按字段 nullable 语义决定输出的行为 | required 标志直接来自生成表(即 XSD) |
| roundtrip 测试暴露的 schedule 级 extensions 缺口(已修复) | GMLAS 写侧的 **`_xml` 原始内容保留字段**(保真机制) | 两者目标一致:回环信息无损 |
| `parseExchangeSet` 兜底条件修复 | (GMLAS 无直接对应)理念上对应其打开数据集时的容错与错误上报 | 纯健壮性 bug 修复 |

**有意不照抄的部分**:运行时 XSD 分析 + 泛型 Feature 模型(GIS 抽象层,牺牲类型化 API)、Xerces 依赖(与嵌入式零依赖冲突)、SAX 流式读取(航线文件小,pugixml DOM 足够)、关联图层/junction 表(OGR 关系型范式)。

---

## 三、行为变化清单(均为改善)

1. schedule 级 extensions(`RouteSchedule` / `Manual|Calculated|Recommended` / `RouteScheduleElement`)现在**双向回环**——原读写两侧均丢弃(保真缺口,由表驱动顺带修复);
2. `routeActionPointID` 为 0 时不再输出(原来恒写 `<routeActionPointID>0</routeActionPointID>`);
3. extensions 属性缺失时 `name/version` 归一为 nullopt(原来写入空串 optional);
4. `turnRadius` 输出由 `std::to_string`(6 位)统一为 `formatDouble`(6 位,等价)。

---

## 四、尚未落地的 GMLAS 对齐项(roadmap)

| 事项 | GMLAS 对应 | 现状 |
|---|---|---|
| 校验分层:schema 层(类型/枚举/pattern/multiplicity)由表驱动,业务规则手写 | `VALIDATE=YES`(Xerces schema 校验)与业务校验分离 | 解析端枚举非法文本仍静默回退默认值;Validator 未消费 FieldSpec 的 required 标志 |
| 结构化错误上报(`ParseError{severity,code,line,column}` + ErrorCollector) | Xerces error handler 的行/列/严重级上报 | 解析仍是 `nullptr` + `getLastError()` 单条字符串 |
| xlink 解析策略化(`Strict`/`Tolerant` 开关) | gmlasconf 的 **xlink resolution rules**(never/raw-content/resolve/延迟) | `resolveById` 末段数字模糊匹配仍可能静默连错对象 |
| 选项对象(替代散落的 `setCoordinatePrecision` 等) | gmlasconf.xml + open options(CONFIG_FILE/VALIDATE/SWAP_COORDINATES…) | 未开始 |
| 往返保真:gml:id 保留、精度可配 | 写侧 `_xml` 字段保留原始 GML | 解析仍丢弃对象级 gml:id,Serializer 重新编 IdMap |

---

## 五、使用速查

```sh
# 构建 + 回环测试
cmake -B build && cmake --build build --target s421_tests
./build/bin/s421_tests                              # 内置样本
./build/bin/s421_tests path/to/sample.gml [...]     # 外部样本回归

# Schema Codegen（XSD 为唯一事实源）
python3 tools/fieldspecGenerate.py                  # 从 dataset/S-421_schema_20260604.xsd 生成 src/S421FieldTable.h
python3 tools/fieldspecGenerate.py --check          # 校验生成物与 XSD 同步（CI 用，漂移退出码 1）
cmake --build build --target s421_codegen_check     # 同上，CMake 目标形式
```
