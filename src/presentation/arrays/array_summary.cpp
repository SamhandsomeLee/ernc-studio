#include "arrays/array_summary.hpp"

namespace ernc::presentation {

bool usesCompactArrayUi(const ernc::domain::ParameterDefinition* definition,
                        const ernc::domain::FirmwareLayout* layout) {
    if (layout == nullptr) {
        return false;
    }
    return ernc::domain::shouldUseCompactArrayUi(definition, *layout);
}

QString compactArrayCaption(const ernc::domain::ParameterDefinition& definition,
                            const ernc::domain::FirmwareLayout& layout,
                            const ernc::domain::ArrayRuntimeStore* arrayStore) {
    if (!definition.firmwarePath) {
        return QStringLiteral("数组");
    }
    const ernc::domain::LayoutResolve resolved = layout.locate(*definition.firmwarePath);
    if (!resolved.ok()) {
        return QStringLiteral("数组");
    }
    const std::size_t count = resolved.field().slotCount;
    QString caption = QString::number(static_cast<qulonglong>(count)) + QStringLiteral(" 元");
    if (arrayStore == nullptr) {
        return caption;
    }
    const ernc::domain::Result<ernc::domain::ParameterId> parsed = ernc::domain::ParameterId::parse(definition.id.text());
    if (!parsed.ok()) {
        return caption;
    }
    const ernc::domain::ArrayRuntimeMeta* meta = arrayStore->meta(parsed.value());
    const bool read = arrayStore->hasAnyRead(parsed.value());
    if (meta != nullptr && meta->stale && !read) {
        caption += QStringLiteral(" · 未读");
    } else if (read) {
        caption += QStringLiteral(" · 已读");
    }
    const std::size_t dirty = arrayStore->dirtyCount(parsed.value());
    if (dirty > 0U) {
        caption += QStringLiteral(" · ") + QString::number(static_cast<qulonglong>(dirty)) + QStringLiteral(" 待写");
    }
    return caption;
}

}  // namespace ernc::presentation
