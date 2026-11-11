#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>

class QComboBox;
class QLabel;
class QSpinBox;

namespace ernc::presentation {

class PlotWorkspace;

/// 实部或虚部。名称来自字段路径，不表示哪一条数组轴是物理通道。
enum class ErncTfField {
    /// ERNC_TF.fSecPathTransR。
    Real,

    /// ERNC_TF.fSecPathTransI。
    Imag
};

/// @brief 按存储分组显示 ERNC_TF 的实部和虚部。
///
/// 必须在 UI 主线程创建和调用。控件拥有两份完整数据集，图上只放当前组的 120 个点。
/// 不访问串口，不组帧。组号是 shape 的前两维下标。点号是最后一维下标，不标成 Hz。
class ErncTfPlot final : public QWidget {
public:
    /// @brief 建立尚未载入数据的页面。
    /// @param parent Qt 父对象。为空时调用方负责销毁。非空时随父对象销毁。
    /// 必须在 UI 主线程调用。
    explicit ErncTfPlot(QWidget* parent = nullptr);

    /// @brief 保存实部和虚部，并显示实部的第 0 组。
    /// @param real 实部数据集，只在本次调用期间借用。路径必须是 ERNC_TF.fSecPathTransR，shape 必须是 {6, 6, 120}。
    /// @param imag 虚部数据集，只在本次调用期间借用。路径必须是 ERNC_TF.fSecPathTransI，shape 必须是 {6, 6, 120}。
    /// @return 成功时没有载荷。路径或 shape 不符时为 InvalidParameter，已有的数据和曲线保持不变。
    ernc::domain::Result<void> setFields(const ernc::domain::AnalysisDataset& real, const ernc::domain::AnalysisDataset& imag);

    /// @brief 切换正在显示的字段。
    /// @param field 实部或虚部。
    /// @return 成功时没有载荷。尚未载入时为 InvalidParameter，当前曲线保持不变。
    ernc::domain::Result<void> selectField(ErncTfField field);

    /// @brief 选择一组 120 个点。
    /// @param group0 第一维下标，合法范围是 0 到 5。
    /// @param group1 第二维下标，合法范围是 0 到 5。
    /// @return 成功时没有载荷。尚未载入或下标越界时为 InvalidParameter，当前组和曲线保持不变。
    ernc::domain::Result<void> selectGroup(std::uint32_t group0, std::uint32_t group1);

    /// @brief 选择读数里的频率点。
    /// @param point 最后一维下标，合法范围是 0 到 119。它不改变曲线上的点数。
    /// @return 成功时没有载荷。尚未载入或越界时为 InvalidParameter，当前点号和曲线保持不变。
    ernc::domain::Result<void> selectPoint(std::uint32_t point);

    /// @brief 一份完整字段的样本个数。
    /// @param field 实部或虚部。
    /// @return 已载入时为 4320。该字段还没有数据时为 0。不读取图上的曲线。
    std::size_t fullSampleCount(ErncTfField field) const noexcept;

    /// @brief 读取完整副本里的一个 float。
    /// @param field 实部或虚部。
    /// @param storageIndex 从 0 开始的存储下标。等于样本数时越界。
    /// @return 成功时为该位置的 float。未载入或越界时为 InvalidParameter。
    ernc::domain::Result<float> valueAt(ErncTfField field, std::size_t storageIndex) const;

    /// @brief 当前曲线的显示点数。
    /// @return 已画出一组时为 120。还没有曲线时为 InvalidParameter。
    ernc::domain::Result<std::size_t> displayedPointCount() const;

    /// @brief 当前点的读数。
    /// @return 已载入时为「点 N = 值」。未载入时为空字符串。文本里不写 Hz。
    QString pointReadout() const;

    /// @brief 借出嵌入的曲线页。
    /// @return 指向内部 PlotWorkspace。不转移所有权，本控件销毁后失效。
    PlotWorkspace* plotWorkspace() const noexcept;

private:
    /// @brief 把当前字段和当前组画到曲线页。
    /// @return 曲线页接受这份 120 点数据时没有载荷。映射失败时为对应错误，成员里的选择不变。
    ernc::domain::Result<void> redraw();

    /// @brief 让下拉框和数字框显示当前选择。
    /// 暂时挡住信号，避免数字框再调用选择函数。
    void syncControls();

    /// @brief 按当前点号重写读数。
    /// 不改曲线。尚未载入时读数清空。
    void updateReadout();

    /// 实部或虚部。本控件拥有。
    QComboBox* fieldBox_ = nullptr;

    /// 第一维组号，范围 0 到 5。
    QSpinBox* group0Box_ = nullptr;

    /// 第二维组号，范围 0 到 5。
    QSpinBox* group1Box_ = nullptr;

    /// 频率点下标，范围 0 到 119。
    QSpinBox* pointBox_ = nullptr;

    /// 当前点的文字读数。
    QLabel* readout_ = nullptr;

    /// 嵌入的曲线页。本控件拥有。
    PlotWorkspace* workspace_ = nullptr;

    /// 完整实部。空表示还没有载入。
    std::optional<ernc::domain::AnalysisDataset> real_;

    /// 完整虚部。空表示还没有载入。
    std::optional<ernc::domain::AnalysisDataset> imag_;

    /// 正在显示的字段。未载入时保留为实部，不代表已经有数据。
    ErncTfField field_ = ErncTfField::Real;

    /// 当前第一维组号。
    std::uint32_t group0_ = 0;

    /// 当前第二维组号。
    std::uint32_t group1_ = 0;

    /// 读数使用的点号。改它不会减少曲线点数。
    std::uint32_t point_ = 0;

    /// 两份完整数据都已收下。
    bool loaded_ = false;
};

}  // namespace ernc::presentation
