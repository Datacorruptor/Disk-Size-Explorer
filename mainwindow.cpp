#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <qapplication.h>
#include <qscrollarea.h>
#include <windows.h>
#include <shellapi.h>
#include <QJsonObject>
#include <QJsonArray>

#include <thread>

#include <QDebug>
#include <QString>
#include <QtWidgets/QLabel>

#include <QStorageInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QFileIconProvider>
#include <QHash>

#include <QSaveFile>
#include <QFile>
#include <QDataStream>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtConcurrent/QtConcurrent>
#include <QtGlobal>
#include <QFutureWatcher>

#include <algorithm>

#include "fractionalprogressbar.h"

#include <QHash>
#include <QIcon>
#include <QMutex>

QHash<QString, QIcon> iconCache;
QMutex iconCacheMutex;

QIcon folderPlaceholder;
QIcon filePlaceholder;

enum Columns {
    COLUMN_INDEX,
    COLUMN_ICON,
    COLUMN_NAME,
    COLUMN_SIZE,
    COLUMN_EDITDATE,
    COLUMN_TYPE,
    COLUMNs_COUNT
};


struct NodeDisk {
    QString name;
    quint64 size;
    bool isDir;
    bool scanned;
    qint64 lastModified;
    qint32 parentIndex;
};

void flattenTree(const std::shared_ptr<Node>& node,
                 QVector<NodeDisk>& out,
                 QHash<const Node*, int>& indexMap,
                 int parentIndex = -1)
{
    NodeDisk d;
    d.name = node->name;
    //d.path = node->path;
    d.size = node->size;
    d.isDir = node->isDir;
    d.scanned = node->scanned;
    d.lastModified = node->lastModified;
    d.parentIndex = parentIndex;

    int myIndex = out.size();
    out.append(d);
    indexMap[node.get()] = myIndex;

    for (const auto& child : node->children) {
        flattenTree(child, out, indexMap, myIndex);
    }
}

bool saveBinary(const QHash<QString, std::shared_ptr<Node>>& hash,
                const QString& fileName)
{
    // Serialize to memory first
    QByteArray rawData;
    QDataStream out(&rawData, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_6);

    // Optional: magic + version
    out << quint32(0x5343414E); // "SCAN"
    out << quint16(1);          // version

    out << quint32(hash.size());

    for (const auto& root : hash) {
        QVector<NodeDisk> flat;
        QHash<const Node*, int> indexMap;

        flattenTree(root, flat, indexMap);

        out << quint32(flat.size());
        for (const auto& d : flat) {
            out << d.name
                << d.size
                << d.isDir
                << d.scanned
                << d.lastModified
                << d.parentIndex;
            //qInfo() << "saving name" << d.name;
        }
    }

    // Compress
    QByteArray compressed = qCompress(rawData, 1); // max compression

    // Atomic save
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    if (file.write(compressed) != compressed.size())
        return false;
    qInfo() << "file saved!";
    return file.commit();
}


bool MainWindow::loadBinary(QHash<QString, std::shared_ptr<Node>>& hash,
                const QString& fileName)
{

    qInfo() << "loading binary scan from " << fileName;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    qInfo() << "file availible for read  " << fileName;

    QByteArray compressed = file.readAll();
    QByteArray rawData = qUncompress(compressed);

    if (rawData.isEmpty())
        return false; // corrupted or invalid file

    qInfo() << "succesfully read data  " << fileName;

    QDataStream in(&rawData, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_6_6);

    // Check magic + version
    quint32 magic;
    quint16 version;
    in >> magic >> version;

    if (magic != 0x5343414E || version != 1)
        return false;
    qInfo() << "correct magic and version" << fileName;

    hash.clear();

    quint32 rootCount;
    in >> rootCount;

    qInfo() << "RootCount " << rootCount;

    static QElapsedTimer loader_timer;
    if (!loader_timer.isValid()) loader_timer.start();


    for (quint32 r = 0; r < rootCount; ++r) {
        quint32 count;
        in >> count;

        QVector<std::shared_ptr<Node>> nodes(count);
        QVector<qint32> parentIndices(count);

        float p = ((r) * 100) / (rootCount );
        float smallp1 = (0/100) * (1 / (rootCount ))/2;
        float smallp2 = (0/100) * (1 / (rootCount ))/2;

        for (quint32 i = 0; i < count; ++i) {
            nodes[i] = std::make_shared<Node>();
            in >> nodes[i]->name
                >> nodes[i]->size
                >> nodes[i]->isDir
                >> nodes[i]->scanned
                >> nodes[i]->lastModified
                >> parentIndices[i];

            if (i == 0){
                loader->setMessage("Loading "+QString(nodes[i]->name));
                loader->setProgress(p);
                QApplication::processEvents();
            }

            if (loader_timer.elapsed() > 2000){
                smallp1 = (100.0*i/count) * (1.0 / (rootCount))/2;
                loader->setProgress(p+smallp1);
                qInfo() << "progress" << smallp1;
                QApplication::processEvents();
                loader_timer.restart();
            }


            //qInfo() <<"name"<< nodes[i]->name;
        }

        // Rebuild parent/children
        for (quint32 i = 0; i < count; ++i) {
            qint32 parentIndex = parentIndices[i];
            if (parentIndex >= 0) {
                nodes[i]->parent = nodes[parentIndex];
                nodes[parentIndex]->children.push_back(nodes[i]);
            }

            if (loader_timer.elapsed() > 2000){
                smallp2 =  (100.0*i/count) * (1.0 / (rootCount))/2;
                loader->setProgress(p+smallp1+smallp2);
                qInfo() << "progress" << smallp2;
                QApplication::processEvents();
                loader_timer.restart();
            }
        }

        // Root node
        qInfo() << "inserting name " << nodes[0]->name << "to the root";
        hash.insert(nodes[0]->name, nodes[0]);
    }

    return true;
}

FractionalProgressBar::FractionalProgressBar(QWidget *p): QProgressBar(p)
{}

QString FractionalProgressBar::text() const
{
    if ( minimum() == maximum() ) // divide by zero guard
        return QString();

    double percent = 100.0 * (value() - minimum()) / (maximum() - minimum());
    return QString("%1%").arg(percent, 0, 'f', 2);
}


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{

    loader = new LoadingDialog(this);
    loader->show();

    setupUi();

    loader->close();
    delete loader;
}

MainWindow::~MainWindow()
{
    cancel = true;
}


/*
void MainWindow::navigate(QTableWidgetItem* item) {
    qInfo() << "Navigation!";

    qInfo() << item->text() <<" "<<  item->;

    Node* n = static_cast<Node*>(item->data(Qt::UserRole).value<void*>());
    if (n && n->isDir)
        populateTable(n);
}*/

QString MainWindow::joinPathWin(const QString& base,
                            const QString& name)
{
    if (base.isEmpty())
        return name;

    if (base.back() == '\\')
        return base + name;

    return base + "\\" + name;
}

void MainWindow::navigate(const QModelIndex& index) {
    qInfo() << "Navigation!";

    QModelIndex src = proxyModel->mapToSource(index);
    int row = src.row();

    QString path = fileModel->pathAt(row);
    QString item_name = fileModel->nameAt(row);
    bool isDir = fileModel->isDirAt(row);
    qInfo() << item_name <<" isDir:"<<  isDir;

    if (current->parent){
        if (row == 0){
            goBack();
            return;
        }
        row = row-1;
    }

    if (isDir){
        qInfo() << "node" <<current->children[row]->name;
        //pathEdit->setText(joinPathWin(pathEdit->text().toStdString(), current->children[row]->name).c_str());
        pathEdit->setCurrentText(current->children[row]->path());
        populateTable(current->children[row]);
    } else{
        QUrl fileUrl = QUrl::fromLocalFile(current->children[row]->path());
        QDesktopServices::openUrl(fileUrl);
    }

}

void MainWindow::goBack() {
    if (current){
        qInfo() << "going back, current "<<current->path() << ", current parent" << current->parent.get();

        if (current->parent) {
            pathEdit->setCurrentText(current->parent->path());
            populateTable(current->parent);
        }
    }
}

void MainWindow::setupUi()
{
    loader->setMessage("Starting application...");
    loader->setProgress(0);
    QApplication::processEvents();


    qInfo() << "An informational message.";

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    QHBoxLayout* topLayout = new QHBoxLayout();

    driveCombo = new QComboBox(this);

    DWORD mask = GetLogicalDrives();
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (mask & 1)
            driveCombo->addItem(QString("%1:\\").arg(c));
        mask >>= 1;
    }

    pathEdit = new QComboBox(this);
    pathEdit->setEditable(true);
    pathEdit->setInsertPolicy(QComboBox::InsertAtBottom);
    pathEdit->setDuplicatesEnabled(false);

    scanButton = new QPushButton("Scan", this);
    backBtn = new QPushButton("Back");
    backBtn->setMaximumWidth(50);

    progressBar = new FractionalProgressBar(this);
    progressBar->setRange(0, 10000);
    progressBar->setEnabled(false);
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setValue(0);




    /*table = new AutoStretchTable(this);
    table->setColumnCount(COLUMNs_COUNT);
    table->setHorizontalHeaderLabels({
        "", "Name", "Size", "Modified", "Type"
    });*/

    folderPlaceholder = QIcon("/icons/load.png");
    filePlaceholder   = QIcon("/icons/load.png");

    label = new QLabel(this);
    label->setAlignment(Qt::AlignCenter);
    label->setText("");

    table = new QTableView(this);
    //table->setSortingEnabled(true);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(20);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);

    table->setStyleSheet("QTableView::item { padding-left: 3px; }");

    fileModel = new FileTableModel(this);

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(fileModel);
    proxyModel->setDynamicSortFilter(true);

    table->setModel(proxyModel);
    //table->setSortingEnabled(true);


    //table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    //table->horizontalHeader()->setStretchLastSection(true);

    //table->horizontalHeader()->minimumWidth()
    table->horizontalHeader()->resizeSection(COLUMN_INDEX, 3);
    table->horizontalHeader()->resizeSection(COLUMN_ICON, 10);
    //table->horizontalHeader()->setSectionResizeMode(table->columnCount()-1, QHeaderView::Fixed);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto* header = table->horizontalHeader();

    // Allow user resizing
    header->setSectionsMovable(true);
    header->setStretchLastSection(false);

    // Stretch ONLY column 1
    header->setSectionResizeMode(COLUMN_NAME, QHeaderView::Stretch);

    // Other columns are interactive
    header->setSectionResizeMode(COLUMN_ICON, QHeaderView::Interactive);
    header->setSectionResizeMode(COLUMN_SIZE, QHeaderView::Interactive);
    header->setSectionResizeMode(COLUMN_EDITDATE, QHeaderView::Interactive);
    header->setSectionResizeMode(COLUMN_TYPE, QHeaderView::Interactive);


    table->setWordWrap(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    //table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSortingEnabled(false);

    proxyModel->setSortRole(Qt::UserRole);
    proxyModel->sort(COLUMN_SIZE, Qt::DescendingOrder);

    topLayout->addWidget(backBtn);
    topLayout->addWidget(driveCombo);
    topLayout->addWidget(pathEdit,1);
    topLayout->addWidget(scanButton);


    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(table);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(label);


    qInfo() << "An informational message2.";

    loadBinary(scanResultHash, "scans.dat");

    resize(900, 600);

    connect(scanButton, &QPushButton::clicked,
            this, &MainWindow::startScan);

    pathEdit->setCurrentText("C:\\");
    connect(driveCombo, &QComboBox::currentTextChanged,
            this, [this](const QString& drive) {
                qInfo() << "text change";
                qInfo() << drive;
                pathEdit->setCurrentText(drive);
                fileModel->clear();

                QString path = pathEdit->currentText().trimmed(); // remove accidental spaces

                if (scanResultHash.contains(path)) {
                    std::shared_ptr<Node> node = scanResultHash.value(path);
                    if (node) {
                        qDebug() << "Pointer is good!" << node->path();
                        root = std::shared_ptr<Node>(scanResultHash[pathEdit->currentText()]);
                        current = root;
                        populateTable(root);

                    } else {
                        qDebug() << "Pointer in hash is null!";
                    }
                } else {
                    qDebug() << "Path not found in hash:" << path;
                }
            });

    connect(pathEdit, &QComboBox::textActivated,
            this, [this](const QString& drive) {
                fileModel->clear();

                QString path = pathEdit->currentText().trimmed(); // remove accidental spaces

                if (scanResultHash.contains(path)) {
                    std::shared_ptr<Node> node = scanResultHash.value(path);
                    if (node) {
                        qDebug() << "Pointer is good!" << node->path();
                        root = std::shared_ptr<Node>(scanResultHash[pathEdit->currentText()]);
                        current = root;
                        populateTable(root);
                    } else {
                        qDebug() << "Pointer in hash is null!";
                    }
                } else {
                    qDebug() << "Path not found in hash:" << path;
                }

                //if (scanResultHash.contains(pathEdit->currentText())){
                //    root = std::shared_ptr<Node>(scanResultHash[pathEdit->currentText()]);
                //    current = root.get();
                //    populateTable(root.get());
                //}
            });

    connect(table, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);

    connect(backBtn, &QPushButton::clicked, this, &MainWindow::goBack);
    //connect(table, &QTableWidget::itemDoubleClicked, this, &MainWindow::navigate);

    connect(table, &QTableView::doubleClicked,this, &MainWindow::navigate);

    /*connect(table->horizontalHeader(), &QHeaderView::sectionResized,
            this, [this](int logicalIndex, int oldSize, int newSize) {
                table->adjustStretchColumn(logicalIndex, oldSize, newSize);
            });*/

    scanner = new DirectoryScanner();

    connect(scanner, &DirectoryScanner::nodeUpdated,
            this, &MainWindow::onNodeUpdated,
            Qt::QueuedConnection);

    connect(scanner, &DirectoryScanner::nodeStarted,
            this, &MainWindow::onNodeStarted,
            Qt::QueuedConnection);

    connect(scanner, &DirectoryScanner::nodeFinished,
            this, &MainWindow::onNodeFinished,
            Qt::QueuedConnection);

    connect(scanner, &DirectoryScanner::scanEnded,
            this, &MainWindow::onScanFinished,
            Qt::QueuedConnection);


    connect(fileModel, &FileTableModel::rowsReady, this, [this]() {
        QtConcurrent::run([this]() {
            QVector<QString> paths;

            QMetaObject::invokeMethod(fileModel, [&]() {
                const int count = fileModel->rowCount();
                paths.reserve(count);
                for (int i = 0; i < count; ++i)
                    paths.push_back(fileModel->pathAt(i));
            }, Qt::BlockingQueuedConnection);

            // now safe, model is ready
            for (int i = 0; i < paths.size(); ++i) {
                QIcon icon;
                if (iconCache.contains(paths[i])) {
                    icon = iconCache[paths[i]];
                } else {
                    icon = iconProvider.icon(QFileInfo(paths[i]));
                    iconCache.insert(paths[i], icon);
                }

                QMetaObject::invokeMethod(fileModel, [this, i, icon]() {
                    fileModel->setIcon(i, icon);
                }, Qt::QueuedConnection);
            }
        });
    });




    QString path = pathEdit->currentText().trimmed(); // remove accidental spaces

    if (scanResultHash.contains(path)) {
        std::shared_ptr<Node> node = scanResultHash.value(path);
        if (node) {
            qDebug() << "Pointer is good!" << node->path();
            root = std::shared_ptr<Node>(scanResultHash[pathEdit->currentText()]);
            current = root;
            populateTable(root);

        } else {
            qDebug() << "Pointer in hash is null!";
        }
    } else {
        qDebug() << "Path not found in hash:" << path;
    }
}

void MainWindow::onScanFinished(uint64_t newSize, std::shared_ptr<Node> node){
    QMetaObject::invokeMethod(this, "scanFinished", Qt::QueuedConnection);
}

void MainWindow::startScan()
{

    root = std::make_unique<Node>();
    root->cachedPath = pathEdit->currentText();
    root->name = root->cachedPath;

    pathEdit->insertItem(0, pathEdit->currentText());
    pathEdit->setCurrentIndex(0);

    root->isDir = true;
    root->children.clear();
    root->scanned = false;
    root->parent = nullptr;

    qInfo() << "new root" << root->path();

    current = root;

    qInfo() << "current" << current->path();

    qInfo() << "Started scanning from " << root->path();

    MainWindow::scan(root);
}


void MainWindow::startPartScan(std::shared_ptr<Node> node)
{
    node->children.clear();
    node->scanned=false;
    MainWindow::scan(node);
}


void MainWindow::scan(std::shared_ptr<Node> node)
{




    qInfo() << "scanning from " << node->path();
    cancel = false;
    processedFiles = 0;

    fileModel->clear();
    table->setDisabled(true);
    progressBar->setEnabled(true);

    progressBar->setValue(0);


    QStorageInfo storage(node->path());

    if (!storage.isValid() || !storage.isReady()) {
        qWarning() << "Invalid or not ready storage for path:" << node->path();
        return;
    }

    uint64_t total = storage.bytesTotal();
    uint64_t free = storage.bytesAvailable();
    totalSize = total - free;

    qInfo() << "totalSize " << totalSize << " Bytes";




    /*totalFiles = scanner.countFiles(node->path, true);
    qInfo() << "counted " << totalFiles << " files";
    qInfo() << "created thread"
            << "node ptr:" << &(*node)
            << "scanned:" << node->scanned.load();*/

    processedSize = 0;

    std::thread([this, node]() {
        scanner->scan(
            node,
            nullptr,
            cancel,
            0
            );
    }).detach();

    qInfo() << "closed thread ";
}





void MainWindow::saveInBackground(QHash<QString, std::shared_ptr<Node>> scanResultHash)
{
    QtConcurrent::run([scanResultHash = std::move(scanResultHash)]() {
        saveBinary(scanResultHash, "scans.dat");
    });
}



void MainWindow::updateProgress()
{
    if (totalSize == 0)
        return;
    progressBar->setValue(
        static_cast<int>(std::max<double>(0,std::min<double>((processedSize * 10000) / (totalSize * 1.01),9999))));

    label->setText(pretty(processedSize));
}

void MainWindow::scanFinished()
{
    if (current->parent){
        std::sort(current->parent->children.begin(), current->parent->children.end(),
                  [](const auto& a, const auto& b) {
                      return a->size > b->size;
                  });
    }
    else{
        qInfo() << "saving "<< current->path() << "to hash";
        scanResultHash[current->path()] = current;
        //saveBinary(scanResultHash, "scans.dat");
        saveInBackground(scanResultHash);
    }

    //progressBar->setValue(10000);
    //progressBar->setEnabled(false);
    populateTable(current);
    table->setDisabled(false);
    progressBar->setValue(10000);
    progressBar->setEnabled(false);

}


void MainWindow::populateTableIncremental(
    const QVector<FileRow>& allRows)
{
    constexpr int CHUNK_SIZE = 999999;

    table->setUpdatesEnabled(true);
    fileModel->clear();

    auto* rowsPtr = new QVector<FileRow>(std::move(allRows));
    auto* index   = new int(0);

    QTimer* timer = new QTimer(this);
    timer->setInterval(0); // let event loop run

    connect(timer, &QTimer::timeout, this,
            [=]() mutable {
                if (*index >= rowsPtr->size()) {
                    timer->stop();
                    timer->deleteLater();

                    //table->setSortingEnabled(true);
                    proxyModel->sort(COLUMN_SIZE, Qt::DescendingOrder);

                    delete rowsPtr;
                    delete index;
                    return;
                }

                QVector<FileRow> chunk;
                chunk.reserve(CHUNK_SIZE);

                for (int i = 0;
                     i < CHUNK_SIZE && *index < rowsPtr->size();
                     ++i, ++(*index)) {
                    chunk.push_back((*rowsPtr)[*index]);
                }

                fileModel->appendRows(chunk);
            });

    timer->start();
}


void MainWindow::populateTable(std::shared_ptr<Node> node)
{


    proxyModel->sort(COLUMN_SIZE, Qt::DescendingOrder);
    table->setUpdatesEnabled(false);
    table->setSortingEnabled(false);

    qInfo() << "Populating table:" << node->path();
    current = node;
    QElapsedTimer timer;
    timer.start();

    QVector<FileRow> rows;
    rows.reserve(node->children.size());

    for (const auto& child : node->children) {
        rows.push_back({
            child->name,
            child->path(),
            child->size,
            QDateTime::fromSecsSinceEpoch(child->lastModified),
            child->isDir,
            child->isDir ? folderPlaceholder : filePlaceholder
        });
    }


    fileModel->setRows(std::move(rows));

    proxyModel->sort(COLUMN_INDEX, Qt::AscendingOrder);

    if (node->parent)
        fileModel->prependRow({"..","../",node->size,QDateTime(),1,filePlaceholder});

    label->setText(pretty(node->size));

    table->setUpdatesEnabled(true);
    //table->setSortingEnabled(true);

    table->viewport()->update();

    qInfo() << "Table populated in" << timer.elapsed() << "ms";
}


void MainWindow::onNodeUpdated(uint64_t newSize, std::shared_ptr<Node> node)
{

    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    //qInfo() << "updating row table" << node->path;
    //int row = 0;
    l1Files++;
    l1Size+=newSize;

    //qInfo() << l1Files << timer.elapsed();
    if ((l1Files % 1000 == 0) || timer.elapsed() > 50) {
        if (Name2Index.contains(node->name)){
            //qInfo() << "changing size";
            fileModel->changeRowSize(Name2Index[node->name],l1Size);
            timer.restart();
        }
    }

}



void MainWindow::onNodeFinished(uint64_t newSize, std::shared_ptr<Node> node)
{
    processedSize+=newSize;
    QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection);

    if (node){
        qInfo() << "finalizing row table" << node->path();
        l1Size = 0;
        //fileModel->changeLastRowSize(node->size);
        //table->setSortingEnabled(true);
    }

}


void MainWindow::onNodeStarted(uint64_t newSize,std::shared_ptr<Node> node)
{
    table->setSortingEnabled(false);
    qInfo() << "adding row table" << node->path();

    Name2Index[node->name] = fileModel->insertRow({
        node->name,
        node->path(),
        node->size,
        QDateTime::fromSecsSinceEpoch(node->lastModified),
        node->isDir,
        node->isDir ? folderPlaceholder : filePlaceholder
    });


    proxyModel->sort(COLUMN_INDEX, Qt::DescendingOrder);


}


void MainWindow::showContextMenu(const QPoint& pos)
{
    int selected_row = table->rowAt(pos.y());
    if (selected_row < 0)
        return;

    QMenu menu(this);
    QAction* del = menu.addAction("Move to Recycle Bin");
    QAction* open = menu.addAction("Show in explorer");

    QAction* chosen =
        menu.exec(table->viewport()->mapToGlobal(pos));


    if (chosen == del) {

        QStringList paths;
        QStringList names;
        QVector<int> rows;
        QVector<Node*> nodes;

        QList<QModelIndex> selectedIndexes = table->selectionModel()->selectedRows();

        foreach (const QModelIndex &index, selectedIndexes) {
            int row = index.row();
            qInfo() << "selected row:" <<row;

            QString path = fileModel->pathAt(row);
            QString name = fileModel->nameAt(row);

            rows.append(row);
            names.append(name);
            paths.append(path);
        }

        confirmMultiDelete(paths, names, rows);
    }

    if (chosen == open){
        Node* node = current->children[selected_row].get();
        QFileInfo fileInfo(node->path());
        QString dirPath = fileInfo.absoluteDir().absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    }
}

void MainWindow::confirmMultiDelete(const QStringList& paths,
                               const QStringList& names,
                               const QVector<int>& rows)
{
    Q_ASSERT(paths.size() == names.size());
    Q_ASSERT(paths.size() == rows.size());

    if (paths.isEmpty())
        return;

    // Build confirmation text
    QString text;
    if (names.size() == 1) {
        text = QString("Move to Recycle Bin:\n\n%1").arg(names.first());
    } else {
        text = QString("Move %1 items to Recycle Bin:\n\n").arg(names.size());
        int counter = 0;
        for (const QString& name : names)
        {
            if (counter > 30){
                text += "...";
                break;
            }
            text += name + '\n';
            counter++;
        }

    }

    /*auto reply = QMessageBox::warning(
        this,
        "Confirm delete",
        text,
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply != QMessageBox::Yes)
        return;*/

    if (!showDeleteConfirmDialog(names)) {
        return;
    }

    if (paths.contains("../") && current->parent){
        QMessageBox::critical(
            this,
            "Error",
            "Cannot delete parent directory (..)"
            );
        return;
    }

    // Move all to recycle bin first
    for (const QString& path : paths) {
        qInfo() << "multi deleting" << path;
        if (!moveToRecycleBin(path)) {
            QMessageBox::critical(
                this,
                "Error",
                "Failed to move one or more items to Recycle Bin."
                );
            return;
        }
    }

    // Remove from model (IMPORTANT: remove from highest row to lowest)
    QVector<int> sortedRows = rows;
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());


    for (int row : sortedRows) {
        if (current->parent){
            row = row-1;
            Node* node = current->children[row].get();
            current->size -= node->size;
            current->children.erase(current->children.begin() + row);
        }
    }

    populateTable(current);
}

bool MainWindow::showDeleteConfirmDialog(const QStringList& names)
{
    QDialog dialog(this);
    dialog.setWindowTitle("Confirm delete");
    dialog.setModal(true);
    dialog.resize(450, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

    QLabel* header = new QLabel(
        QString("Move %1 items to Recycle Bin:").arg(names.size())
        );
    mainLayout->addWidget(header);

    // Scroll area
    QScrollArea* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(150);


    QWidget* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout;
    content->setLayout(contentLayout);

    for (const QString& name : names) {
        QLabel* label = new QLabel(name);
        label->setWordWrap(true);
        contentLayout->addWidget(label);
    }

    scrollArea->setWidget(content);
    mainLayout->addWidget(scrollArea);

    // Buttons
    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No);
    mainLayout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);

    return dialog.exec() == QDialog::Accepted;
}


bool MainWindow::moveToRecycleBin(const QString& path)
{
    std::wstring wpath = path.toStdWString();
    wpath.push_back(L'\0'); // double-null terminated

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = wpath.c_str();
    op.fFlags =
        FOF_ALLOWUNDO |     // Recycle Bin
        FOF_NOCONFIRMATION |
        FOF_NOERRORUI |
        FOF_SILENT;

    return SHFileOperationW(&op) == 0;
}


void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Backspace) {
        goBack();
    }
    else {
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (isSaving) {
        // Already saving, ignore repeated close
        event->ignore();
        return;
    }

    if (!scanResultHash.isEmpty()) {
        // Hide window to stay responsive
        this->hide();

        // Start background save
        saveBeforeExit();

        // Ignore close event for now
        event->ignore();
    } else {
        event->accept(); // nothing to save
    }
}

void MainWindow::saveBeforeExit()
{
    if (isSaving) return;
    isSaving = true;

    // Snapshot the hash
    auto snapshot = scanResultHash;

    auto* watcher = new QFutureWatcher<bool>(this);

    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
        bool ok = watcher->future().result();
        qDebug() << "Background save finished:" << ok;
        watcher->deleteLater();

        // Now we can quit
        isSaving = false;
        qApp->quit();
    });

    watcher->setFuture(QtConcurrent::run([snapshot]() {
        return saveBinary(snapshot, "scans.dat");
    }));
}

