#pragma once

#include <QMainWindow>

class InferenceEngine;
class QLineEdit;
class QPlainTextEdit;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(InferenceEngine& engine, QWidget* parent = nullptr);

private:
    void selectFile();
    void run();

    InferenceEngine& engine_;
    QLineEdit* filePath_ = nullptr;
    QPlainTextEdit* result_ = nullptr;
};
