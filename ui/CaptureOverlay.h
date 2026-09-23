#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <functional>
#include <vector>

class QScreen;

class CaptureOverlay final : public QWidget {
public:
    using Finished = std::function<void(const QImage&, bool recognizeTable)>;
    CaptureOverlay(Finished finished, QWidget* parent = nullptr);
    void begin();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    struct ScreenShot { QRect geometry; QPixmap pixmap; };
    void finish(bool table);
    void cancel();
    QImage selectedImage() const;
    Finished finished_;
    std::vector<ScreenShot> shots_;
    QRect bounds_;
    QPoint start_, current_;
    bool dragging_ = false;
    bool selected_ = false;
};
