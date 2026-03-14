#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include <QDir>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Set application metadata
    app.setApplicationName("ModbusHub");
    app.setApplicationVersion("2.0.0-Qt");
    app.setOrganizationName("Modbus Tools");
    
    // Use Fusion style as base for consistent cross-platform look
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Load macOS-inspired stylesheet
    QStringList stylesheetPaths = {
        QDir::currentPath() + "/src/ui/resources/macos_style.qss",
        QDir::currentPath() + "/macos_style.qss",
        QCoreApplication::applicationDirPath() + "/macos_style.qss",
        ":/styles/macos_style.qss"
    };
    
    bool styleLoaded = false;
    for (const QString &path : stylesheetPaths) {
        QFile styleFile(path);
        if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
            QString stylesheet = QString::fromUtf8(styleFile.readAll());
            app.setStyleSheet(stylesheet);
            styleFile.close();
            styleLoaded = true;
            break;
        }
    }
    
    // Fallback: Use embedded minimal macOS-style if file not found
    if (!styleLoaded) {
        app.setStyleSheet(R"(
            QWidget {
                font-family: "Segoe UI", "SF Pro Display", "Helvetica Neue", Arial, sans-serif;
                font-size: 13px;
                color: #1d1d1f;
            }
            QMainWindow {
                background-color: #f5f5f7;
            }
            QTabWidget::pane {
                border: 1px solid #d2d2d7;
                border-radius: 10px;
                background-color: #ffffff;
                padding: 10px;
            }
            QTabBar::tab {
                background-color: transparent;
                padding: 8px 20px;
                margin: 2px 4px;
                border-radius: 6px;
                font-weight: 500;
                color: #6e6e73;
            }
            QTabBar::tab:selected {
                background-color: #ffffff;
                color: #1d1d1f;
                border: 1px solid #d2d2d7;
            }
            QGroupBox {
                font-weight: 600;
                border: 1px solid #e5e5e7;
                border-radius: 12px;
                margin-top: 16px;
                padding-top: 16px;
                background-color: #ffffff;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                padding: 4px 12px;
                left: 12px;
                color: #6e6e73;
                font-size: 12px;
                text-transform: uppercase;
            }
            QPushButton {
                background-color: #ffffff;
                border: 1px solid #d2d2d7;
                border-radius: 8px;
                padding: 8px 16px;
                font-weight: 500;
                min-height: 20px;
            }
            QPushButton:hover {
                background-color: #f5f5f7;
            }
            QPushButton:pressed {
                background-color: #e5e5e7;
            }
            QLineEdit, QComboBox {
                background-color: #ffffff;
                border: 1px solid #d2d2d7;
                border-radius: 8px;
                padding: 8px 12px;
            }
            QLineEdit:focus, QComboBox:focus {
                border-color: #007aff;
            }
            QTextEdit {
                background-color: #ffffff;
                border: 1px solid #d2d2d7;
                border-radius: 10px;
                padding: 12px;
            }
            QTableWidget {
                background-color: #ffffff;
                border: 1px solid #d2d2d7;
                border-radius: 10px;
                gridline-color: #e5e5e7;
            }
            QStatusBar {
                background-color: #f5f5f7;
                border-top: 1px solid #d2d2d7;
            }
        )");
    }
    
    // Create and show main window
    MainWindow mainWindow;
    mainWindow.show();
    
    return app.exec();
}
