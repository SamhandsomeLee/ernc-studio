#pragma once

#include <QString>

namespace ernc::presentation {

/// 串口 Worker 与 UI 约定的批量读「全目录标量」令牌。不是项目树页面 id。
inline const QString kBatchReadAllTag = QStringLiteral("@batch_read_all_scalars");

/// 批量写「全部脏参数」令牌。不是 navigator 的 `ALL` 页面。
inline const QString kBatchWriteAllTag = QStringLiteral("@batch_write_all_dirty");

/// @brief 项目树「ALL」页：参数表显示全目录，读写范围等同批量「所有标量/全部脏参数」。
inline bool isNavigatorAllPage(const QString& pageId) {
    return pageId == QStringLiteral("ALL");
}

}  // namespace ernc::presentation
