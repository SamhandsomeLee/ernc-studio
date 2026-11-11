#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class QComboBox;
class QLabel;
class QVBoxLayout;
class QWidget;

namespace ernc::presentation {

class PlotWorkspace;

/// 一个 check_data 字段的画法。只影响当前字段的曲线怎么摆，不改样本。
enum class CheckDataViewMode {
    /// 各条曲线画在同一张图上。
    Overlay,

    /// 每一条曲线单独一张图。
    Stack
};

/// @brief 切换 check_data 字段，并按叠加或堆叠显示。
///
/// 必须在 UI 主线程创建和调用。控件按路径保存完整数据集，图只放当前字段的显示副本。
/// 不访问串口，不组帧。横轴用曲线页上的「索引」，不写 Hz 或 dB。
class CheckDataPlot final : public QWidget {
public:
    /// @brief 建立尚未载入字段的页面。
    /// @param parent Qt 父对象。为空时调用方负责销毁。非空时随父对象销毁。
    /// 必须在 UI 主线程调用。默认模式是叠加。
    explicit CheckDataPlot(QWidget* parent = nullptr);

    /// @brief 保存一个 check_data 子字段。
    /// @param field 已构造成功的数据集，只在本次调用期间借用。path 必须是 20 个 check_data 子字段之一。
    /// shape 和点数以这份数据集为准。
    /// @return 成功时没有载荷。路径不在这 20 个字段里时为 InvalidParameter，已有数据和当前图保持不变。
    /// 同一路径会换掉该路径的副本。第一次成功载入时，当前字段变为这一份。
    ernc::domain::Result<void> setField(const ernc::domain::AnalysisDataset& field);

    /// @brief 切换当前显示的字段。
    /// @param path 已经载入的 check_data 子字段路径。
    /// @return 成功时没有载荷。该路径还没有数据时为 InvalidParameter，当前图保持不变。
    ernc::domain::Result<void> selectField(std::string_view path);

    /// @brief 切换叠加或堆叠。
    /// @param mode 目标画法。
    /// @return 成功时没有载荷。重画失败时模式保持原值。
    ernc::domain::Result<void> setMode(CheckDataViewMode mode);

    /// @brief 当前画法。
    /// @return 尚未切换过时为叠加。
    CheckDataViewMode mode() const noexcept;

    /// @brief 一份已保存字段的样本个数。
    /// @param path 字段路径。
    /// @return 已载入时为该数据集的点数。没有这份数据时为 0。不读取图上的曲线。
    std::size_t fullSampleCount(std::string_view path) const noexcept;

    /// @brief 读取完整副本里的一个 float。
    /// @param path 字段路径。
    /// @param storageIndex 从 0 开始的存储下标。等于样本数时越界。
    /// @return 成功时为该位置的 float。路径未载入或越界时为 InvalidParameter。
    ernc::domain::Result<float> valueAt(std::string_view path, std::size_t storageIndex) const;

    /// @brief 当前画出的图张数。
    /// @return 没有当前字段时为 0。叠加时为 1。堆叠时等于曲线条数。
    int plotCount() const noexcept;

    /// @brief 一张图上第一条曲线的显示点数。
    /// @param plotIndex 从 0 开始的图序号。等于 plotCount() 时越界。
    /// @return 成功时为该图第一条曲线的点数。越界时为 InvalidParameter。
    ernc::domain::Result<std::size_t> displayedPointCount(int plotIndex) const;

    /// @brief 借出一张图。
    /// @param plotIndex 从 0 开始的图序号。
    /// @return 下标有效时指向内部 PlotWorkspace，不转移所有权，本控件销毁或重画后失效。越界时为空。
    PlotWorkspace* plotAt(int plotIndex) const noexcept;

    /// @brief 字段、模式和分组标签上的文字。
    /// @return 多段文字用换行连起来。用来核对界面没有写出 Hz 或 dB。
    QString chromeText() const;

private:
    /// @brief 按当前字段和模式重画。
    /// @return 显示副本都构建成功时没有载荷。失败时原来的图还在。
    ernc::domain::Result<void> rebuild();

    /// @brief 让字段下拉框对准当前路径。
    /// 暂时挡住信号，避免再次调用 selectField。
    void syncFieldBox();

    /// @brief 让模式下拉框对准当前画法。
    /// 暂时挡住信号，避免再次调用 setMode。
    void syncModeBox();

    /// @brief 按路径查找已保存的数据集。
    /// @param path 字段路径，只在本次调用期间借用。
    /// @return 找到时指向内部数据集，不转移所有权。没有时为空。
    const ernc::domain::AnalysisDataset* findField(std::string_view path) const noexcept;

    /// 字段路径下拉框。本控件拥有。
    QComboBox* fieldBox_ = nullptr;

    /// 叠加或堆叠下拉框。本控件拥有。
    QComboBox* modeBox_ = nullptr;

    /// 放各张图的竖向布局。滚动区域拥有它所在的页面。
    QVBoxLayout* plotLayout_ = nullptr;

    /// 已保存的字段，按首次载入的顺序。本控件拥有。
    std::vector<ernc::domain::AnalysisDataset> fields_;

    /// 当前图的指针。重画时清空再填。图本身由布局里的行拥有。
    std::vector<PlotWorkspace*> plots_;

    /// 每张图旁边的路径或组号标签。行拥有这些标签。
    std::vector<QLabel*> labels_;

    /// 当前字段路径。空表示还没有成功载入。
    std::string currentPath_;

    /// 当前画法。
    CheckDataViewMode mode_ = CheckDataViewMode::Overlay;
};

}  // namespace ernc::presentation
