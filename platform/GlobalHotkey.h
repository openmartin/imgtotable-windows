#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <functional>

#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#endif

class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter {
public:
    explicit GlobalHotkey(std::function<void()> callback, QObject* parent = nullptr);
    ~GlobalHotkey() override;
    bool setShortcut(const QKeySequence& shortcut, QString& error);
    QKeySequence shortcut() const { return shortcut_; }
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
private:
    void unregister();
    std::function<void()> callback_;
    QKeySequence shortcut_;
#ifdef Q_OS_MACOS
    EventHotKeyRef hotkey_ = nullptr;
    EventHandlerRef handler_ = nullptr;
    static OSStatus handleEvent(EventHandlerCallRef, EventRef, void*);
#endif
};
