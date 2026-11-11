#pragma once

#include <QString>
#include <QWidget>

#include <vector>

namespace ernc::presentation {

/// CAN 信号名称目录。只显示 Excel 名称、未接入和没有新鲜度。
/// 不读取 Can_Msg，也不连接设备。必须在 UI 主线程创建和调用。
class CanNamePage final : public QWidget {
public:
    /// @brief 创建空的名称目录。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 名称要等 loadFile 之后才出现。构造不打开串口。
    explicit CanNamePage(QWidget* parent = nullptr);

    /// @brief 从 Excel 导出的 JSON 读取 K 列名称。
    /// @param path 名称文件路径。调用方拥有原件。文件打不开时目录变为空。
    /// @note 只取非空的 K 列文本。不把行号当成 Can_Msg 槽位。只在 UI 主线程调用。
    void loadFile(const QString& path);

    /// @brief 名称行数。
    /// @return 成功加载正式文件时为 36。失败时为 0。
    int nameCount() const;

    /// @brief 某一行的 Excel 名称。
    /// @param index 从 0 开始，顺序与文件一致。越界时为空字符串。
    /// @return K 列原文。
    QString nameAt(int index) const;

    /// @brief 某一行的映射状态。
    /// @param index 从 0 开始。越界时为空字符串。
    /// @return 固定为「未接入」。不是 Can_Msg 里的数值。
    QString statusAt(int index) const;

    /// @brief 某一行的新鲜度。
    /// @param index 从 0 开始。越界时为空字符串。
    /// @return 固定为「无」。不是设备时间戳。
    QString freshnessAt(int index) const;

private:
    /// 按文件顺序保存的名称。本页拥有这些字符串。
    std::vector<QString> names_;
};

}  // namespace ernc::presentation
