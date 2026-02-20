#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QMenu>
#include <QMessageBox>
#include <atomic>
#include <QResizeEvent>
#include <QSortFilterProxyModel>
#include "filetablemodel.h"
#include "loadingdialog.h"

#include <atomic>
#include <qfileiconprovider.h>

#include "scanner.h"



class AutoStretchTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTableWidget::resizeEvent(event);

        adjustStretchColumn(-1, event->oldSize().width(), event->size().width());
    }

public:
    void adjustStretchColumn(int logicalIndex, int oldSize, int newSize)
    {

        /*if (logicalIndex == columnCount()-1)
        {
            setColumnWidth(logicalIndex, oldSize);
            return;
        }*/


        int stretchCol = 1;
        int available = viewport()->width();

        for (int c = 0; c < columnCount(); ++c) {
            if (c != stretchCol)
                available -= columnWidth(c);
        }

        int minWidth = 80; // optional
        if (available < minWidth)
            available = minWidth;

        if (columnWidth(stretchCol) != available)
            setColumnWidth(stretchCol, available);
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void startScan();
    void startPartScan(std::shared_ptr<Node> node);
    void scan(std::shared_ptr<Node> node);
    void scanFinished();
    void updateProgress();
    void showContextMenu(const QPoint& pos);
    void navigate(const QModelIndex& index);
    void sectionClick(const int index);
    void goBack();
    void goFwd();

private:
    void setupUi();
    void populateTable(std::shared_ptr<Node> node, QString select = "");
    void onNodeStarted(uint64_t newSize, std::shared_ptr<Node> node);
    void onNodeUpdated(uint64_t newSize, std::shared_ptr<Node> node);
    void onNodeFinished(uint64_t newSize, std::shared_ptr<Node> node);
    void onScanFinished(uint64_t newSize, std::shared_ptr<Node> node);
    void confirmMultiDelete(const QStringList& paths,const QStringList& names,const QStringList& sizes,const QVector<int>& rows, bool foreverDelete);
    std::pair<bool, QString> moveToRecycleBin(const QString& path, bool foreverDelete);
    QString joinPathWin(const QString& base, const QString& name);
    void saveInBackground(QHash<QString, std::shared_ptr<Node>> scanResultHash);
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool showDeleteConfirmDialog(const QStringList& names, const QStringList& sizes, bool foreverDelete);
    bool loadBinary(QHash<QString, std::shared_ptr<Node>>& hash,const QString& fileName);
    void saveBeforeExit();

    bool deleteFromNodes(int id);

    // UI
    QComboBox* pathEdit;
    QPushButton* scanButton;
    QPushButton* refreshButton;
    QPushButton* backBtn;
    QPushButton* fwdBtn;
    QProgressBar* progressBar;
    //AutoStretchTable* table;
    LoadingDialog *loader;

    QTableView* table;
    FileTableModel* fileModel;
    QSortFilterProxyModel* proxyModel;


    QComboBox* driveCombo;
    QLabel* label;

    // Scanner
    DirectoryScanner* scanner;
    std::shared_ptr<Node> root;
    std::shared_ptr<Node> current = nullptr;
    QHash<QString, std::shared_ptr<Node>> scanResultHash;
    QHash<QString, int> Name2Index;
    uint64_t totalFiles = 0;
    uint64_t totalSize = 0;
    uint64_t l1Size = 0;
    uint64_t l1Files = 0;
    uint64_t processedFiles = 0;
    uint64_t processedSize = 0;
    std::atomic<bool> cancel{ false };
    bool isSaving = false;

    QVector<std::shared_ptr<Node>> backlog;

    QFileIconProvider iconProvider;

};
