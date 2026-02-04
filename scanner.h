#pragma once
#include "node.h"
#include <atomic>
#include <QString>

class DirectoryScanner : public QObject{
    Q_OBJECT

public:
    explicit DirectoryScanner(QObject* parent = nullptr);
    ~DirectoryScanner() override = default;

public:
    QPair<uint64_t,uint64_t> scan(std::shared_ptr<Node> node,
                  std::shared_ptr<Node> l1parent,
                  std::atomic<bool>& cancel,
                  uint64_t level);

    uint64_t countFiles(const QString& path, bool doPrint);

signals:
    void nodeStarted(uint64_t bytes, std::shared_ptr<Node> node);
    void nodeUpdated(uint64_t bytes, std::shared_ptr<Node> node);
    void nodeFinished(uint64_t bytes, std::shared_ptr<Node> node);
    void scanEnded(uint64_t bytes, std::shared_ptr<Node> node);
};
