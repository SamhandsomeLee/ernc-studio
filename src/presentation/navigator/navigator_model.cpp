#include "navigator/navigator_model.hpp"

namespace ernc::presentation {
namespace {

/// 页面节点的 internalId。参数节点使用所属页面的行号，行号不会取这个值。
constexpr quintptr kPageNode = static_cast<quintptr>(-1);

} // namespace

NavigatorModel::NavigatorModel(ernc::domain::CatalogLoad catalog, QObject* parent)
    : QAbstractItemModel(parent)
    , catalog_(std::move(catalog)) {
    if (!catalog_.ok()) {
        error_ = QStringLiteral("参数目录无法加载。");
        return;
    }
    NavigatorProjection projection = projectNavigatorPages(catalog_.catalog());
    if (!projection.ok) {
        error_ = QString::fromStdString(projection.error);
        return;
    }
    projection_ = std::move(projection);
}

bool NavigatorModel::ok() const {
    return projection_.has_value();
}

QString NavigatorModel::error() const {
    return error_;
}

const ernc::domain::ParameterCatalog* NavigatorModel::catalog() const {
    if (!ok()) {
        return nullptr;
    }
    return catalog_.tryCatalog();
}

QModelIndex NavigatorModel::index(int row, int column, const QModelIndex& parent) const {
    if (!projection_ || column != 0 || row < 0) {
        return {};
    }
    if (!parent.isValid()) {
        if (row >= static_cast<int>(projection_->pages.size())) {
            return {};
        }
        return createIndex(row, column, kPageNode);
    }
    if (parent.internalId() != kPageNode) {
        return {};
    }
    const auto pageRow = static_cast<std::size_t>(parent.row());
    if (pageRow >= projection_->pages.size()) {
        return {};
    }
    if (row >= static_cast<int>(projection_->pages[pageRow].parameters.size())) {
        return {};
    }
    return createIndex(row, column, static_cast<quintptr>(parent.row()));
}

QModelIndex NavigatorModel::parent(const QModelIndex& child) const {
    if (!child.isValid() || child.internalId() == kPageNode) {
        return {};
    }
    return createIndex(static_cast<int>(child.internalId()), 0, kPageNode);
}

int NavigatorModel::rowCount(const QModelIndex& parent) const {
    if (!projection_) {
        return 0;
    }
    if (!parent.isValid()) {
        return static_cast<int>(projection_->pages.size());
    }
    if (parent.column() != 0 || parent.internalId() != kPageNode) {
        return 0;
    }
    const auto pageRow = static_cast<std::size_t>(parent.row());
    if (pageRow >= projection_->pages.size()) {
        return 0;
    }
    return static_cast<int>(projection_->pages[pageRow].parameters.size());
}

int NavigatorModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return projection_ ? 1 : 0;
}

QVariant NavigatorModel::data(const QModelIndex& index, int role) const {
    if (!projection_ || !index.isValid()) {
        return {};
    }
    if (index.internalId() == kPageNode) {
        const auto pageRow = static_cast<std::size_t>(index.row());
        if (pageRow >= projection_->pages.size()) {
            return {};
        }
        const std::string_view pageId = projection_->pages[pageRow].id;
        if (role == Qt::DisplayRole) {
            return QString::fromUtf8(pageId.data(), static_cast<qsizetype>(pageId.size()));
        }
        if (role == KindRole) {
            return QStringLiteral("页面");
        }
        return {};
    }

    const auto pageRow = static_cast<std::size_t>(index.internalId());
    if (pageRow >= projection_->pages.size()) {
        return {};
    }
    const auto& parameters = projection_->pages[pageRow].parameters;
    const auto parameterRow = static_cast<std::size_t>(index.row());
    if (parameterRow >= parameters.size() || parameters[parameterRow] == nullptr) {
        return {};
    }
    if (role == Qt::DisplayRole) {
        return QString::fromStdString(parameters[parameterRow]->id.text());
    }
    if (role == KindRole) {
        return QStringLiteral("参数");
    }
    return {};
}

Qt::ItemFlags NavigatorModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace ernc::presentation
