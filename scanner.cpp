#include "scanner.h"
#include <windows.h>
#include <algorithm>

#include <QDebug>
#include <QString>

// --------------------
// Helpers
// --------------------

static std::wstring toW(const QString& s)
{
    return s.toStdWString();
}

static QString joinPath(const QString& base,
                            const QString& name)
{
    if (base.isEmpty())
        return name;

    if (base.back() == '\\')
        return base + name;

    return base + "\\" + name;
}

DirectoryScanner::DirectoryScanner(QObject* parent)
    : QObject(parent)
{
}


qint64 getFileSize(const QString &path)
{
    qint64 size = 0;
    QFileInfo fileInfo(path);

    if(fileInfo.isSymLink() && fileInfo.size() == QFileInfo(fileInfo.symLinkTarget()).size())
    {
        // Try this approach first
        QFile file(path);
        if(file.exists() && file.open(QIODevice::ReadOnly))
            size = file.size();
        file.close();

        // If that didn't work, try this
        if(size == 0)
        {
            QString tmpPath = path+".tmp";
            for(int i=2; QFileInfo().exists(tmpPath); ++i) // Make sure filename is unique
                tmpPath = path+".tmp"+QString::number(i);

            if(QFile::copy(path, tmpPath))
            {
                size = QFileInfo(tmpPath).size();
                QFile::remove(tmpPath);
            }
        }
    }
    else size = fileInfo.size();

    return size;
}

// --------------------
// Scan directory sizes
// --------------------

QPair<uint64_t,uint64_t> DirectoryScanner::scan(std::shared_ptr<Node> node,
                                std::shared_ptr<Node> l1parent,
                                std::atomic<bool>& cancel,
                                uint64_t level)
{

    if (level == 0)
        qInfo() << "scan started"
                << "scanned:" << node->scanned;

    if (node->scanned)
        return {node->size, node->children_objects_len};

    uint64_t totalSize = 0;
    uint64_t totalChildren = 0;

    QDir dir(node->path());
    dir.setFilter(
        QDir::NoDotAndDotDot |
        QDir::AllEntries |
        QDir::Hidden |
        QDir::System
        );


    for (const auto& info : dir.entryInfoList()){
        auto child = std::make_shared<Node>();
        child->name = info.fileName();
        child->cachedPath = joinPath(node->path(), info.fileName());
        child->parent = node;
        child->isDir = info.isDir() && !info.isSymLink();
        child->lastModified = info.lastModified().toSecsSinceEpoch();

        if (level == 0)
            emit nodeStarted(0, child);

        if (child->isDir) {
            if (level == 0)
                qInfo() << "scanning " << child->path();

            if (level == 1){
                l1parent = node;
            }
            auto result = scan(child, l1parent, cancel, level+1);
            totalSize += result.first;
            totalChildren += result.second;


            if (level == 0)
                emit nodeFinished(0, child);
        } else {
            child->size = getFileSize(child->path());
            totalSize += child->size;
            totalChildren += 1;

            if (l1parent) {
                emit nodeUpdated(child->size, l1parent);
            }

            //one file processed
            emit nodeFinished(child->size, nullptr);

            if (level == 0)
                emit nodeFinished(0, child);
        }

        node->children.push_back(std::move(child));
    }

    // Sort largest first
    std::sort(node->children.begin(), node->children.end(),
              [](const auto& a, const auto& b) {
                  return a->size > b->size;
              });

    node->size = totalSize;
    node->children_objects_len = totalChildren;
    node->scanned = true;


    if (level == 0)
    {
        qInfo() << "scan ended"
                << "scanned:" << node->scanned;
        emit scanEnded(node->size, node);
    }
    return {totalSize, totalChildren};
}
