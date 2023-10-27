/*
 * Copyright (c) 2018 Sylvain "Skarsnik" Colinet.
 *
 * This file is part of the QUsb2Snes project.
 * (see https://github.com/Skarsnik/QUsb2snes).
 *
 * QUsb2Snes is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * QUsb2Snes is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QUsb2Snes.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <QDebug>

#ifndef QUSB2SNES_NOGUI
#include <QApplication>
#include <QMessageBox>
#include "ui/appui.h"
#else
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#endif

#include <QTimer>
#include <QThread>
#include <QFile>
#include <QSettings>
#include <QObject>
#include <QHostInfo>
#include <QVersionNumber>
#include <QStandardPaths>
#include <QDir>
#include <ostream>

#ifdef Q_OS_MACOS
#include "osx/appnap.h"
#endif

#include "qskarsnikringlist.hpp"
#include "wsserver.h"
#include "devices/sd2snesfactory.h"
#include "devices/luabridge.h"
#include "devices/retroarchfactory.h"
#include "devices/snesclassicfactory.h"
#include "devices/emunetworkaccessfactory.h"

std::ostream *stdLogStream = nullptr;

WSServer wsServer;
QSettings *globalSettings;

/*
#include "backward.hpp"
namespace backward {

backward::SignalHandling sh;

} // namespace backward*/

static QTextStream logfile;
static QTextStream debugLogFile;
// static QTextStream lowlogfile;

// QUSB2SNES_DEVEL is used for special stuff needed when working on qusb code
// For example it enable outputing all the logs entry on cout also so you see them in
// your IDE

#if !defined(GIT_TAG_VERSION)
#define QUSB2SNES_DEVEL
#endif

#ifdef QUSB2SNES_DEVEL

static QTextStream cout(stdout);

#else

#endif

static QSkarsnikRingList<QString> logDebugCrash(500);

static void onCrash()
{
    QFile crashLog(qApp->applicationDirPath() + "/crash-log.txt");
    if (!crashLog.open(QIODevice::WriteOnly | QIODevice::Text))
        exit(1);
    crashLog.write(QString("Runing QUsb2Snes version " + qApp->applicationVersion() + "\n").toUtf8());
    crashLog.write(QString("Compiled against Qt" + QString(QT_VERSION_STR) + ", running" + qVersion() + "\n").toUtf8());
    for (unsigned int i = 0; i < logDebugCrash.size(); i++)
    {
        crashLog.write(logDebugCrash.at(i).toUtf8() + "\n");
    }
    crashLog.flush();
    crashLog.close();
    exit(1);
}

static void seg_handler(int sig)
{
    Q_UNUSED(sig);
    onCrash();
}

// Set this to true if you expect lot of flood message
// It's used for example on the snes classic device that check stuff on a timer
// and only the relevant information is logged but not the whole snes classic message exchange
bool dontLogNext = false;

const unsigned int MAX_LINE_LOG = 2000;
QFile *refToLogFile;

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static QString logBuffer;
    static quint32 logCounter = 0;
    QByteArray localMsg = msg.toLocal8Bit();
    QTextStream *log = &logfile;
    // QTextStream* log = new QTextStream();
    // cout << msg;
//    if (dontLogNext)
//        return ;
#ifdef QT_NO_DEBUG
    QString logString = QString("%6 %5 - %7: %1").arg(localMsg.constData()).arg(context.category, 20).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
#else
    QString logString = QString("%6 %5 - %7: %1 \t(%2:%3, %4)").arg(localMsg.constData()).arg(context.file).arg(context.line).arg(context.function).arg(context.category, 20).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
#endif
    switch (type)
    {
    case QtDebugMsg:
        logDebugCrash.append(logString.arg("Debug"));
        break;
    case QtCriticalMsg:
        logDebugCrash.append(logString.arg("Critical"));
        *log << logString.arg("Critical");
        logBuffer.append(logString.arg("Critical"));
        break;
    case QtWarningMsg:
        logDebugCrash.append(logString.arg("Warning"));
        *log << logString.arg("Warning");
        logBuffer.append(logString.arg("Warning"));
        break;
    case QtFatalMsg:
        logDebugCrash.append(logString.arg("Fatal"));
        *log << logString.arg("Fatal");
        *log << "\n";
        log->flush();
#ifndef QUSB2SNES_NOGUI
        QMessageBox::critical(nullptr, QObject::tr("Critical error"), msg);
#else
        fprintf(stderr, "Critical error :%s\n", msg.toLatin1().constData());
#endif
        qApp->exit(1);
        break;
    case QtInfoMsg:
        logDebugCrash.append(logString.arg("Info"));
        *log << logString.arg("Info");
        logBuffer.append(logString.arg("Info"));
        break;
    }
    if (debugLogFile.device() != nullptr)
    {
        debugLogFile << logString.arg("Debug");
        debugLogFile << "\n";
        debugLogFile.flush();
    }
    if (type != QtDebugMsg)
    {
        *log << "\n";
        logBuffer.append("\n");
        if (logCounter == MAX_LINE_LOG)
        {
            logCounter = 0;
            refToLogFile->resize(0);
            *log << logBuffer;
            logBuffer.clear();
        }
        logCounter++;
        log->flush();
    }
#ifdef QUSB2SNES_DEVEL
    cout << QString("%3 - %1 : %2").arg(context.category, 20).arg(msg).arg(QDateTime::currentDateTime().toString(Qt::ISODate)) << "\n";
    cout.flush();
#endif
}

void startServer()
{
    quint16 port = 8080;
    QHostAddress addr(QHostAddress::Any);
    if (globalSettings->contains("listen"))
        addr = QHostInfo::fromName(globalSettings->value("listen").toString()).addresses().first();
    if (globalSettings->contains("port"))
        port = globalSettings->value("port").toUInt();
    QString status = wsServer.start(addr, port);
    if (!status.isEmpty())
    {
        fprintf(stderr, "Can't listen on localhost:8080 or the host set in the config file: %s\n", status.toLatin1().data());
        qApp->exit(1);
    }
}

#include <signal.h>

int main(int ac, char *ag[])
{
    signal(SIGSEGV, seg_handler);
    std::set_terminate(onCrash);
#ifndef QUSB2SNES_NOGUI
    QApplication app(ac, ag);
#else
    QCoreApplication app(ac, ag);
#endif
#ifdef Q_OS_WIN
    QFile mlog(qApp->applicationDirPath() + "/log.txt");
    QFile mDebugLog(qApp->applicationDirPath() + "/log-debug.txt");
    // QString crashFileQPath = qApp->applicationDirPath() + "/crash-log.txt";
    // QByteArray bacf = crashFileQPath.toLocal8Bit();
    // char* crashFilePath = bacf.data();

#else
    const QString appDataPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    if (!QFile::exists(appDataPath))
        QDir().mkdir(appDataPath);
    QFile mlog(appDataPath + "/log.txt");
    QFile mDebugLog(appDataPath + "/log-debug.txt");
#endif

    app.setApplicationName("QUsb2Snes");
    // This is only defined in the PRO file
#ifdef GIT_TAG_VERSION
    QString plop(GIT_TAG_VERSION);
    plop.remove(0, 1); // Remove the v
    app.setApplicationVersion(QVersionNumber::fromString(plop).toString());
#else
    app.setApplicationVersion("0.8");
#endif

#ifdef Q_OS_MACOS
    auto appNap = new AppNapSuspender();
    appNap->suspend();
#endif
    qInfo() << "Runing QUsb2Snes version " << qApp->applicationVersion();
    qInfo() << "Compiled against Qt" << QT_VERSION_STR << ", running" << qVersion();
    // let set some know trusted domain
    wsServer.addTrusted("http://localhost");
    wsServer.addTrusted("http://127.0.0.1");
    wsServer.addTrusted("http://www.multitroid.com");
    wsServer.addTrusted("http://multitroid.com");
    wsServer.addTrusted("https://multiworld.samus.link/");
    wsServer.addTrusted("http://usb2snes.com");
    wsServer.addTrusted("https://samus.link");
    wsServer.addTrusted("https://funtoon.party");
    QLoggingCategory::setFilterRules("EmuNWAccessClient.debug=true\n");

#ifndef QUSB2SNES_NOGUI
    AppUi *appUi = new AppUi();
    int updatedIndex = app.arguments().indexOf("-updated");
    if (updatedIndex != -1)
        appUi->updated(app.arguments().at(updatedIndex + 1));
    appUi->sysTray->show();
#else
    QCommandLineParser parser;
    parser.setApplicationDescription("QUsb2Snes");
    parser.addHelpOption();
    // parser.addVersionOption();

    QCommandLineOption configFile(QStringList() << "c"
                                                << "config",
                                  QCoreApplication::translate("main", "Config file (Default: 'QUsb2Snes')"),
                                  "config",
                                  "QUsb2Snes");
    parser.addOption(configFile);

    QCommandLineOption debugEnable(QStringList() << "debug",
                                   QCoreApplication::translate("main", "Activate debug log."));
    parser.addOption(debugEnable);

    QCommandLineOption showVersionInfo(QStringList() << "v"
                                                     << "version",
                                       QCoreApplication::translate("main", "Print version info and exit"));
    parser.addOption(showVersionInfo);

    QCommandLineOption alternatePort(QStringList() << "p"
                                                   << "port",
                                     QCoreApplication::translate("main", "Alternate port to use (Default: 8080)"),
                                     "port");
    parser.addOption(alternatePort);

    QCommandLineOption retroArchHostOption(QStringList() << "retroarchhost",
                                           QCoreApplication::translate("main", "Retroarch Host (Default: 127.0.0.1:55355)"),
                                           "retroarchhost");
    parser.addOption(retroArchHostOption);

    QCommandLineOption sd2SnesEnable(QStringList() << "sd2snes",
                                     QCoreApplication::translate("main", "Activate sd2snes support."));
    parser.addOption(sd2SnesEnable);

    QCommandLineOption luaBridgeEnable(QStringList() << "luabridge",
                                       QCoreApplication::translate("main", "Activate lua bridge support."));
    parser.addOption(luaBridgeEnable);

    QCommandLineOption retroArchEnable(QStringList() << "retroarch",
                                       QCoreApplication::translate("main", "Activate retroarch support."));
    parser.addOption(retroArchEnable);

    QCommandLineOption snesClassicEnable(QStringList() << "snesclassic",
                                         QCoreApplication::translate("main", "Activate snesclassic support."));
    parser.addOption(snesClassicEnable);

    parser.process(app);

    // std::filebuf crashFile;
#ifndef Q_OS_WIN
    globalSettings = new QSettings("skarsnik.nyo.fr", parser.value(configFile));
#else
    globalSettings = new QSettings("config.ini", QSettings::IniFormat);
#endif
    if (parser.isSet(debugEnable) || (globalSettings->contains("debugLog") && globalSettings->value("debugLog").toBool()))
    {
        mDebugLog.open(QIODevice::WriteOnly | QIODevice::Text);
        debugLogFile.setDevice(&mDebugLog);

        /*crashFile.open(crashFilePath, std::ios::out);
        stdLogStream = new std::ostream(&crashFile);*/
    }
    if (mlog.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        logfile.setDevice(&mlog);
        refToLogFile = &mlog;
        qInstallMessageHandler(myMessageOutput);
    }

    // qDebug() << parser.values(showVersionInfo);
    // qDebug() << parser.value(alternatePort);

    // if (app.arguments().contains("--version"))
    if (parser.isSet(showVersionInfo))
    {
        fprintf(stdout, "QUsb2Snes version : %s\n", app.applicationVersion().toLocal8Bit().constData());
        return 0;
    }
    if (parser.isSet(retroArchHostOption))
    {
        globalSettings->setValue("RetroArchHost", parser.value(retroArchHostOption));
    }
    if (parser.isSet(alternatePort))
    {
        // qDebug() << parser.value(alternatePort);
        globalSettings->setValue("port", parser.value(alternatePort));
    }

    if (globalSettings->value("retroarchdevice").toBool() || parser.isSet(retroArchEnable))
    {
        RetroArchFactory *retroarchFactory = new RetroArchFactory();
        wsServer.addDeviceFactory(retroarchFactory);
    }

    if (globalSettings->value("sd2snessupport").toBool() || parser.isSet(sd2SnesEnable))
    {
        SD2SnesFactory *sd2snesFactory = new SD2SnesFactory();
        wsServer.addDeviceFactory(sd2snesFactory);
    }
    if (globalSettings->value("luabridge").toBool() || parser.isSet(luaBridgeEnable))
    {
        LuaBridge *luaBridge = new LuaBridge();
        wsServer.addDeviceFactory(luaBridge);
    }
    if (globalSettings->value("snesclassic").toBool() || parser.isSet(snesClassicEnable))
    {
        SNESClassicFactory *snesClassic = new SNESClassicFactory();
        wsServer.addDeviceFactory(snesClassic);
    }
    if (globalSettings->value("emunwaccess").toBool() || app.arguments().contains("-emunwaccess"))
    {
        EmuNetworkAccessFactory *emunwf = new EmuNetworkAccessFactory();
        wsServer.addDeviceFactory(emunwf);
    }
    QObject::connect(&wsServer, &WSServer::listenFailed, [=](const QString &err) {});
    QTimer::singleShot(100, &startServer);
#endif
    return app.exec();
}
