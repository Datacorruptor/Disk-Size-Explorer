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
#include <QShortcut>

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
    COLUMN_CHILDREN_OBJECTS_LEN,
    COLUMN_EDITDATE,
    COLUMN_TYPE,
    COLUMNs_COUNT
};

QString prettySizeMain(uint64_t b) {
    const char* suf[] = {"B","KB","MB","GB","TB"};
    double v = b;
    int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; i++; }
    return QString("%1 %2").arg(v, 0, 'f', 2).arg(suf[i]);
}

QString prettyNumberMain(qint64 value)
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

static QString joinPathMain(const QString& base,
                        const QString& name)
{
    if (base.isEmpty())
        return name;

    if (base.back() == '\\')
        return base + name;

    return base + "\\" + name;
}


struct NodeDisk {
    QString name;
    quint64 size;
    quint64 childrenSize;
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
    d.childrenSize = node->children_objects_len;
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
    out << quint16(2);          // version

    out << quint32(hash.size());

    for (const auto& root : hash) {
        QVector<NodeDisk> flat;
        QHash<const Node*, int> indexMap;

        flattenTree(root, flat, indexMap);

        out << quint32(flat.size());
        for (const auto& d : flat) {
            out << d.name
                << d.size
                << d.childrenSize
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

    if (magic != 0x5343414E || version != 2)
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
                >> nodes[i]->children_objects_len
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
        backlog.clear();
        qInfo() << "node" <<current->children[row]->name;
        //pathEdit->setText(joinPathWin(pathEdit->text().toStdString(), current->children[row]->name).c_str());
        pathEdit->setCurrentText(current->children[row]->path());
        populateTable(current->children[row]);
    } else{
        QUrl fileUrl = QUrl::fromLocalFile(current->children[row]->path());
        QDesktopServices::openUrl(fileUrl);
    }

}



int last_index = -1;
int last_index_count = 0;

void MainWindow::sectionClick(const int index){
    qInfo() << "section click! " << index;

    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    auto order = Qt::DescendingOrder;

    if (last_index == index){
        if (last_index_count % 2 == 0)
            order = Qt::AscendingOrder;
        last_index_count++;
    }else{
        last_index_count=0;
    }

    proxyModel->sort(index, order);
    table->horizontalHeader()->setSortIndicator(index, order);
    table->horizontalHeader()->setSortIndicatorShown(true);


    last_index = index;

}

void MainWindow::goBack() {
    if (current){
        qInfo() << "going back, current "<<current->path() << ", current parent" << current->parent.get();

        if (current->parent) {
            backlog.append(current);
            pathEdit->setCurrentText(current->parent->path());
            populateTable(current->parent, current->name);
        }
    }
}

void MainWindow::goFwd() {
    if (current){
        qInfo() << "going forward, current "<<current->path();

        if (!backlog.isEmpty()) {
            auto last = backlog.takeLast();
            pathEdit->setCurrentText(last->path());
            populateTable(last);
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
    refreshButton = new QPushButton("Refresh", this);
    backBtn = new QPushButton("");
    backBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    backBtn->setMaximumWidth(30);

    fwdBtn = new QPushButton("");
    fwdBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    fwdBtn->setMaximumWidth(30);

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

    table->horizontalHeader()->resizeSection(COLUMN_SIZE, 70);
    table->horizontalHeader()->resizeSection(COLUMN_CHILDREN_OBJECTS_LEN, 50);
    table->horizontalHeader()->resizeSection(COLUMN_TYPE, 50);
    //table->horizontalHeader()->setSectionResizeMode(table->columnCount()-1, QHeaderView::Fixed);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto* header = table->horizontalHeader();
    header->setStyleSheet(
        "QHeaderView::section:sortIndicator {"
        "   font-weight: bold;"
        "}"
        );

    // Allow user resizing
    header->setSectionsMovable(true);
    header->setStretchLastSection(false);
    header->setSortIndicatorShown(true);
    //header->setSortIndicatorEnabled(false);

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
    proxyModel->sort(COLUMN_INDEX, Qt::AscendingOrder);

    topLayout->addWidget(backBtn);
    topLayout->addWidget(fwdBtn);
    topLayout->addWidget(driveCombo);
    topLayout->addWidget(pathEdit,1);
    topLayout->addWidget(scanButton);
    topLayout->addWidget(refreshButton);


    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(table);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(label);


    qInfo() << "An informational message2.";

    loadBinary(scanResultHash, "scans.dat");

    resize(900, 600);

    connect(scanButton, &QPushButton::clicked,
            this, &MainWindow::startScan);

    connect(refreshButton, &QPushButton::clicked,
            this, [this](){
                startPartScan(current);
            });

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
    connect(fwdBtn, &QPushButton::clicked, this, &MainWindow::goFwd);
    //connect(table, &QTableWidget::itemDoubleClicked, this, &MainWindow::navigate);

    connect(table, &QTableView::doubleClicked, this, &MainWindow::navigate);

    connect(header, &QHeaderView::sectionClicked, this, &MainWindow::sectionClick);


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

    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this, SLOT(goBack()));
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), this, SLOT(goBack()));
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this, SLOT(goFwd()));

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

    label->setText(prettySizeMain(processedSize));
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
    proxyModel->sort(COLUMN_INDEX,Qt::AscendingOrder);
    populateTable(current);
    table->setDisabled(false);
    progressBar->setValue(10000);
    progressBar->setEnabled(false);

}


bool MainWindow::deleteFromNodes(int row){
    Node* node_delete = current->children[row].get();
    current->size -= node_delete->size;
    current->children_objects_len -= node_delete->children_objects_len+1;

    std::shared_ptr<Node> node_current = current;
    while(node_current->parent){
        node_current->parent->size -= node_delete->size;
        node_current->parent->children_objects_len -= node_delete->children_objects_len+1;
        node_current = node_current->parent;
    }
    current->children.erase(current->children.begin() + row);
    return true;
}

void MainWindow::populateTable(std::shared_ptr<Node> node, QString select)
{
    qInfo() << "started populating table";

    //proxyModel->sort(COLUMN_SIZE, Qt::DescendingOrder);
    table->setUpdatesEnabled(false);
    table->setSortingEnabled(false);

    qInfo() << "Populating table:" << node->path();
    current = node;
    QElapsedTimer timer;
    timer.start();

    QVector<FileRow> rows;
    rows.reserve(node->children.size());

    bool check_actuality = true;

    QDir dir(node->path());
    dir.setFilter(
        QDir::NoDotAndDotDot |
        QDir::AllEntries |
        QDir::Hidden |
        QDir::System
        );

    qInfo() << "created dir filters";

    if (node->children.size()<10000){

        for (int i =0 ;i < dir.count();i++){
            qInfo()<<dir.entryList()[i];
        }
        qInfo()<<" ";

        for (int i =0 ;i < node->children.size();i++){
            qInfo()<<node->children[i]->name;
        }


        QFileInfoList real_files_info = dir.entryInfoList();
        QStringList real_files = dir.entryList();
        QSet<QString> real_files_set(real_files.begin(), real_files.end());

        QStringList real_nodes_names;
        for (const auto& child : node->children) {
            real_nodes_names.append(child->name);
        }
        QSet<QString> real_nodes_names_set(real_nodes_names.begin(), real_nodes_names.end());

        QVector<int> toAdd;
        QVector<int> toDelete;


        for (int i =0;i< real_files.size();i++){
            const auto& file = real_files[i];
            if(!real_nodes_names_set.contains(file)){
                qInfo()<< "real_nodes_names_set does not contain "+file;
                toAdd.append(i);
                check_actuality = false;
            }

        }
        std::sort(toAdd.begin(), toAdd.end(), std::greater<int>());


        for (int i =0;i< real_nodes_names.size();i++) {
            const auto& node_name = real_nodes_names[i];
            if(!real_files_set.contains(node_name)){
                qInfo()<< "real_files_set does not contain "+node_name;
                toDelete.append(i);
                check_actuality = false;
            }
        }
        std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());

        for (int row : toDelete) {
            deleteFromNodes(row);
        }

        for (int row : toAdd) {

            auto child = std::make_shared<Node>();
            auto info = real_files_info[row];
            child->name = info.fileName();
            child->cachedPath = joinPathMain(node->path(), info.fileName());
            child->parent = node;
            child->isDir = info.isDir();
            child->lastModified = info.lastModified().toSecsSinceEpoch();

            if (real_files_info[row].isDir()){
                child->size = 0;
            }else{
                child->size = info.size();
            }

            current->size += child->size;
            current->children_objects_len += 1;

            std::shared_ptr<Node> node_current = current;
            while(node_current->parent){
                node_current->parent->size += child->size;
                node_current->parent->children_objects_len += 1;
                node_current = node_current->parent;
                std::sort(node_current->children.begin(), node_current->children.end(),
                          [](const auto& a, const auto& b) {
                              return a->size > b->size;
                          });
            }

            current->children.push_back(std::move(child));
        }

        if (toAdd.size()>0){
            std::sort(current->children.begin(), current->children.end(),
                      [](const auto& a, const auto& b) {
                          return a->size > b->size;
                      });
        }


        if (check_actuality){
            qInfo()<< "Actuality check completed successfully";
        }
    }


    int select_row = -10;
    for (int i = 0; i < node->children.size();i++){

        const auto& child = node->children[i];
        rows.push_back({
            child->name,
            child->path(),
            child->size,
            child->children_objects_len,
            QDateTime::fromSecsSinceEpoch(child->lastModified),
            child->isDir,
            child->isDir ? folderPlaceholder : filePlaceholder
        });
    }


    fileModel->setRows(std::move(rows));

    //proxyModel->sort(COLUMN_INDEX, Qt::AscendingOrder);

    if (node->parent)
    {
        fileModel->prependRow({"..","../",node->size+1,node->children_objects_len,QDateTime(),1,filePlaceholder});
    }

    label->setText(prettySizeMain(node->size));

    table->setUpdatesEnabled(true);
    //table->setSortingEnabled(true);

    table->viewport()->update();

    QModelIndex start = proxyModel->sourceModel()->index(0, COLUMN_NAME);
    auto matches = proxyModel->sourceModel()->match(start,Qt::DisplayRole,select,1,Qt::MatchExactly);

    if (!matches.isEmpty()) {
        QModelIndex proxyIndex = proxyModel->mapFromSource(matches.first());
        table->selectionModel()->select(proxyIndex,QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        table->scrollTo(proxyIndex);
    }

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
            fileModel->changeRowSize(Name2Index[node->name],l1Size,l1Files);
            timer.restart();
        }
    }

}



void MainWindow::onNodeFinished(uint64_t newSize, std::shared_ptr<Node> node)
{
    processedSize+=newSize;
    QMetaObject::invokeMethod(this, "updateProgress", Qt::QueuedConnection);

    if (node){
        //qInfo() << "finalizing row table" << node->path();
        l1Size = 0;
        l1Files = 0;
        //fileModel->changeLastRowSize(node->size);
        //table->setSortingEnabled(true);
    }

}


void MainWindow::onNodeStarted(uint64_t newSize,std::shared_ptr<Node> node)
{
    table->setSortingEnabled(false);
    //qInfo() << "adding row table" << node->path();

    Name2Index[node->name] = fileModel->insertRow({
        node->name,
        node->path(),
        node->size,
        node->children_objects_len,
        QDateTime::fromSecsSinceEpoch(node->lastModified),
        node->isDir,
        node->isDir ? folderPlaceholder : filePlaceholder
    });


    proxyModel->sort(COLUMN_INDEX, Qt::DescendingOrder);


}


void MainWindow::showContextMenu(const QPoint& pos)
{
    //int selected_row = table->rowAt(pos.y());
    const QModelIndex &selected_index = table->indexAt(pos);
    QModelIndex selected_src = proxyModel->mapToSource(selected_index);
    int selected_row = selected_src.row();

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
        QStringList sizes;
        QVector<int> rows;
        QVector<Node*> nodes;

        QList<QModelIndex> selectedIndexes = table->selectionModel()->selectedRows();

        foreach (const QModelIndex &index, selectedIndexes) {
            //int row = index.row();

            QModelIndex src = proxyModel->mapToSource(index);
            int row = src.row();

            /*QModelIndex firstColumnIndex = index.sibling(index.row(), 0);
            QVariant value = firstColumnIndex.data();
            int row = value.toInt();*/

            QString path = fileModel->pathAt(row);
            QString name = fileModel->nameAt(row);
            QString size = prettySizeMain(fileModel->sizeAt(row));

            qInfo() << "selected row:" <<row << " " << name << " " << size;

            rows.append(row);
            names.append(name);
            paths.append(path);
            sizes.append(size);



        }

        confirmMultiDelete(paths, names, sizes, rows, false);
    }

    if (chosen == open){



        Node* node = current->children[selected_row].get();
        QString path = QDir::toNativeSeparators(node->path());
        QProcess::startDetached("explorer", {"/select,", path});

        /*QFileInfo fileInfo(node->path());
        QString dirPath = fileInfo.absoluteDir().absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));*/
    }
}

void MainWindow::confirmMultiDelete(const QStringList& paths,
                               const QStringList& names,
                               const QStringList& sizes,
                               const QVector<int>& rows,
                               bool foreverDelete)
{
    Q_ASSERT(paths.size() == names.size());
    Q_ASSERT(paths.size() == rows.size());

    if (paths.isEmpty())
        return;

    if (!showDeleteConfirmDialog(names, sizes, foreverDelete)) {
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

    qInfo() << "invoking deletion in a separate thread";

    table->setEnabled(false);


    auto future = QtConcurrent::run([paths, foreverDelete, this]() -> std::pair<bool, QString> {
        int processed = 0;
        int total_paths = paths.size();

        for (const QString& path : paths) {

            QMetaObject::invokeMethod(this, [=]() {
                progressBar->setValue(processed * 10000 / total_paths);
                label->setText("Deleting "+ path);
            }, Qt::QueuedConnection);

            auto [success, error] = moveToRecycleBin(path, foreverDelete);
            if (!success)
                return {false, error};

            processed++;
        }

        QMetaObject::invokeMethod(this, [=]() {
            progressBar->setValue(10000);
        }, Qt::QueuedConnection);

        return {true, ""};
    });


    auto *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);

    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [=]() {
        auto [success, error] = watcher->result();
        if (!success) {


            QMessageBox::critical(
                this,
                "Error",
                "System prevented this action:\n" + error
                );
        } else {
            // model + UI updates stay on GUI thread
            // Remove from model (IMPORTANT: remove from highest row to lowest)
            QVector<int> sortedRows = rows;
            std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());


            for (int row : sortedRows) {
                if (current->parent){
                    row = row-1;
                }
                deleteFromNodes(row);
            }
            populateTable(current);
        }

        table->setEnabled(true);
        watcher->deleteLater();
    });

    watcher->setFuture(future);
}

bool MainWindow::showDeleteConfirmDialog(const QStringList& names, const QStringList& sizes, bool foreverDelete)
{
    QDialog dialog(this);
    dialog.setWindowTitle("Confirm delete");
    dialog.setModal(true);
    dialog.resize(450, 300);


    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);


    QString headerText = QString("Move %1 items to Recycle Bin:").arg(names.size());
    if (foreverDelete){
        headerText = QString("Are you sure you want to PERMANENTLY DELETE these %1 items:").arg(names.size());
    }

    QLabel* header = new QLabel(headerText);
    mainLayout->addWidget(header);

    // Scroll area
    QScrollArea* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(150);


    QWidget* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout(content);

    for (int i = 0; i < names.size(); ++i) {

        auto* contentLayoutLine = new QHBoxLayout;

        QLabel* label1 = new QLabel(names[i]);
        QLabel* label2 = new QLabel(sizes[i]);

        contentLayoutLine->addWidget(label1);
        contentLayoutLine->addStretch();
        contentLayoutLine->addWidget(label2);

        contentLayout->addLayout(contentLayoutLine);

    }


    /*QWidget* content = new QWidget(&dialog);
    auto* grid = new QGridLayout(content);

    for (int i = 0; i < names.size(); ++i) {
        QLabel* nameLabel = new QLabel(names[i]);
        QLabel* sizeLabel = new QLabel("[" + sizes[i] + "]");

        sizeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        grid->addWidget(nameLabel, i, 0);
        grid->addWidget(sizeLabel, i, 1);
    }

    // push column 1 to the right
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 0);

    */
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


std::pair<bool, QString> MainWindow::moveToRecycleBin(const QString& path, bool foreverDelete)
{
    qInfo() << "Recycle thread:" << QThread::currentThread();

    QFileInfo info(path);
    if (!info.exists()) {
        return {false, "file or directory does not exist " + path};
    }

    if (foreverDelete) {
        if (info.isFile()) {
            QFile file(path);
            if (!file.remove()) {
                return {false, file.errorString() + " " + path};
            } else{
                return {true, ""};
            }
        } else if (info.isDir()) {
            QDir dir(path);
            if (!dir.removeRecursively()) {
                return {false, "Failed to remove directory " + path};
            }else{
                return {true, ""};
            }
        }
        else{
            return {false, "Unknown object type" + path};
        }
    } else {
        // move to trash
        if (!QFile::moveToTrash(path)) {  // works for files and directories, but may fail if locked
            return {false, "Failed to move to trash " + path};
        }
        else{
            return {true, ""};
        }
    }
}



void MainWindow::keyPressEvent(QKeyEvent *event)
{
    qInfo() << "Key Event!!";
    bool altPressed = event->modifiers() & Qt::AltModifier;
    bool shiftPressed = event->modifiers() & Qt::ShiftModifier;

    switch (event->key()) {

    case  Qt::Key_Backspace:
        goBack();
        break;

    case Qt::Key_Delete:{


        QStringList paths;
        QStringList names;
        QStringList sizes;
        QVector<int> rows;
        QVector<Node*> nodes;

        QList<QModelIndex> selectedIndexes = table->selectionModel()->selectedRows();

        foreach (const QModelIndex &index, selectedIndexes) {
            QModelIndex src = proxyModel->mapToSource(index);
            int row = src.row();

            QString path = fileModel->pathAt(row);
            QString name = fileModel->nameAt(row);
            QString size = prettySizeMain(fileModel->sizeAt(row));

            qInfo() << "selected row:" <<row << " " << name << " " << size;

            rows.append(row);
            names.append(name);
            paths.append(path);
            sizes.append(size);
        }

        confirmMultiDelete(paths, names, sizes, rows, shiftPressed);



        break;}

    default:
        QMainWindow::keyPressEvent(event);
        break;
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

