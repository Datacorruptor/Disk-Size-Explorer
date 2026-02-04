#include "filetablemodel.h"
#include <qtimer.h>

QString prettySize(uint64_t b) {
    const char* suf[] = {"B","KB","MB","GB","TB"};
    double v = b;
    int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; i++; }
    return QString("%1 %2").arg(v, 0, 'f', 2).arg(suf[i]);
}

QString prettyNumber(qint64 value)
{
    const char* suffixes[] = {"", "K", "M", "B", "T"};

    double num = static_cast<double>(value);
    int suffixIndex = 0;

    while (num >= 1000.0 && suffixIndex < 4) {
        num /= 1000.0;
        ++suffixIndex;
    }

    // show 1 decimal only if needed
    if (num >= 10 || suffixIndex == 0)
        return QString::number(static_cast<qint64>(num)) + suffixes[suffixIndex];
    else
        return QString::number(num, 'f', 1) + suffixes[suffixIndex];
}



enum Columns {
    COLUMN_INDEX,
    COLUMN_ICON,
    COLUMN_NAME,
    COLUMN_SIZE,
    COLUMN_CHILDREN_OBJECTS_LEN,
    COLUMN_EDITDATE,
    COLUMN_TYPE,
    COLUMN_COUNT
};

FileTableModel::FileTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int FileTableModel::rowCount(const QModelIndex&) const
{
    return m_rows.size();
}

int FileTableModel::columnCount(const QModelIndex&) const
{
    return COLUMN_COUNT;
}

QVariant FileTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};

    const FileRow& row = m_rows[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case COLUMN_INDEX:
            return index.row()+1;
        case COLUMN_NAME:
            return row.name;
        case COLUMN_SIZE:
            return prettySize(row.size);
        case COLUMN_CHILDREN_OBJECTS_LEN:
            return row.isDir ? prettyNumber(row.children_onjects_len) : "1";
        case COLUMN_EDITDATE:
            return row.modified.toString("yyyy-MM-dd HH:mm");
        case COLUMN_TYPE:
            return row.isDir ? "Folder" : "File";
        case COLUMN_ICON:
            return row.icon;

        }
        break;

    case Qt::DecorationRole:
        if (index.column() == COLUMN_ICON)
            return row.icon;
        break;

    case Qt::UserRole:
        switch (index.column()) {
        case COLUMN_INDEX:
            return index.row();
        case COLUMN_NAME:
            return row.path;
        case COLUMN_SIZE:
            return row.size;
        case COLUMN_EDITDATE:
            return row.modified;
        case COLUMN_TYPE:
            return row.isDir;
        case COLUMN_ICON:
            return row.icon;
        case COLUMN_CHILDREN_OBJECTS_LEN:
            return row.children_onjects_len;

        }
        break;

    }

    return {};
}

QVariant FileTableModel::headerData(int section,
                                    Qt::Orientation orientation,
                                    int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        static const QStringList headers = {
            "№", "", "Name", "Size", "Objects", "Modified", "Type"
        };
        return headers.value(section);
    }
    return {};
}

void FileTableModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

#include <QElapsedTimer>

void FileTableModel::setRows(QVector<FileRow>&& rows)
{
    QElapsedTimer t;
    t.start();

    beginResetModel();
    qInfo() << "beginResetModel():" << t.elapsed() << "ms";

    m_rows = std::move(rows);
    qInfo() << "move rows:" << t.elapsed() << "ms";

    endResetModel();

    qInfo() << "endResetModel():" << t.elapsed() << "ms";

    QTimer::singleShot(0, this, &FileTableModel::rowsReady);
}
int FileTableModel::insertRow(const FileRow& row)
{
    int pos = m_rows.size();
    beginInsertRows(QModelIndex(), pos, pos);
    m_rows.push_back(row);
    endInsertRows();
    return pos;
}


void FileTableModel::prependRow(const FileRow& row)
{
    beginInsertRows(QModelIndex(), 0, 0);
    m_rows.insert(m_rows.begin(), row);
    endInsertRows();
}

QString FileTableModel::pathAt(int row) const
{
    return m_rows[row].path;
}

QString FileTableModel::nameAt(int row) const
{
    return m_rows[row].name;
}

quint64 FileTableModel::sizeAt(int row) const
{
    return m_rows[row].size;
}

bool FileTableModel::isDirAt(int row) const
{
    return m_rows[row].isDir;
}


void FileTableModel::changeRowSize(int row, quint64 size, quint64 files){
    m_rows[row].size = size;
    m_rows[row].children_onjects_len = files;
    QModelIndex index1 = createIndex(row, COLUMN_SIZE);
    QModelIndex index2 = createIndex(row, COLUMN_CHILDREN_OBJECTS_LEN);
    emit dataChanged(index1,index2);
}

void FileTableModel::changeLastRowSize(quint64 size){
    int row = m_rows.size()-1;
    m_rows[row].size = size;
    QModelIndex index = createIndex(row, COLUMN_SIZE);
    emit dataChanged(index,index);
}


void FileTableModel::appendRows(const QVector<FileRow>& rows)
{
    int start = m_rows.size();
    int end   = start + rows.size() - 1;

    beginInsertRows(QModelIndex(), start, end);
    m_rows += rows;
    endInsertRows();
}


void FileTableModel::setIcon(int row, const QIcon& icon) {
    m_rows[row].icon = icon;
    emit dataChanged(index(row, COLUMN_ICON),
                     index(row, COLUMN_ICON),
                     {Qt::DecorationRole});
}
