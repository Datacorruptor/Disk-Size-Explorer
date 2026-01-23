#pragma once

#include <QAbstractTableModel>
#include <QVector>
#include <QDateTime>
#include <QIcon>

struct FileRow
{
    QString name;
    QString path;
    quint64 size;
    QDateTime modified;
    bool isDir;
    QIcon icon;
};

class FileTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FileTableModel(QObject* parent = nullptr);

    // Required overrides
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    // API for MainWindow
    void clear();
    void setRows(QVector<FileRow>&& rows);
    int insertRow(const FileRow& row);
    void prependRow(const FileRow& row);
    void appendRows(const QVector<FileRow>& rows);
    void changeRowSize(int row, quint64 size);
    void changeLastRowSize(quint64 size);
    void setIcon(int row, const QIcon& icon);

    // Accessors
    QString pathAt(int row) const;
    QString nameAt(int row) const;
    quint64 sizeAt(int row) const;
    bool isDirAt(int row) const;

signals:
    void rowsReady();



private:
    QVector<FileRow> m_rows;
};
