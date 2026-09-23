#include "GlobalHotkey.h"

#include <QApplication>
#include <QKeyCombination>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

GlobalHotkey::GlobalHotkey(std::function<void()> callback, QObject* parent)
    : QObject(parent), callback_(std::move(callback)) {
#ifdef Q_OS_WIN
    qApp->installNativeEventFilter(this);
#elif defined(Q_OS_MACOS)
    EventTypeSpec event{ kEventClassKeyboard, kEventHotKeyPressed };
    InstallApplicationEventHandler(&GlobalHotkey::handleEvent, 1, &event, this, &handler_);
#endif
}

GlobalHotkey::~GlobalHotkey() {
    unregister();
#ifdef Q_OS_WIN
    qApp->removeNativeEventFilter(this);
#elif defined(Q_OS_MACOS)
    if (handler_) RemoveEventHandler(handler_);
#endif
}

void GlobalHotkey::unregister() {
#ifdef Q_OS_WIN
    UnregisterHotKey(nullptr, 0x1A67);
#elif defined(Q_OS_MACOS)
    if (hotkey_) { UnregisterEventHotKey(hotkey_); hotkey_ = nullptr; }
#endif
}

bool GlobalHotkey::setShortcut(const QKeySequence& shortcut, QString& error) {
    error.clear();
    if (shortcut.isEmpty() || shortcut.count() != 1) { error = tr("请输入一个快捷键组合。"); return false; }
    auto combo=shortcut[0];
    int key=combo.key();
    auto mods=combo.keyboardModifiers();
    if (key < Qt::Key_0 || (key > Qt::Key_9 && key < Qt::Key_A) || key > Qt::Key_Z || mods==Qt::NoModifier) {
        error=tr("快捷键须由修饰键和一个字母或数字组成。"); return false;
    }
    const QKeySequence previous=shortcut_;
#ifdef Q_OS_WIN
    UINT nativeMods=MOD_NOREPEAT;
    if(mods & Qt::ControlModifier) nativeMods|=MOD_CONTROL;
    if(mods & Qt::ShiftModifier) nativeMods|=MOD_SHIFT;
    if(mods & Qt::AltModifier) nativeMods|=MOD_ALT;
    if(mods & Qt::MetaModifier) nativeMods|=MOD_WIN;
    UINT nativeKey=static_cast<UINT>(key);
    unregister();
    if(!RegisterHotKey(nullptr,0x1A67,nativeMods,nativeKey)) {
        error=tr("快捷键已被系统或其他应用占用。");
        if(!previous.isEmpty()) { shortcut_={}; QString ignored; setShortcut(previous,ignored); }
        return false;
    }
#elif defined(Q_OS_MACOS)
    static constexpr int digitCodes[]={29,18,19,20,21,23,22,26,28,25};
    static constexpr int letterCodes[]={0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    UInt32 nativeKey=key<=Qt::Key_9 ? digitCodes[key-Qt::Key_0] : letterCodes[key-Qt::Key_A];
    UInt32 nativeMods=0;
    if(mods & Qt::ControlModifier) nativeMods|=controlKey;
    if(mods & Qt::ShiftModifier) nativeMods|=shiftKey;
    if(mods & Qt::AltModifier) nativeMods|=optionKey;
    if(mods & Qt::MetaModifier) nativeMods|=cmdKey;
    unregister();
    EventHotKeyID id{'IMGT',1};
    if(RegisterEventHotKey(nativeKey,nativeMods,id,GetApplicationEventTarget(),0,&hotkey_)!=noErr) {
        error=tr("快捷键已被系统或其他应用占用。");
        if(!previous.isEmpty()) { shortcut_={}; QString ignored; setShortcut(previous,ignored); }
        return false;
    }
#endif
    shortcut_=shortcut;
    return true;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if ((type=="windows_generic_MSG" || type=="windows_dispatcher_MSG") &&
        static_cast<MSG*>(message)->message==WM_HOTKEY &&
        static_cast<MSG*>(message)->wParam==0x1A67) { callback_(); if(result) *result=0; return true; }
#else
    (void)type; (void)message; (void)result;
#endif
    return false;
}

#ifdef Q_OS_MACOS
OSStatus GlobalHotkey::handleEvent(EventHandlerCallRef, EventRef event, void* context) {
    EventHotKeyID id{};
    if(GetEventParameter(event,kEventParamDirectObject,typeEventHotKeyID,nullptr,sizeof(id),nullptr,&id)==noErr &&
       id.signature=='IMGT' && id.id==1) {
        static_cast<GlobalHotkey*>(context)->callback_(); return noErr;
    }
    return eventNotHandledErr;
}
#endif
