#pragma once

#include "ernc/application/region_writer.hpp"
#include "ernc/domain/array_state.hpp"
#include "ernc/domain/region_catalog.hpp"

#include <QAbstractTableModel>
#include <QWidget>

#include <cstdint>
#include <span>

class QTableView;

namespace ernc::presentation {

/// 谱半径表的列。频率列是下标，不是赫兹。
enum class SpecRadiusColumn : int {
    /// 下标 0–119。不可编辑。
    Frequency = 0,

    /// 设备读数。不可编辑。
    Read = 1,

    /// 用户目标。只有这一列可编辑。
    Set = 2,

    /// 该下标是否 dirty。不可编辑。
    Dirty = 3
};

/// @brief 120 点谱半径的四列表格。
///
/// 模型不保存第二份数值，数值在 ArrayState 里。必须在 UI 主线程创建和调用。
/// 不连接设备，也不组帧。
class SpecRadiusModel final : public QAbstractTableModel {
public:
    /// @brief 绑定谱半径状态。
    /// @param state 120 个 float 的数组，只借用。空指针或长度不是 120 时没有行。
    /// 调用方保证它比本模型活得久。必须在 UI 主线程创建。
    /// @param parent Qt 父对象。非空时由父子关系拥有本模型。
    explicit SpecRadiusModel(ernc::domain::ArrayState* state, QObject* parent = nullptr);

    /// @brief 行数。
    /// @param parent 只接受无效索引。有效父索引返回 0。
    /// @return 状态长度正好是 120 时为 120。否则为 0。
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// @brief 列数。
    /// @param parent 只接受无效索引。有效父索引返回 0。
    /// @return 固定为 4：频率、Read、Set、Dirty。
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /// @brief 读取一个格子的显示文字。
    /// @param index 行是下标，列是 SpecRadiusColumn。
    /// @param role 只回答 DisplayRole 和 Set 列的 EditRole。
    /// @return 频率列是下标文本。没有值时 Read 和 Set 为「—」。Dirty 为「是」或「否」。
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief 列标题。
    /// @param section 列号。
    /// @param orientation 只回答水平表头。
    /// @param role 只回答 DisplayRole。
    /// @return 频率、Read、Set 或 Dirty。
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// @brief 格子能否编辑。
    /// @param index 表格索引。
    /// @return 只有 Set 列带 ItemIsEditable。
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /// @brief 写入 Set 列。
    /// @param index 必须是 Set 列。
    /// @param value 用户输入的文本。非有限浮点被拒绝。
    /// @param role 只接受 EditRole。
    /// @return 成功写入该下标的 Set 时为 true。Read 和其他行不变。
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    /// @brief 把每个 Set 收回对应的 Read，并刷新整表。
    /// 不访问设备。
    void resetSets();

    /// @brief 通知 Read 列重新读取状态。
    /// 调用方已经改过 ArrayState 的 Read。本函数不改数值。
    void notifyReads();

private:
    /// 谱半径状态。本模型不拥有。
    ernc::domain::ArrayState* state_;
};

/// @brief 独立的 120 点谱半径页面。
///
/// 页面拥有数组状态。必须在 UI 主线程创建和调用。
/// 不挂进功能页导航，不创建串口，也不拼 FC16。
/// 写出的字节交给 RegionWriter::preview。
class SpecRadiusPage final : public QWidget {
public:
    /// @brief 按一块区域定义建立页面。
    /// @param region 必须是 ERNC_TF.SpecRadius，shape 为 [120]，480 字节的 float。
    /// 不符合时表格没有行。只在本次构造期间借用，页面保存自己的副本。
    /// @param parent Qt 父对象。非空时由父子关系拥有本页面。必须在 UI 主线程创建。
    explicit SpecRadiusPage(const ernc::domain::RegionDefinition& region, QWidget* parent = nullptr);

    /// @brief 这块区域是不是本页接受的 120 点谱半径。
    /// @return 路径、shape、长度和 float 类型都符合时为 true。
    bool acceptsRegion() const noexcept;

    /// @brief 借出表格模型。
    /// @return 指向内部模型，不转移所有权。生存期与本页面相同。
    SpecRadiusModel* model() const noexcept;

    /// @brief 把 480 字节读入各行的 Read。
    /// @param bytes 小端 float32，只在本次调用期间借用。长度必须是 480。
    /// @return 解码成功时为 true。只改 Read，不改 Set 和 Dirty。长度或种类不符时为 false，状态不变。
    bool loadReadBytes(std::span<const std::uint8_t> bytes);

    /// @brief 把每个 Set 收回对应的 Read。
    /// 不访问设备。区域不被接受时没有行可收回。
    void resetSets();

    /// @brief 把 120 个 Set 收成 480 个小端字节。
    /// @return 每个下标都有有限 Set 时，调用方拥有字节。缺值、非有限值或区域不符时为 InvalidParameter。
    ernc::domain::Result<std::vector<std::uint8_t>> setBytes() const;

    /// @brief 用这 120 个 Set 向区域写要一份预览。
    /// @param writer 区域写，只借用。本函数只调用 preview，不调用 start。
    /// @return Set 齐全时为写预览。正式目录上的谱半径 commitPermitted 为 false。
    /// Set 不齐时为 InvalidParameter，writer 不会入队。
    ernc::domain::Result<ernc::application::RegionWritePreview> previewWrite(ernc::application::RegionWriter& writer) const;

private:
    /// 区域定义的副本。
    ernc::domain::RegionDefinition region_;

    /// 构造时是否接受这块区域。
    bool accepted_{false};

    /// 120 个 float。不被接受时长度为 0。
    ernc::domain::ArrayState state_;

    /// 四列表格。由 Qt 父子关系拥有。
    SpecRadiusModel* model_;
};

}  // namespace ernc::presentation
