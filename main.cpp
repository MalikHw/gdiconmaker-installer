#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextEdit>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QStyleFactory>
#include <QPalette>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProgressDialog>
#include <QScrollBar>
#include <QDateTime>

class IconInstallerWindow : public QMainWindow {
    Q_OBJECT

private:
    QString zipPath;
    QString gdPath;
    QLabel *statusLabel;
    QPushButton *selectZipBtn;
    QPushButton *detectGDBtn;
    QPushButton *installBtn;
    QPushButton *restoreBtn;
    QRadioButton *geodeRadio;
    QRadioButton *manualRadio;
    QButtonGroup *installMethodGroup;
    QGroupBox *methodBox;
    QTimer *processTimer;
    QSettings *settings;
    QTextEdit *logBox;
    bool gdDetected = false;

    QString getBackupPath() {
#ifdef Q_OS_WIN
        return "C:/gdiconmaker-bkp";
#else
        return QDir::homePath() + "/gdiconmaker-bkp";
#endif
    }

    void log(const QString &msg) {
        logBox->append("• " + msg);
        logBox->verticalScrollBar()->setValue(logBox->verticalScrollBar()->maximum());
    }

    bool validateZip(const QString &path) {
        // Use PowerShell to check ZIP contents
        QString tempDir = QDir::tempPath() + "/gdiconmaker_validate_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        
        QProcess proc;
#ifdef Q_OS_WIN
        QString psCmd = QString("powershell -Command \"Expand-Archive -Path '%1' -DestinationPath '%2' -Force\"")
            .arg(path).arg(tempDir);
        proc.start("cmd.exe", QStringList() << "/c" << psCmd);
#else
        proc.start("unzip", QStringList() << "-q" << path << "-d" << tempDir);
#endif
        
        if (!proc.waitForFinished(10000)) {
            log("❌ ZIP extraction timeout");
            QDir(tempDir).removeRecursively();
            return false;
        }
        
        if (proc.exitCode() != 0) {
            log("❌ Failed to extract ZIP for validation");
            QDir(tempDir).removeRecursively();
            return false;
        }

        bool hasPack = QFile::exists(tempDir + "/pack.json");
        bool hasPng = QFile::exists(tempDir + "/pack.png");
        bool hasIcons = QDir(tempDir + "/icons").exists();

        QDir(tempDir).removeRecursively();

        if (!hasPack || !hasPng || !hasIcons) {
            log("❌ Invalid pack structure (missing pack.json/pack.png/icons)");
            return false;
        }

        log("✓ ZIP validated successfully");
        return true;
    }

    void checkForGD() {
        QProcess proc;
#ifdef Q_OS_WIN
        proc.start("tasklist", QStringList() << "/FI" << "IMAGENAME eq GeometryDash.exe" << "/FO" << "CSV" << "/NH");
#else
        proc.start("pgrep", QStringList() << "-x" << "GeometryDash.exe");
#endif
        proc.waitForFinished();
        QString output = proc.readAllStandardOutput();

        if (output.contains("GeometryDash.exe") || !output.isEmpty()) {
            processTimer->stop();
            determineGDPath();
        }
    }

    void determineGDPath() {
        QProcess proc;
#ifdef Q_OS_WIN
        proc.start("wmic", QStringList() << "process" << "where" << "name='GeometryDash.exe'" << "get" << "ExecutablePath" << "/FORMAT:LIST");
        proc.waitForFinished();
        QString output = proc.readAllStandardOutput();
        
        for (const QString &line : output.split('\n')) {
            if (line.startsWith("ExecutablePath=")) {
                QString exePath = line.mid(15).trimmed();
                if (!exePath.isEmpty()) {
                    QFileInfo info(exePath);
                    gdPath = info.absolutePath();
                    break;
                }
            }
        }
#elif defined(Q_OS_LINUX)
        proc.start("bash", QStringList() << "-c" << "ps aux | grep GeometryDash.exe | grep -v grep | awk '{print $11}'");
        proc.waitForFinished();
        QString exePath = proc.readAllStandardOutput().trimmed();
        if (!exePath.isEmpty()) {
            QFileInfo info(exePath);
            gdPath = info.absolutePath();
        }
#elif defined(Q_OS_MAC)
        gdPath = "/Applications/Geometry Dash.app/Contents/Resources";
#endif

        if (gdPath.isEmpty()) {
            log("❌ Could not determine GD path");
            QMessageBox::critical(this, "Error", "Failed to detect Geometry Dash location.\nMake sure GD is running!");
            return;
        }

        settings->setValue("gdPath", gdPath);
        log("✓ GD detected at: " + gdPath);
        statusLabel->setText("✓ GD Path: " + gdPath);
        gdDetected = true;
        detectGDBtn->setEnabled(false);
        checkInstallMethods();
        updateUI();
    }

    void checkInstallMethods() {
        QString geodePath = gdPath + "/geode/mods/geode.texture-loader.geode";
        bool geodeAvail = QFile::exists(geodePath);

        geodeRadio->setEnabled(geodeAvail);
        if (geodeAvail) {
            log("✓ Geode Texture Loader detected");
            geodeRadio->setChecked(true);
        } else {
            log("⚠ Geode Texture Loader not found, manual install only");
            manualRadio->setChecked(true);
        }
    }

    void performGeodeInstall() {
        QString packDir = gdPath + "/geode/config/geode.texture-loader/packs/";
        QDir dir;
        if (!dir.exists(packDir)) {
            dir.mkpath(packDir);
        }

        QFileInfo zipInfo(zipPath);
        QString destPath = packDir + zipInfo.fileName();

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }

        if (QFile::copy(zipPath, destPath)) {
            log("✓ Pack copied to Geode folder");
            QMessageBox::information(this, "Success!", 
                "✓ Icon pack installed via Geode!\n\n"
                "Open GD → Settings → Graphics → Textures\n"
                "Apply your texture pack!");
        } else {
            log("❌ Failed to copy pack");
            QMessageBox::critical(this, "Error", "Failed to copy pack to Geode folder");
        }
    }

    void performManualInstall() {
        QString backupPath = getBackupPath();
        QString resourcesPath = gdPath + "/resources";
        QString iconsPath = resourcesPath + "/icons";

        // Extract ZIP to temp using PowerShell
        QString tempDir = QDir::tempPath() + "/gdiconmaker_temp_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(tempDir);

        log("Extracting pack...");
        
        QProcess proc;
#ifdef Q_OS_WIN
        QString psCmd = QString("powershell -Command \"Expand-Archive -Path '%1' -DestinationPath '%2' -Force\"")
            .arg(zipPath).arg(tempDir);
        proc.start("cmd.exe", QStringList() << "/c" << psCmd);
#else
        proc.start("unzip", QStringList() << "-q" << zipPath << "-d" << tempDir);
#endif

        if (!proc.waitForFinished(30000)) {
            log("❌ Extraction timeout");
            QMessageBox::critical(this, "Error", "ZIP extraction timed out");
            QDir(tempDir).removeRecursively();
            return;
        }

        if (proc.exitCode() != 0) {
            log("❌ Failed to extract ZIP");
            QMessageBox::critical(this, "Error", "Failed to extract ZIP file");
            QDir(tempDir).removeRecursively();
            return;
        }

        // Get icon files from extracted pack
        QString extractedIcons = tempDir + "/icons";
        QDir iconsDir(extractedIcons);
        QStringList iconFiles = iconsDir.entryList(QStringList() << "*.png" << "*.plist", QDir::Files);

        if (iconFiles.isEmpty()) {
            log("❌ No icon files found in pack");
            QMessageBox::critical(this, "Error", "No icon files found in pack");
            QDir(tempDir).removeRecursively();
            return;
        }

        // Backup original files if not already backed up
        if (!QDir(backupPath).exists()) {
            log("Creating backup of original icons...");
            QDir().mkpath(backupPath);

            for (const QString &file : iconFiles) {
                QString originalFile = iconsPath + "/" + file;
                if (QFile::exists(originalFile)) {
                    QString backupFile = backupPath + "/" + file;
                    QFile::copy(originalFile, backupFile);
                }
            }
            log("✓ Backup created at: " + backupPath);
        } else {
            log("Backup already exists, skipping...");
        }

        // Copy new icons
        log("Installing icon files...");
        int installed = 0;
        for (const QString &file : iconFiles) {
            QString srcFile = extractedIcons + "/" + file;
            QString dstFile = iconsPath + "/" + file;

            if (QFile::exists(dstFile)) {
                QFile::remove(dstFile);
            }

            if (QFile::copy(srcFile, dstFile)) {
                installed++;
            }
        }

        // Cleanup temp
        QDir(tempDir).removeRecursively();

        log(QString("✓ Installed %1 icon files").arg(installed));
        QMessageBox::information(this, "Success!", 
            QString("✓ Icon pack installed manually!\n\n"
                    "%1 files installed\n"
                    "Backup saved to: %2\n\n"
                    "Launch GD to see your new icons!").arg(installed).arg(backupPath));
    }

    void restoreBackup() {
        QString backupPath = getBackupPath();
        if (!QDir(backupPath).exists()) {
            QMessageBox::warning(this, "No Backup", "No backup found to restore!");
            return;
        }

        if (!gdDetected) {
            QMessageBox::warning(this, "GD Not Detected", "Please detect Geometry Dash location first!");
            return;
        }

        auto reply = QMessageBox::question(this, "Restore Backup",
            "This will restore your original GD icons.\nContinue?",
            QMessageBox::Yes | QMessageBox::No);

        if (reply != QMessageBox::Yes) return;

        QString iconsPath = gdPath + "/resources/icons";
        QDir backupDir(backupPath);
        QStringList backupFiles = backupDir.entryList(QStringList() << "*.png" << "*.plist", QDir::Files);

        log("Restoring backup...");
        int restored = 0;
        for (const QString &file : backupFiles) {
            QString srcFile = backupPath + "/" + file;
            QString dstFile = iconsPath + "/" + file;

            if (QFile::exists(dstFile)) {
                QFile::remove(dstFile);
            }

            if (QFile::copy(srcFile, dstFile)) {
                restored++;
            }
        }

        log(QString("✓ Restored %1 files from backup").arg(restored));
        QMessageBox::information(this, "Restored", 
            QString("✓ Original icons restored!\n%1 files restored").arg(restored));
    }

    void updateUI() {
        bool canInstall = !zipPath.isEmpty() && gdDetected;
        installBtn->setEnabled(canInstall);
        methodBox->setEnabled(canInstall);
        restoreBtn->setEnabled(gdDetected);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls()) {
            QList<QUrl> urls = event->mimeData()->urls();
            if (!urls.isEmpty() && urls.first().toLocalFile().endsWith(".zip")) {
                event->acceptProposedAction();
            }
        }
    }

    void dropEvent(QDropEvent *event) override {
        QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString filePath = urls.first().toLocalFile();
            if (filePath.endsWith(".zip")) {
                zipPath = filePath;
                if (validateZip(zipPath)) {
                    statusLabel->setText("✓ ZIP: " + QFileInfo(zipPath).fileName());
                    selectZipBtn->setText("Change ZIP");
                    updateUI();
                }
            }
        }
    }

public:
    IconInstallerWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        settings = new QSettings("GDIconMaker", "Installer", this);
        setWindowTitle("GD Icon Installer");
        setFixedSize(600, 650);
        setAcceptDrops(true);

        // Main widget and layout
        QWidget *central = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(central);
        mainLayout->setSpacing(15);
        mainLayout->setContentsMargins(20, 20, 20, 20);

        // Title
        QLabel *title = new QLabel("🎨 GD Icon Pack Installer");
        QFont titleFont = title->font();
        titleFont.setPointSize(16);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(title);

        // Status label
        statusLabel = new QLabel("No pack selected");
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setStyleSheet("padding: 10px; background: #2d2d2d; border-radius: 5px; color: #aaa;");
        mainLayout->addWidget(statusLabel);

        // Select ZIP button
        selectZipBtn = new QPushButton("📁 Select Icon Pack ZIP");
        selectZipBtn->setMinimumHeight(40);
        connect(selectZipBtn, &QPushButton::clicked, this, [this]() {
            QString file = QFileDialog::getOpenFileName(this, "Select Icon Pack", "", "ZIP Files (*.zip)");
            if (!file.isEmpty()) {
                zipPath = file;
                if (validateZip(zipPath)) {
                    statusLabel->setText("✓ ZIP: " + QFileInfo(zipPath).fileName());
                    selectZipBtn->setText("Change ZIP");
                    updateUI();
                }
            }
        });
        mainLayout->addWidget(selectZipBtn);

        QLabel *dragHint = new QLabel("💡 You can also drag & drop the ZIP here!");
        dragHint->setAlignment(Qt::AlignCenter);
        dragHint->setStyleSheet("color: #888; font-size: 11px;");
        mainLayout->addWidget(dragHint);

        // Detect GD button
        detectGDBtn = new QPushButton("🎮 Run GD and Click Here");
        detectGDBtn->setMinimumHeight(40);
        detectGDBtn->setStyleSheet("background: #667eea; color: white; font-weight: bold;");
        connect(detectGDBtn, &QPushButton::clicked, this, [this]() {
            // Check if already saved
            QString savedPath = settings->value("gdPath").toString();
            if (!savedPath.isEmpty() && QDir(savedPath).exists()) {
                gdPath = savedPath;
                log("✓ Using saved GD path: " + gdPath);
                statusLabel->setText("✓ GD Path: " + gdPath);
                gdDetected = true;
                detectGDBtn->setEnabled(false);
                checkInstallMethods();
                updateUI();
                return;
            }

            log("Waiting for GeometryDash.exe...");
            log("Please run Geometry Dash now!");
            statusLabel->setText("⏳ Waiting for GD to start...");
            detectGDBtn->setEnabled(false);
            
            processTimer = new QTimer(this);
            connect(processTimer, &QTimer::timeout, this, &IconInstallerWindow::checkForGD);
            processTimer->start(500);
        });
        mainLayout->addWidget(detectGDBtn);

        // Install method selection
        methodBox = new QGroupBox("Installation Method");
        methodBox->setEnabled(false);
        QVBoxLayout *methodLayout = new QVBoxLayout(methodBox);

        installMethodGroup = new QButtonGroup(this);
        
        geodeRadio = new QRadioButton("🔧 Geode (Recommended)");
        geodeRadio->setEnabled(false);
        installMethodGroup->addButton(geodeRadio);
        methodLayout->addWidget(geodeRadio);

        QLabel *geodeDesc = new QLabel("  Uses Geode Texture Loader - easy apply/remove");
        geodeDesc->setStyleSheet("color: #888; font-size: 11px;");
        methodLayout->addWidget(geodeDesc);

        manualRadio = new QRadioButton("📂 Manual (Direct Files)");
        manualRadio->setChecked(true);
        installMethodGroup->addButton(manualRadio);
        methodLayout->addWidget(manualRadio);

        QLabel *manualDesc = new QLabel("  Replaces game files directly - backup created");
        manualDesc->setStyleSheet("color: #888; font-size: 11px;");
        methodLayout->addWidget(manualDesc);

        mainLayout->addWidget(methodBox);

        // Install button
        installBtn = new QPushButton("✨ Install Icon Pack");
        installBtn->setEnabled(false);
        installBtn->setMinimumHeight(50);
        installBtn->setStyleSheet("background: #28a745; color: white; font-weight: bold; font-size: 14px;");
        connect(installBtn, &QPushButton::clicked, this, [this]() {
            if (geodeRadio->isChecked()) {
                performGeodeInstall();
            } else {
                performManualInstall();
            }
        });
        mainLayout->addWidget(installBtn);

        // Restore button
        restoreBtn = new QPushButton("🔄 Restore Original Icons");
        restoreBtn->setEnabled(false);
        restoreBtn->setMinimumHeight(35);
        restoreBtn->setStyleSheet("background: #6c757d; color: white;");
        connect(restoreBtn, &QPushButton::clicked, this, &IconInstallerWindow::restoreBackup);
        mainLayout->addWidget(restoreBtn);

        // Log box
        QLabel *logLabel = new QLabel("📋 Log:");
        logLabel->setStyleSheet("font-weight: bold;");
        mainLayout->addWidget(logLabel);

        logBox = new QTextEdit();
        logBox->setReadOnly(true);
        logBox->setMaximumHeight(150);
        logBox->setStyleSheet("background: #1a1a1a; color: #ddd; font-family: monospace; font-size: 11px;");
        mainLayout->addWidget(logBox);

        setCentralWidget(central);

        log("GD Icon Installer started");
        log("Made with ❤️ by MalikHw47");
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Set dark theme
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(102, 126, 234));
    darkPalette.setColor(QPalette::Highlight, QColor(102, 126, 234));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);

    IconInstallerWindow window;
    window.show();

    return app.exec();
}

#include "main.moc"
