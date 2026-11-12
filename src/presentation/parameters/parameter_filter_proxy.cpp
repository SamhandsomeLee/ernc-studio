#include "parameters/parameter_filter_proxy.hpp"

namespace ernc::presentation {

ParameterFilterProxy::ParameterFilterProxy(const ernc::domain::ParameterCatalog* catalog,
                                           ernc::domain::ParameterRuntimeStore* store, QObject* parent)
    : QSortFilterProxyModel(parent), catalog_{catalog}, store_{store} {
    setDynamicSortFilter(true);
}

void ParameterFilterProxy::setPage(const QString& pageId) {
    const QString next = pageId.trimmed();
    if (next == page_) {
        return;
    }
    page_ = next;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ParameterFilterProxy::setQuery(const QString& text) {
    const QString next = text.trimmed();
    if (next == query_) {
        return;
    }
    query_ = next;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ParameterFilterProxy::setDirtyOnly(bool enabled) {
    if (enabled == dirtyOnly_) {
        return;
    }
    dirtyOnly_ = enabled;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool ParameterFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (sourceParent.isValid() || catalog_ == nullptr || sourceRow < 0
        || static_cast<std::size_t>(sourceRow) >= catalog_->size()) {
        return false;
    }
    const ernc::domain::ParameterDefinition& definition = catalog_->entries()[static_cast<std::size_t>(sourceRow)];
    if (definition.id.text() == "Can_Msg") {
        return false;
    }
    if (!page_.isEmpty() && page_ != QStringLiteral("ALL")) {
        if (!definition.category || QString::fromStdString(*definition.category) != page_) {
            return false;
        }
    }
    if (!query_.isEmpty()) {
        const QString id = QString::fromStdString(definition.id.text());
        const QString path = definition.firmwarePath ? QString::fromStdString(*definition.firmwarePath) : QString();
        const QString description = definition.descriptionZh ? QString::fromStdString(*definition.descriptionZh) : QString();
        const bool matched = id.contains(query_, Qt::CaseInsensitive) || path.contains(query_, Qt::CaseInsensitive)
                             || description.contains(query_, Qt::CaseInsensitive);
        if (!matched) {
            return false;
        }
    }
    if (dirtyOnly_) {
        const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition.id);
        if (state == nullptr || !state->dirty) {
            return false;
        }
    }
    return true;
}

} // namespace ernc::presentation
