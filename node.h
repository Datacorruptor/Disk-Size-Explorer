#pragma once
#include <vector>
#include <memory>
#include <ctime>
#include <QString>
#include <QDir>

/*struct Node {
    QString name;
    QString path;
    uint64_t size = 0;
    bool isDir = false;
    bool scanned = false;

    std::time_t lastModified = 0;

    std::shared_ptr<Node> parent = nullptr;
    std::vector<std::shared_ptr<Node>> children;
};*/

struct Node {
    QString name;      // local name only
    uint64_t size = 0;
    bool isDir = false;
    bool scanned = false;
    std::time_t lastModified = 0;

    std::shared_ptr<Node> parent;   // avoid cycles
    std::vector<std::shared_ptr<Node>> children;

    // Derived, not serialized
    mutable QString cachedPath;
    QString path();
    QString joinPathWin(const QString& base,const QString& name);
};


inline QString Node::joinPathWin(const QString& base,
                                const QString& name)
{
    if (base.isEmpty())
        return name;

    if (base.back() == '\\')
        return base + name;

    return base + "\\" + name;
}

inline QString Node::path()
{
    //qInfo()<<"getting path";
    if (!cachedPath.isEmpty())
        return cachedPath;
   // qInfo()<<"getting new path cache";
    if (parent) {
        cachedPath = joinPathWin(parent->path(),name);
    //    qInfo()<<"node has parent with path "+parent->path();
    } else {
        cachedPath = name; // root (e.g. "C:")
    //    qInfo()<<"node does not have a parent, but has a name |"+name<<"|";
    }
    return cachedPath;
}
