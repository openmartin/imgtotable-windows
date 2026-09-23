#include "CaptureOverlay.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <algorithm>

namespace {
QRect actionBar(const QRect& selection, const QRect& screen) {
    int x=std::clamp(selection.left(),0,std::max(0,screen.width()-250));
    int y=selection.bottom()+8;
    if(y+35>screen.height()) y=selection.top()-43;
    return {x,std::max(0,y),250,35};
}
}

CaptureOverlay::CaptureOverlay(Finished finished, QWidget* parent)
    : QWidget(parent), finished_(std::move(finished)) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
}

void CaptureOverlay::begin() {
    shots_.clear(); bounds_={}; selected_=false;
    for(auto* screen:QGuiApplication::screens()) {
        auto pixmap=screen->grabWindow(0);
        if(pixmap.isNull()) continue;
        shots_.push_back({screen->geometry(),pixmap});
        bounds_=bounds_.isNull()?screen->geometry():bounds_.united(screen->geometry());
    }
    if(shots_.empty()) { cancel(); return; }
    setGeometry(bounds_); show(); raise(); activateWindow(); setFocus();
}

void CaptureOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(),Qt::black);
    for(const auto& shot:shots_) p.drawPixmap(shot.geometry.translated(-bounds_.topLeft()),shot.pixmap);
    p.fillRect(rect(),QColor(0,0,0,110));
    if(!selected_ && !dragging_) {
        p.setPen(Qt::white); p.drawText(rect().adjusted(0,24,0,0),Qt::AlignTop|Qt::AlignHCenter,
                                        tr("拖动选择区域 · Esc 取消"));
        return;
    }
    QRect box=QRect(start_,current_).normalized().intersected(rect());
    if(box.isEmpty()) return;
    p.setCompositionMode(QPainter::CompositionMode_Source);
    for(const auto& shot:shots_) {
        QRect target=shot.geometry.translated(-bounds_.topLeft()).intersected(box);
        if(target.isEmpty()) continue;
        QRectF source(target.topLeft()-shot.geometry.topLeft()+bounds_.topLeft(), target.size());
        source.setTopLeft(QPointF(target.topLeft()-shot.geometry.topLeft()+bounds_.topLeft())*shot.pixmap.devicePixelRatio());
        source.setSize(QSizeF(target.size())*shot.pixmap.devicePixelRatio());
        p.drawPixmap(target,shot.pixmap,source);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setPen(QPen(QColor("#2da8ff"),2)); p.drawRect(box);
    if(selected_) {
        QRect buttonBar=actionBar(box,rect());
        p.fillRect(buttonBar,QColor(30,30,30,235));
        p.setPen(Qt::white);
        p.drawText(buttonBar.adjusted(6,0,-125,0),Qt::AlignCenter,tr("截图"));
        p.drawText(buttonBar.adjusted(125,0,-4,0),Qt::AlignCenter,tr("截图并识别表格"));
    }
}

void CaptureOverlay::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton) return;
    QRect box=QRect(start_,current_).normalized();
    QRect bar=actionBar(box,rect());
    if(selected_ && bar.contains(e->pos())) { finish(e->pos().x()>=bar.left()+125); return; }
    selected_=false; dragging_=true; start_=current_=e->pos(); update();
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent* e) { if(dragging_) { current_=e->pos(); update(); } }
void CaptureOverlay::mouseReleaseEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton || !dragging_) return;
    dragging_=false; current_=e->pos();
    selected_=QRect(start_,current_).normalized().width()>2 && QRect(start_,current_).normalized().height()>2;
    update();
}
void CaptureOverlay::keyPressEvent(QKeyEvent* e) {
    if(e->key()==Qt::Key_Escape) cancel();
    else if(e->key()==Qt::Key_Return && selected_) finish(false);
    else if(e->key()==Qt::Key_T && selected_) finish(true);
}

QImage CaptureOverlay::selectedImage() const {
    QRect selection=QRect(start_,current_).normalized().intersected(rect()).translated(bounds_.topLeft());
    qreal dpr=1;
    for(const auto& shot:shots_) if(selection.intersects(shot.geometry)) dpr=std::max(dpr,shot.pixmap.devicePixelRatio());
    QImage result(selection.size()*dpr,QImage::Format_RGB32);
    result.fill(Qt::white);
    QPainter p(&result);
    for(const auto& shot:shots_) {
        QRect intersection=selection.intersected(shot.geometry);
        if(intersection.isEmpty()) continue;
        QRectF source(QPointF(intersection.topLeft()-shot.geometry.topLeft())*shot.pixmap.devicePixelRatio(),
                      QSizeF(intersection.size())*shot.pixmap.devicePixelRatio());
        QRectF target(QPointF(intersection.topLeft()-selection.topLeft())*dpr,QSizeF(intersection.size())*dpr);
        p.drawPixmap(target,shot.pixmap,source);
    }
    return result;
}
void CaptureOverlay::finish(bool table) { auto image=selectedImage(); hide(); finished_(image,table); deleteLater(); }
void CaptureOverlay::cancel() { hide(); finished_({},false); deleteLater(); }
