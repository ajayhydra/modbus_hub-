/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/mainwindow.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto MainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN10MainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MainWindow",
        "onModeChanged",
        "",
        "index",
        "onConnect",
        "onDisconnect",
        "onRead",
        "onWrite",
        "onClearData",
        "onPollToggled",
        "checked",
        "onPollTimer",
        "onFunctionChanged",
        "onFormatChanged",
        "onGatewayModeChanged",
        "onStartGateway",
        "onStopGateway",
        "onAddDevice",
        "onRemoveDevice",
        "onEditDevice",
        "onDeviceSelected",
        "row",
        "column",
        "onConnectAll",
        "onDisconnectAll",
        "onMonitorAll",
        "onStopMonitor",
        "onScanNetwork",
        "onStopScan",
        "onScanTimer",
        "onDeviceWorkerLog",
        "msg",
        "category",
        "onDeviceWorkerPollResult",
        "deviceId",
        "deviceName",
        "pollReg",
        "value",
        "onDeviceWorkerRefresh",
        "onDeviceWorkerDisconnected",
        "onConfigureAlarms",
        "onRefreshDashboard",
        "onDarkModeToggled",
        "onOpenManual",
        "onClearLog",
        "onSaveLog",
        "onLogFilterChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onModeChanged'
        QtMocHelpers::SlotData<void(int)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'onConnect'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDisconnect'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRead'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onWrite'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onClearData'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPollToggled'
        QtMocHelpers::SlotData<void(bool)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Slot 'onPollTimer'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFunctionChanged'
        QtMocHelpers::SlotData<void(int)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'onFormatChanged'
        QtMocHelpers::SlotData<void(int)>(13, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'onGatewayModeChanged'
        QtMocHelpers::SlotData<void(bool)>(14, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Slot 'onStartGateway'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStopGateway'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAddDevice'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRemoveDevice'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEditDevice'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDeviceSelected'
        QtMocHelpers::SlotData<void(int, int)>(20, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 21 }, { QMetaType::Int, 22 },
        }}),
        // Slot 'onConnectAll'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDisconnectAll'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onMonitorAll'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStopMonitor'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onScanNetwork'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStopScan'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onScanTimer'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDeviceWorkerLog'
        QtMocHelpers::SlotData<void(const QString &, int)>(30, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 31 }, { QMetaType::Int, 32 },
        }}),
        // Slot 'onDeviceWorkerPollResult'
        QtMocHelpers::SlotData<void(int, const QString &, int, int)>(33, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 34 }, { QMetaType::QString, 35 }, { QMetaType::Int, 36 }, { QMetaType::Int, 37 },
        }}),
        // Slot 'onDeviceWorkerRefresh'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDeviceWorkerDisconnected'
        QtMocHelpers::SlotData<void(int, const QString &)>(39, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 34 }, { QMetaType::QString, 35 },
        }}),
        // Slot 'onConfigureAlarms'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRefreshDashboard'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDarkModeToggled'
        QtMocHelpers::SlotData<void(bool)>(42, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Slot 'onOpenManual'
        QtMocHelpers::SlotData<void()>(43, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onClearLog'
        QtMocHelpers::SlotData<void()>(44, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSaveLog'
        QtMocHelpers::SlotData<void()>(45, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onLogFilterChanged'
        QtMocHelpers::SlotData<void(int)>(46, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MainWindow, qt_meta_tag_ZN10MainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10MainWindowE_t>.metaTypes,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onModeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->onConnect(); break;
        case 2: _t->onDisconnect(); break;
        case 3: _t->onRead(); break;
        case 4: _t->onWrite(); break;
        case 5: _t->onClearData(); break;
        case 6: _t->onPollToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 7: _t->onPollTimer(); break;
        case 8: _t->onFunctionChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->onFormatChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->onGatewayModeChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 11: _t->onStartGateway(); break;
        case 12: _t->onStopGateway(); break;
        case 13: _t->onAddDevice(); break;
        case 14: _t->onRemoveDevice(); break;
        case 15: _t->onEditDevice(); break;
        case 16: _t->onDeviceSelected((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 17: _t->onConnectAll(); break;
        case 18: _t->onDisconnectAll(); break;
        case 19: _t->onMonitorAll(); break;
        case 20: _t->onStopMonitor(); break;
        case 21: _t->onScanNetwork(); break;
        case 22: _t->onStopScan(); break;
        case 23: _t->onScanTimer(); break;
        case 24: _t->onDeviceWorkerLog((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 25: _t->onDeviceWorkerPollResult((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 26: _t->onDeviceWorkerRefresh(); break;
        case 27: _t->onDeviceWorkerDisconnected((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 28: _t->onConfigureAlarms(); break;
        case 29: _t->onRefreshDashboard(); break;
        case 30: _t->onDarkModeToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 31: _t->onOpenManual(); break;
        case 32: _t->onClearLog(); break;
        case 33: _t->onSaveLog(); break;
        case 34: _t->onLogFilterChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 35)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 35;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 35)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 35;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN13GatewayThreadE_t {};
} // unnamed namespace

template <> constexpr inline auto GatewayThread::qt_create_metaobjectdata<qt_meta_tag_ZN13GatewayThreadE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "GatewayThread",
        "logMessage",
        "",
        "message"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'logMessage'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<GatewayThread, qt_meta_tag_ZN13GatewayThreadE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject GatewayThread::staticMetaObject = { {
    QMetaObject::SuperData::link<QThread::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13GatewayThreadE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13GatewayThreadE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13GatewayThreadE_t>.metaTypes,
    nullptr
} };

void GatewayThread::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GatewayThread *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->logMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (GatewayThread::*)(const QString & )>(_a, &GatewayThread::logMessage, 0))
            return;
    }
}

const QMetaObject *GatewayThread::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *GatewayThread::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13GatewayThreadE_t>.strings))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int GatewayThread::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void GatewayThread::logMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN18DeviceWorkerThreadE_t {};
} // unnamed namespace

template <> constexpr inline auto DeviceWorkerThread::qt_create_metaobjectdata<qt_meta_tag_ZN18DeviceWorkerThreadE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DeviceWorkerThread",
        "deviceLog",
        "",
        "msg",
        "category",
        "pollResult",
        "deviceId",
        "deviceName",
        "pollReg",
        "value",
        "deviceDisconnected",
        "requestRefresh"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'deviceLog'
        QtMocHelpers::SignalData<void(const QString &, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'pollResult'
        QtMocHelpers::SignalData<void(int, const QString &, int, int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 }, { QMetaType::QString, 7 }, { QMetaType::Int, 8 }, { QMetaType::Int, 9 },
        }}),
        // Signal 'deviceDisconnected'
        QtMocHelpers::SignalData<void(int, const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 }, { QMetaType::QString, 7 },
        }}),
        // Signal 'requestRefresh'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DeviceWorkerThread, qt_meta_tag_ZN18DeviceWorkerThreadE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DeviceWorkerThread::staticMetaObject = { {
    QMetaObject::SuperData::link<QThread::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18DeviceWorkerThreadE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18DeviceWorkerThreadE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18DeviceWorkerThreadE_t>.metaTypes,
    nullptr
} };

void DeviceWorkerThread::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DeviceWorkerThread *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->deviceLog((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->pollResult((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 2: _t->deviceDisconnected((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->requestRefresh(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DeviceWorkerThread::*)(const QString & , int )>(_a, &DeviceWorkerThread::deviceLog, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DeviceWorkerThread::*)(int , const QString & , int , int )>(_a, &DeviceWorkerThread::pollResult, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DeviceWorkerThread::*)(int , const QString & )>(_a, &DeviceWorkerThread::deviceDisconnected, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DeviceWorkerThread::*)()>(_a, &DeviceWorkerThread::requestRefresh, 3))
            return;
    }
}

const QMetaObject *DeviceWorkerThread::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DeviceWorkerThread::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18DeviceWorkerThreadE_t>.strings))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int DeviceWorkerThread::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void DeviceWorkerThread::deviceLog(const QString & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void DeviceWorkerThread::pollResult(int _t1, const QString & _t2, int _t3, int _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 2
void DeviceWorkerThread::deviceDisconnected(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void DeviceWorkerThread::requestRefresh()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
