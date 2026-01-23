#ifndef LOADINGDIALOG_H
#define LOADINGDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

class LoadingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoadingDialog(QWidget *parent = nullptr)
        : QDialog(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setFixedSize(400, 150);
        setModal(true);

        QVBoxLayout *layout = new QVBoxLayout(this);

        m_label = new QLabel("Loading...", this);
        m_label->setAlignment(Qt::AlignCenter);

        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);

        layout->addWidget(m_label);
        layout->addWidget(m_progressBar);
        setLayout(layout);
    }

    void setMessage(const QString &msg) {
        m_label->setText(msg);
    }

    void setProgress(int value) {
        m_progressBar->setValue(value);
    }

private:
    QLabel *m_label;
    QProgressBar *m_progressBar;
};

#endif
