#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

class LoadingDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoadingDialog(QWidget* parent = nullptr);
    void setMessage(const QString& message);
    void setProgress(int value);
    void setMaximum(int maximum);
    int progress() const { return progressBar->value(); }

private:
    QLabel* messageLabel;
    QProgressBar* progressBar;
}; 