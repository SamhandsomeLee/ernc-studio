#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <optional>

class QLabel;

namespace ernc::presentation {

class PlotWorkspace;

/// @brief 画出 120 点谱半径曲线。
///
/// 必须在 UI 主线程创建和调用。控件拥有一份数据集副本，图只保存显示点。
/// 横轴是下标 0–119。频率轴尚未确认，这里不标 Hz。不访问串口，也不组帧。
class SpecRadiusPlot final : public QWidget {
public:
    /// @brief 建立尚未载入曲线的页面。
    /// @param parent Qt 父对象。为空时调用方负责销毁。非空时随父对象销毁。
    /// 必须在 UI 主线程调用。
    explicit SpecRadiusPlot(QWidget* parent = nullptr);

    /// @brief 显示一条谱半径曲线。
    /// @param dataset 已构造成功的数据集，只在本次调用期间借用。路径必须是 ERNC_TF.SpecRadius，shape 必须是 {120}，点数必须是 120。
    /// @return 成功时没有载荷。路径、shape 或点数不符时为 InvalidParameter，已有曲线保持不变。
    ernc::domain::Result<void> setCurve(const ernc::domain::AnalysisDataset& dataset);

    /// @brief 已保存的点数。
    /// @return 已载入时为 120。未载入时为 0。不读取 Qwt 曲线。
    std::size_t sampleCount() const noexcept;

    /// @brief 读取一个已保存的 float。
    /// @param index 从 0 开始的下标。等于 sampleCount() 时越界。它不是频率。
    /// @return 成功时为该下标的 float。未载入或越界时为 InvalidParameter。NaN 和 Inf 原样返回。
    ernc::domain::Result<float> valueAt(std::size_t index) const;

    /// @brief 借出嵌入的曲线页。
    /// @return 指向内部 PlotWorkspace。不转移所有权，本控件销毁后失效。
    PlotWorkspace* plotWorkspace() const noexcept;

    /// @brief 页面上说明横轴的文字。
    /// @return 已载入时含路径和「索引 0–119」。未载入时为空。文字里不写 Hz。
    QString chromeText() const;

private:
    /// 说明横轴是下标的标签。本控件拥有。
    QLabel* caption_ = nullptr;

    /// 嵌入的曲线页。本控件拥有。
    PlotWorkspace* workspace_ = nullptr;

    /// 120 个原始点。空表示还没有载入。
    std::optional<ernc::domain::AnalysisDataset> dataset_;
};

}  // namespace ernc::presentation
