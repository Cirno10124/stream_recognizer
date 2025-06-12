#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

class LoadingDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoadingDialog(QWidget* parent = nullptr);
    ~LoadingDialog();
    void setMessage(const QString& message);
    void setProgress(int value);
    void setMaximum(int maximum);
    int progress() const { return progressBar ? progressBar->value() : 0; }

private:
    QLabel* messageLabel;
    QProgressBar* progressBar;
}; 