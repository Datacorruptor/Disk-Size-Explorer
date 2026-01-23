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
    // your init code
}

// --------------------
// Count files (for progress)
// --------------------

uint64_t DirectoryScanner::countFiles(const QString& path, bool doPrint)
{
    if (doPrint)
        qInfo() << "Started files count" << " " << path;
    uint64_t count = 0;

    WIN32_FIND_DATAW data;
    std::wstring search = toW(path) + L"\\*";

    HANDLE h = FindFirstFileW(search.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do {
        std::wstring wname = data.cFileName;
        if (wname == L"." || wname == L"..")
            continue;

        QString name = QString::fromStdWString(wname);
        QString childPath = joinPath(path, name);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count += countFiles(childPath, false);
            if (doPrint)
                qInfo() << count << " " <<"Files counted" << "+" << childPath;
        } else {
            count++;
            if (doPrint)
                qInfo() << count << " " <<"Files counted" << "+" << childPath;
        }

    } while (FindNextFileW(h, &data));

    FindClose(h);
    return count;
}

// --------------------
// Scan directory sizes
// --------------------

uint64_t DirectoryScanner::scan(std::shared_ptr<Node> node,
                                std::shared_ptr<Node> l1parent,
                                std::atomic<bool>& cancel,
                                uint64_t level)
{

    if (level == 0)
        qInfo() << "scan started"
                << "scanned:" << node->scanned;

    if (node->scanned)
        return node->size;

    uint64_t totalSize = 0;

    WIN32_FIND_DATAW data;
    std::wstring search = toW(node->path()) + L"\\*";

    HANDLE hFind = FindFirstFileW(search.c_str(), &data);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    do {
        if (cancel)
            break;

        std::wstring wname = data.cFileName;
        if (wname == L"." || wname == L"..")
            continue;

        QString name = QString::fromStdWString(wname);

        auto child = std::make_shared<Node>();
        child->name = name;
        child->cachedPath = joinPath(node->path(), name);
        child->parent = node;
        child->isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

        // Last modified time
        FILETIME ft = data.ftLastWriteTime;
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        child->lastModified =
            static_cast<std::time_t>(
                (ull.QuadPart - 116444736000000000ULL) / 10000000ULL
                );

        if (level == 0)
            emit nodeStarted(0, child);

        if (child->isDir) {
            if (level == 0)
                qInfo() << "scanning " << child->path();

            if (level == 1){
                l1parent = node;
            }
            totalSize += scan(child, l1parent, cancel, level+1);


            if (level == 0)
                emit nodeFinished(0, child);
        } else {
            LARGE_INTEGER s;
            s.HighPart = data.nFileSizeHigh;
            s.LowPart = data.nFileSizeLow;

            child->size = s.QuadPart;
            totalSize += child->size;

            if (l1parent) {
                emit nodeUpdated(child->size, l1parent);
            }

            //one file processed
            emit nodeFinished(child->size, nullptr);

            if (level == 0)
                emit nodeFinished(0, child);
        }

        node->children.push_back(std::move(child));

    } while (FindNextFileW(hFind, &data));

    FindClose(hFind);

    // Sort largest first
    std::sort(node->children.begin(), node->children.end(),
              [](const auto& a, const auto& b) {
                  return a->size > b->size;
              });

    node->size = totalSize;
    node->scanned = true;


    if (level == 0)
    {
        qInfo() << "scan ended"
                << "scanned:" << node->scanned;
        emit scanEnded(node->size, node);
    }
    return totalSize;
}
