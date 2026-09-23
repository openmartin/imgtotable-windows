#include "MainWindow.h"
#include "core/InferenceEngine.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(InferenceEngine& engine, QWidget* parent)
    : QMainWindow(parent), engine_(engine) {
    setWindowTitle(tr("ImgToTable"));
    resize(760, 480);

    // Qt parent ownership manages all widgets and layouts below.
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    auto* title = new QLabel(tr("ImgToTable — 本地文件处理工具"), central);
    auto font = title->font();
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    auto* description = new QLabel(tr("选择文件后运行。当前为项目骨架，尚未接入文件预处理和结果解析。"), central);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* row = new QHBoxLayout;
    auto* select = new QPushButton(tr("选择文件"), central);
    filePath_ = new QLineEdit(central);
    filePath_->setReadOnly(true);
    filePath_->setPlaceholderText(tr("尚未选择文件"));
    filePath_->setAccessibleName(tr("文件路径"));
    auto* runButton = new QPushButton(tr("运行"), central);
    row->addWidget(select);
    row->addWidget(filePath_, 1);
    row->addWidget(runButton);
    layout->addLayout(row);
    result_ = new QPlainTextEdit(central);
    result_->setReadOnly(true);
    result_->setAccessibleName(tr("结果输出"));
    layout->addWidget(result_, 1);

    connect(select, &QPushButton::clicked, this, [this] { selectFile(); });
    connect(runButton, &QPushButton::clicked, this, [this] { run(); });
    if (engine_.isModelLoaded()) {
        statusBar()->showMessage(tr("模型已加载（CPU）"));
        result_->setPlainText(tr("模型已就绪。请接入与模型匹配的预处理和后处理逻辑。"));
    } else {
        statusBar()->showMessage(tr("模型未就绪"));
        result_->setPlainText(tr("无法初始化模型。请检查 ONNX Runtime 配置及 models/model.onnx。\n\n详细信息：\n%1")
                                 .arg(QString::fromStdString(engine_.lastError())));
    }
}

void MainWindow::selectFile() {
    const auto path = QFileDialog::getOpenFileName(this, tr("选择输入文件"));
    if (!path.isEmpty()) {
        filePath_->setText(path);
        statusBar()->showMessage(tr("已选择文件"));
    }
}

void MainWindow::run() {
    const QFileInfo file(filePath_->text());
    if (filePath_->text().isEmpty() || !file.isFile() || !file.isReadable()) {
        result_->setPlainText(tr("请先选择一个可读取的文件。"));
        statusBar()->showMessage(tr("输入文件不可用"));
        return;
    }
    if (!engine_.isModelLoaded()) {
        qWarning().noquote() << QString::fromStdString(engine_.lastError());
        result_->setPlainText(tr("模型尚未加载：\n%1").arg(QString::fromStdString(engine_.lastError())));
        statusBar()->showMessage(tr("模型未就绪"));
        return;
    }
    // TODO: preprocess the selected file into the model's tensors.
    // Before calling engine_.run(), move preprocessing + inference to one worker
    // (e.g. QThread). Marshal results to the UI and join the worker on shutdown.
    // Never call the real synchronous inference API on the GUI thread.
    result_->setPlainText(tr("已选择：%1\n\n尚未接入模型专用的预处理、后台推理和结果解析；本次未执行推理。")
                             .arg(file.absoluteFilePath()));
    statusBar()->showMessage(tr("等待接入处理流程"));
}
