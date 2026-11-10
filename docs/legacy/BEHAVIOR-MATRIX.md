# 旧版行为与文件格式基线

ES-005 的追溯表。后面的目录、黄金向量和 BIN/TXT 解码只引用这里已经指向旧文件的行为。

## 证据

| 项 | 值 |
|---|---|
| 仓库 | `SamhandsomeLee/CHNB` |
| 分支 | `CHNB-NEW-6MIC` |
| 提交 | `fbecac9f54d378d212794fd937e950d9d2acd58d` |
| 设计对照 | `docs/baseline/design/ERNC-Studio-Technical-Design-v1.2.md` 第 2 章 |

下表每一行都属于这个提交。路径相对于该提交的仓库根目录。阅读时使用该提交的已跟踪源码。同目录下未跟踪的 `ERNC_FlashData.bin`、`rnc_diverge_threshold.txt` 和日志不是本表的格式样本。

`rnc_tunning.py` 文件头写明由 `form.ui` 生成。该提交的已跟踪文件里没有 `form.ui`。

## 本文件记录什么

每一行记录行为、旧版路径、符号，以及在该提交里能直接看到的格式或入口。源码里写明的内容标为旧版事实。源码之间互相不一致的地方标为待对照，不在本表里选边。

整区字节数在旧代码里是 `br_ernc/layout_ctypes.py` 的 `sizeof(ANC_TransFunc_f)` 与 `sizeof(Check_data)`。本表不展开这两个 `sizeof` 的数值，也不记录字段偏移。

## 行为矩阵

| 行为 | 旧版路径 | 符号 | 观察到的格式或入口 |
|---|---|---|---|
| 程序入口 | `main.py` | `MainWindow`；`__main__` | 第 2755 行创建 `QApplication` 和 `MainWindow` 并显示。主窗口生成代码在 `rnc_tunning.py` 的 `Ui_MainWindow`。 |
| Scalar | `param_data.py` | `ParamData.VAR_TYPE_VAR`；`ParamDataVar` | 类型值 `0`。设备读数在 `_getval`，用户目标值在 `_setval`。`ui_kind` 默认 `"scalar"`。 |
| Scalar | `br_widget_var_details.py` | `BrWidgetVarDetails` | 标量详情页。发出 `signal_read`、`signal_write`、`signal_read_batch`。 |
| Scalar | `main.py` | `widget_page_var` | 第 293–303 行创建详情页并接入上述三个信号。 |
| Scalar | `br_ernc/algo_off_switch.py` | `UI_KIND_SWITCH_INV`；`ALGO_OFF_PATHS` | 路径 `ANC_OFF`、`ENC_OFF`、`RNC_OFF`。文件文档字符串写明线值 `0` 为算法运行、`1` 为算法停止。详情页上的开关面板是 `br_widget_algo_off_switches.py` 的 `BrAlgoOffSwitchPanel`。 |
| Array | `param_data.py` | `ParamData.VAR_TYPE_ARR`；`ParamDataArr` | 类型值 `1`。设备读数在 `getval_list`，用户目标值在 `setval_list`。`ui_kind` 默认 `"array"`。 |
| Array | `br_widget_arr_details.py` | `BrWidgetArrDetails` | 数组详情页。`main.py` 第 292、298 行放入同一个详情栈。 |
| Array | `br_ernc/curve_array.py` | `CURVE_ARRAYS`；`CurveArraySpec` | 两条曲线路径：`rnc_dicergence.RNCSpkFAdapMax`、`rnc_dicergence.rnc_diverge_threshold`。顺序 `speaker_major`。通道数和点数来自 `layout_ctypes.py` 的 `SPEAKER_NUM = 6`、`TOTAL_FRE_NUM = 120`。 |
| Region | `param_data.py` | `ParamData.VAR_TYPE_BLOB`；`ParamDataBlob` | 类型值 `2`。`dtype` 为 `"blob"`，`ui_kind` 为 `"blob"`，长度字段是 `byte_size`。 |
| Region | `br_widget_blob_placeholder.py` | `BrWidgetRegionPanel` | 第 1181 行别名 `BrWidgetBlobPlaceholder`。读取范围、分块、进度、取消、BIN/TXT 和画图都从这里进入。 |
| Region | `main.py` | `widget_page_blob` | 第 295–308 行接入 `signal_region_read`、`signal_region_write`、`signal_cancel_export`、`signal_plot_region`。 |
| Region | `br_ernc/region_plot_load.py` | `load_bin_file`；`parts_from_memory` | 把 `ERNC_TF`、`check_data` 和 `SpecRadius` 文件装入 `parts`。子区目录在 `br_ernc/ernc_tf_regions.py`、`br_ernc/check_data_regions.py`，解码入口在 `br_ernc/region_decode.py`。 |
| BIN/TXT | `br_export_txt.py` | `write_float_lines_comma`；`read_float_lines_comma`；`write_region_all_txt` | 文本为一行一个 `value,`。空行和 `#` 开头行在读入时跳过。整区文本另有 `# region=`、`# format=slot_floats`、`# slots=`。 |
| BIN/TXT | `br_ernc/curve_array.py` | `write_curve_txt`；`write_curve_bin`；`load_curve_values` | 曲线文本带 `# path=`、`# format=`、`# channels=`、`# pts=`。曲线 BIN 是 little-endian float 顺序打包，无文件头。只接受 `.txt` 与 `.bin`。 |
| BIN/TXT | `br_widget_blob_placeholder.py` | `_on_save_bin_clicked`；`_on_open_bin_clicked`；`_on_open_txt_clicked`；`_on_export_txt_clicked`；`_on_export_all_clicked`；`_on_merge_txt_clicked` | Region 面板上的保存、打开、分包导出、整区导出和 TXT 合成入口。 |
| BIN/TXT | `br_output_paths.py` | `OUTPUT_LAYOUT`；`region_export_dir` | `ERNC_TF` 与 `ERNC_TF.SpecRadius` 导出到 `output/传函/bin` 和 `output/传函/txt`。`check_data` 导出到 `output/checkdata/bin` 和 `output/checkdata/txt`。 |
| Plot | `br_widget_basic_plot.py` | `BrWidgetBasicPlot._load_floats_from_file` | 多曲线窗口。`.txt` 走 `read_float_lines_comma`，`.bin` 按 little-endian float 解析，长度必须是 4 的倍数。 |
| Plot | `main.py` | `_open_ernc_tf_grid_plot`；`_open_spec_radius_plot`；`_open_check_data_plot` | 传函网格调用 `br_widget_ernc_tf_grid_plot.py` 的 `show_ernc_tf_grid_plot`。SpecRadius 使用 `BrWidgetBasicPlot`。check_data 调用 `br_widget_check_data_plot.py` 的 `show_check_data_plot`。 |
| Plot | `br_widget_blob_placeholder.py` | `_on_plot_clicked`；`_on_import_plot_clicked` | 用内存中的 Region 或导入的 `.bin`/`.txt` 发出 `signal_plot_region`。 |
| Pass-through | `modbus_passthrough_tool.py` | `main` | 独立进程，不由 `main.py` 创建。窗口类是 `passthrough/passthrough_window.py` 的 `PassThroughWindow`，标题为「Modbus 透传工具」。 |
| Pass-through | `passthrough/bridge_core.py` | `BridgeCore`；`BridgeOptions` | host 与 poll 两个串口端点之间的转发。默认 `strict_crc=True`、`drop_invalid=True`、`loopback_test=False`。打包脚本是 `build_modbus_passthrough.py`。 |

同提交里的往返测试也指向这些格式：`tests/test_export_txt.py`、`tests/test_curve_array.py`、`tests/test_region_plot_load.py`、`tests/test_basic_plot_import.py`。

Region 面板的每包 slot 数取自 `br_protocol/constants.py`：`DEFAULT_BULK_SLOTS = 60`，`MAX_BULK_SLOTS_READ = 62`，`MAX_BULK_SLOTS_WRITE = 61`。这里只记录旧文件中的赋值。

## 相邻文件索引

这三行只标出入口。JSON、`.params` 和模板的兼容实现不属于 ES-005。

| 格式 | 旧版路径 | 符号 | 观察到的格式或入口 |
|---|---|---|---|
| JSON | `main.py` | `export_params_to_json`；`load_param_values_file` | 对象含 `version` 与 `params`。导出时 `version` 写为 `"1.0.0"`。元素是 `name` 和 `value`。标量取 `val()`，数组取 `getval_list`。`VAR_TYPE_BLOB` 不进入导出。 |
| `.params` | `main.py` | `export_params_to_params` | 按模板顺序写成一行一个 `value,`。模板第一行解析为行数上限。找不到的名称和不足的行数写成 `0,`。按钮默认文件名位于 `output/参数/params`。 |
| 导出模板 | `config/export_params.tpl` | `load_export_template` | 已跟踪模板是这个文件，第一行是行数。导出按钮在路径为空时使用 `./config/export_params.tpl`。`load_export_template` 的默认参数写成 `config/export_template.tpl`，该提交没有这个已跟踪文件。 |

## 留给后续任务

- ES-006 导入 Excel 三表。
- ES-007 写设备 Profile 和待确认清单。
- ES-008 用固件 `sizeof` / `offsetof` 填写 Layout Manifest。
- ES-009 冻结黄金报文；黄金期望不由新编码器生成。
- ES-016 在新代码里定义开关线值。
- ES-075、ES-087、ES-088 实现 BIN/TXT、JSON 和 `.params` 兼容。
