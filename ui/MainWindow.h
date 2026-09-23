#pragma once

#include "core/Recognizer.h"

#include <QImage>
#include <QMainWindow>
#include <thread>
#include <vector>

class GlobalHotkey;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QScrollArea;
class QSystemTrayIcon;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void beginCapture();
protected:
    void closeEvent(QCloseEvent*) override;
private:
    void openImage();
    void setImage(const QImage&, bool recognizeTable);
    void showImage();
    void commitImage(const QImage&);
    void cropImage(const QRect&);
    void rotateImage(int degrees);
    void changeHistory(int delta);
    void recognize(bool table);
    void copyCsv();
    void exportCsv();
    QString currentCsv() const;
    void configureShortcut();
    void setBusy(bool busy);
    void clearTable();
    void showTable(const TableResult& table);

    struct EditableCell { int row = 0; int column = 0; QLineEdit* edit = nullptr; };

    Recognizer recognizer_;
    std::thread worker_;
    std::vector<QImage> history_;
    int historyIndex_ = -1;
    bool busy_ = false;
    bool cropMode_ = false;
    bool exiting_ = false;
    unsigned long long imageVersion_ = 0;
    QLabel* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QPlainTextEdit* textResult_ = nullptr;
    QWidget* tablePanel_ = nullptr;
    QGridLayout* tableLayout_ = nullptr;
    std::vector<EditableCell> tableCells_;
    int tableRows_ = 0;
    int tableColumns_ = 0;
    QPushButton* textButton_ = nullptr;
    QPushButton* tableButton_ = nullptr;
    GlobalHotkey* hotkey_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
};
