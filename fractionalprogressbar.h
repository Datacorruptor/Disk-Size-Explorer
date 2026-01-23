#ifndef FRACTIONALPROGRESSBAR_H
#define FRACTIONALPROGRESSBAR_H

#include <QProgressBar>

class FractionalProgressBar : public QProgressBar
{
public:
    FractionalProgressBar(QWidget *p = nullptr);

    // QProgressBar interface
public:
    virtual QString text() const Q_DECL_OVERRIDE;
};

#endif // FRACTIONALPROGRESSBAR_H
