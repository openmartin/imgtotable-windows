#include "MainWindow.h"
#include "CaptureOverlay.h"
#include "platform/GlobalHotkey.h"
#include "platform/PlatformUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QGridLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QTransform>
#include <algorithm>
#include <chrono>
#include <functional>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace {
class ImageCanvas final : public QLabel {
public:
    std::function<void(QRect)> cropped;
    bool cropMode = false;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if(cropMode && e->button()==Qt::LeftButton) { start_=end_=e->pos(); dragging_=true; update(); }
        else QLabel::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override { if(dragging_) { end_=e->pos(); update(); } }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if(dragging_ && e->button()==Qt::LeftButton) {
            dragging_=false; end_=e->pos(); update();
            auto r=QRect(start_,end_).normalized().intersected(rect());
            if(r.width()>2 && r.height()>2 && cropped) cropped(r);
        }
    }
    void paintEvent(QPaintEvent* e) override {
        QLabel::paintEvent(e);
        if(dragging_) { QPainter p(this); p.setPen(QPen(QColor("#2da8ff"),2)); p.fillRect(QRect(start_,end_).normalized(),QColor(45,168,255,50)); p.drawRect(QRect(start_,end_).normalized()); }
    }
private:
    QPoint start_, end_;
    bool dragging_=false;
};

RecognitionImage toRecognitionImage(const QImage& source) {
    QImage image=source.convertToFormat(QImage::Format_RGB888);
    RecognitionImage result{image.width(),image.height(),std::vector<std::uint8_t>(static_cast<size_t>(image.width())*image.height()*3)};
    for(int y=0;y<image.height();++y)
        std::copy_n(image.constScanLine(y),static_cast<size_t>(image.width())*3,
                    result.rgb.data()+static_cast<size_t>(y)*image.width()*3);
    return result;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), recognizer_(PlatformUtils::toPath(PlatformUtils::modelPath())) {
    setWindowTitle(tr("ImgToTable")); resize(1100,760);
    auto* central=new QWidget(this); setCentralWidget(central);
    auto* root=new QVBoxLayout(central);
    auto* controls=new QHBoxLayout;
    auto button=[&](const QString& text, auto action) { auto* b=new QPushButton(text,central); controls->addWidget(b); connect(b,&QPushButton::clicked,this,action); return b; };
    button(tr("开始截图"),[this]{beginCapture();});
    button(tr("打开图片"),[this]{openImage();});
    button(tr("裁剪"),[this]{cropMode_=true; static_cast<ImageCanvas*>(canvas_)->cropMode=true; statusBar()->showMessage(tr("拖动图片选择裁剪区域"));});
    button(tr("左转"),[this]{rotateImage(-90);});
    button(tr("右转"),[this]{rotateImage(90);});
    button(tr("撤销"),[this]{changeHistory(-1);});
    button(tr("重做"),[this]{changeHistory(1);});
    button(tr("复制图片"),[this]{if(historyIndex_>=0) QApplication::clipboard()->setImage(history_[historyIndex_]);});
    button(tr("保存 PNG"),[this]{if(historyIndex_<0)return; auto p=QFileDialog::getSaveFileName(this,tr("保存图片"),{},tr("PNG 图片 (*.png)")); if(!p.isEmpty() && !history_[historyIndex_].save(p,"PNG")) QMessageBox::warning(this,tr("保存失败"),tr("无法写入图片。"));});
    root->addLayout(controls);
    auto* panes=new QHBoxLayout;
    scroll_=new QScrollArea(central); scroll_->setWidgetResizable(false);
    canvas_=new ImageCanvas; canvas_->setAlignment(Qt::AlignTop|Qt::AlignLeft);
    canvas_->setText(tr("开始截图或打开图片")); canvas_->resize(450,350);
    static_cast<ImageCanvas*>(canvas_)->cropped=[this](QRect rect){cropImage(rect);};
    scroll_->setWidget(canvas_); panes->addWidget(scroll_,3);
    auto* right=new QVBoxLayout;
    auto* actions=new QHBoxLayout;
    textButton_=new QPushButton(tr("识别文字"),central);
    tableButton_=new QPushButton(tr("识别表格"),central);
    actions->addWidget(textButton_); actions->addWidget(tableButton_);
    connect(textButton_,&QPushButton::clicked,this,[this]{recognize(false);});
    connect(tableButton_,&QPushButton::clicked,this,[this]{recognize(true);});
    right->addLayout(actions);
    auto* tabs=new QTabWidget(central);
    textResult_=new QPlainTextEdit(tabs); textResult_->setPlaceholderText(tr("文字识别结果"));
    auto* tableScroll=new QScrollArea(tabs);
    tableScroll->setWidgetResizable(true);
    tablePanel_=new QWidget(tableScroll);
    tableLayout_=new QGridLayout(tablePanel_);
    tableLayout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    tableScroll->setWidget(tablePanel_);
    tabs->addTab(textResult_,tr("文字")); tabs->addTab(tableScroll,tr("表格")); right->addWidget(tabs,1);
    auto* exportRow=new QHBoxLayout;
    auto* copy=new QPushButton(tr("复制 CSV"),central);
    auto* save=new QPushButton(tr("导出 CSV"),central);
    exportRow->addWidget(copy); exportRow->addWidget(save);
    connect(copy,&QPushButton::clicked,this,[this]{copyCsv();});
    connect(save,&QPushButton::clicked,this,[this]{exportCsv();});
    right->addLayout(exportRow); panes->addLayout(right,2); root->addLayout(panes,1);
    statusBar()->showMessage(tr("就绪"));

    hotkey_=new GlobalHotkey([this]{beginCapture();},this);
    QSettings settings;
    QString saved=settings.value("captureShortcut",QStringLiteral("Ctrl+Shift+2")).toString();
    QString error;
    if(!hotkey_->setShortcut(QKeySequence(saved),error)) statusBar()->showMessage(error);
    auto* trayMenu=new QMenu(this);
    trayMenu->addAction(tr("开始截图"),this,[this]{beginCapture();});
    trayMenu->addAction(tr("打开图片"),this,[this]{openImage();});
    trayMenu->addAction(tr("显示窗口"),this,[this]{showNormal();raise();activateWindow();});
    trayMenu->addAction(tr("设置截图快捷键"),this,[this]{configureShortcut();});
    trayMenu->addSeparator();
    trayMenu->addAction(tr("退出"),this,[this]{exiting_=true; qApp->quit();});
    tray_=new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon),this);
    tray_->setContextMenu(trayMenu); tray_->setToolTip(tr("ImgToTable")); tray_->show();
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
        if(reason==QSystemTrayIcon::Trigger || reason==QSystemTrayIcon::DoubleClick) {showNormal();raise();activateWindow();}
    });
}

MainWindow::~MainWindow() { if(worker_.joinable()) worker_.join(); }
void MainWindow::closeEvent(QCloseEvent* event) { if(exiting_) event->accept(); else { event->ignore(); hide(); } }

void MainWindow::beginCapture() {
    if(busy_) {statusBar()->showMessage(tr("请等待当前识别完成。")); return;}
#ifdef Q_OS_MACOS
    if(!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        QMessageBox::warning(this,tr("需要截屏权限"),tr("请在系统设置的屏幕录制权限中允许 ImgToTable，然后重新启动应用。")); return;
    }
#endif
    const bool restoreOnCancel=isVisible();
    hide();
    QTimer::singleShot(250,this,[this,restoreOnCancel]{
        auto* overlay=new CaptureOverlay([this,restoreOnCancel](const QImage& image,bool table){
            if(image.isNull()) {if(restoreOnCancel) showNormal(); return;}
            setImage(image,table); showNormal();raise();activateWindow();
        });
        overlay->begin();
    });
}

void MainWindow::openImage() {
    auto path=QFileDialog::getOpenFileName(this,tr("打开图片"),{},tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if(path.isEmpty()) return;
    QImageReader reader(path); reader.setAutoTransform(true);
    auto image=reader.read();
    if(image.isNull()) QMessageBox::warning(this,tr("打开失败"),reader.errorString());
    else setImage(image,false);
}
void MainWindow::setImage(const QImage& image,bool table) {
    ++imageVersion_;
    cropMode_=false;
    static_cast<ImageCanvas*>(canvas_)->cropMode=false;
    history_.clear(); history_.push_back(image); historyIndex_=0;
    textResult_->clear(); clearTable();
    statusBar()->showMessage(tr("图片已打开"));
    showImage();
    if(table) recognize(true);
}
void MainWindow::showImage() {
    if(historyIndex_<0)return;
    canvas_->setPixmap(QPixmap::fromImage(history_[historyIndex_]));
    canvas_->resize(history_[historyIndex_].size());
}
void MainWindow::commitImage(const QImage& image) {
    if(image.isNull())return;
    ++imageVersion_;
    history_.resize(static_cast<size_t>(historyIndex_+1)); history_.push_back(image); ++historyIndex_;
    showImage(); textResult_->clear(); clearTable();
    statusBar()->showMessage(tr("图片已编辑"));
}
void MainWindow::cropImage(const QRect& rect) {
    cropMode_=false; static_cast<ImageCanvas*>(canvas_)->cropMode=false;
    if(historyIndex_>=0) commitImage(history_[historyIndex_].copy(rect));
}
void MainWindow::rotateImage(int degrees) {
    if(historyIndex_<0)return;
    QTransform transform;transform.rotate(degrees);
    commitImage(history_[historyIndex_].transformed(transform));
}
void MainWindow::changeHistory(int delta) {
    int next=historyIndex_+delta;
    if(next<0 || next>=static_cast<int>(history_.size()))return;
    ++imageVersion_; historyIndex_=next;showImage();textResult_->clear();clearTable();
    statusBar()->showMessage(tr("图片已恢复"));
}
void MainWindow::setBusy(bool busy) {
    busy_=busy; textButton_->setEnabled(!busy);tableButton_->setEnabled(!busy);
    statusBar()->showMessage(busy?tr("正在识别…"):tr("就绪"));
}
void MainWindow::recognize(bool table) {
    if(busy_ || historyIndex_<0)return;
    if(worker_.joinable())worker_.join();
    auto image=history_[historyIndex_];
    auto version=imageVersion_;
    setBusy(true);
    worker_=std::thread([this,image,table,version]{
        const auto started=std::chrono::steady_clock::now();
        std::vector<TextRegion> text;
        TableResult cells;
        bool ok=recognizer_.isLoaded() || recognizer_.load();
        if(ok) { auto rgb=toRecognitionImage(image); ok=table?recognizer_.recognizeTable(rgb,cells):recognizer_.recognizeText(rgb,text); }
        QString error=QString::fromUtf8(recognizer_.lastError());
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        QMetaObject::invokeMethod(this,[this,ok,table,version,error,elapsed,text=std::move(text),cells=std::move(cells)]{
            setBusy(false);
            if(version!=imageVersion_) {statusBar()->showMessage(tr("图片已更改，请重新识别。"));return;}
            if(!ok) {
                if(table) clearTable();
                else textResult_->clear();
                statusBar()->showMessage(error);
                QMessageBox::warning(this,tr("识别失败"),error);return;
            }
            if(table) {
                showTable(cells);
            } else {
                QStringList lines;for(const auto& item:text)lines<<QString::fromUtf8(item.text);
                textResult_->setPlainText(lines.join('\n'));
            }
            statusBar()->showMessage(tr("识别完成（%1 ms）").arg(elapsed));
        },Qt::QueuedConnection);
    });
}
void MainWindow::clearTable() {
    tableCells_.clear(); tableRows_=0; tableColumns_=0;
    while(auto* item=tableLayout_->takeAt(0)) {
        if(auto* widget=item->widget()) { widget->hide(); widget->deleteLater(); }
        delete item;
    }
}
void MainWindow::showTable(const TableResult& table) {
    clearTable();
    tableRows_=table.rows; tableColumns_=table.columns;
    std::vector<bool> occupied(static_cast<size_t>(table.rows)*table.columns);
    for(int c=0;c<table.columns;++c) tableLayout_->addWidget(new QLabel(QString::number(c+1),tablePanel_),0,c+1);
    for(int r=0;r<table.rows;++r) tableLayout_->addWidget(new QLabel(QString::number(r+1),tablePanel_),r+1,0);
    auto addCell=[&](int row,int col,int rowSpan,int colSpan,const QString& value) {
        auto* edit=new QLineEdit(value,tablePanel_);
        edit->setMinimumWidth(100);
        edit->setAccessibleName(tr("第 %1 行，第 %2 列").arg(row+1).arg(col+1));
        tableLayout_->addWidget(edit,row+1,col+1,rowSpan,colSpan);
        tableCells_.push_back({row,col,edit});
    };
    for(const auto& cell:table.cells) {
        if(cell.row<0 || cell.column<0 || cell.row>=table.rows || cell.column>=table.columns) continue;
        int rowSpan=std::clamp(cell.rowSpan,1,table.rows-cell.row);
        int colSpan=std::clamp(cell.columnSpan,1,table.columns-cell.column);
        addCell(cell.row,cell.column,rowSpan,colSpan,QString::fromUtf8(cell.text));
        for(int r=cell.row;r<cell.row+rowSpan;++r)
            for(int c=cell.column;c<cell.column+colSpan;++c)
                occupied[static_cast<size_t>(r)*table.columns+c]=true;
    }
    for(int r=0;r<table.rows;++r)for(int c=0;c<table.columns;++c)
        if(!occupied[static_cast<size_t>(r)*table.columns+c]) addCell(r,c,1,1,{});
}
QString MainWindow::currentCsv() const {
    TableResult table;table.rows=tableRows_;table.columns=tableColumns_;
    for(const auto& cell:tableCells_)
        table.cells.push_back({cell.row,cell.column,1,1,cell.edit->text().toUtf8().toStdString()});
    return QString::fromUtf8(tableToCsv(table));
}
void MainWindow::copyCsv() { QApplication::clipboard()->setText(currentCsv());statusBar()->showMessage(tr("已复制 CSV")); }
void MainWindow::exportCsv() {
    auto path=QFileDialog::getSaveFileName(this,tr("导出 CSV"),{},tr("CSV 文件 (*.csv)"));
    if(path.isEmpty())return;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)) {QMessageBox::warning(this,tr("导出失败"),file.errorString());return;}
    auto bytes=currentCsv().toUtf8();if(file.write(bytes)!=bytes.size())QMessageBox::warning(this,tr("导出失败"),file.errorString());
}
void MainWindow::configureShortcut() {
    QDialog dialog(this);dialog.setWindowTitle(tr("设置截图快捷键"));
    QFormLayout layout(&dialog);QKeySequenceEdit edit(hotkey_->shortcut(),&dialog);
    layout.addRow(tr("截图快捷键"),&edit);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    layout.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    QString error;
    if(!hotkey_->setShortcut(edit.keySequence(),error)) {QMessageBox::warning(this,tr("无法设置快捷键"),error);return;}
    QSettings settings;settings.setValue("captureShortcut",hotkey_->shortcut().toString());
}
