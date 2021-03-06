#include "connection.h"
#include "ssvalidator.h"
#include <QHostInfo>
#include <QHostAddress>

Connection::Connection(QObject *parent) :
    QObject(parent),
    running(false)
{
    controllerThread = new ControllerThread(this);
    connect(controllerThread, &ControllerThread::stateChanged, [&](bool run){
        running = run;
        emit stateChanged(run);
    });
    connect(controllerThread, &ControllerThread::newBytesRead, [&](const quint64 &b) {
        profile.bytesRead += b;
        emit bytesReadChanged(profile.bytesRead);
    });
    connect(controllerThread, &ControllerThread::newBytesSent, [&](const quint64 &b) {
        profile.bytesSent += b;
        emit bytesSentChanged(profile.bytesSent);
    });
    connect(controllerThread, &ControllerThread::logAvailable, this, &Connection::onNewLog);
    connect(controllerThread, &ControllerThread::failed, this, &Connection::startFailed);
}

Connection::Connection(const SQProfile &_profile, QObject *parent) :
    Connection(parent)
{
    profile = _profile;
}

Connection::Connection(QString uri, QObject *parent) :
    Connection(parent)
{
    uri.remove(0, 5);//remove the prefix "ss://" from uri
    QStringList resultList = QString(QByteArray::fromBase64(QByteArray(uri.toStdString().c_str()))).split(':');
    profile.method = resultList.takeFirst().toUpper();
    profile.serverPort = resultList.takeLast().toUShort();
    QStringList ser = resultList.join(':').split('@');//there are lots of ':' in IPv6 address
    profile.serverAddress = ser.takeLast();
    profile.password = ser.join('@');//incase there is a '@' in password
}

Connection::~Connection()
{
    stop();
}

const SQProfile& Connection::getProfile() const
{
    return profile;
}

const QString& Connection::getName() const
{
    return profile.name;
}

const QString& Connection::getLog() const
{
    return log;
}

QByteArray Connection::getURI() const
{
    QString ssurl = QString("%1:%2@%3:%4").arg(profile.method.toLower()).arg(profile.password).arg(profile.serverAddress).arg(QString::number(profile.serverPort));
    QByteArray ba = QByteArray(ssurl.toStdString().c_str()).toBase64();
    ba.prepend("ss://");
    return ba;
}

bool Connection::isValid() const
{
    if (profile.serverAddress.isEmpty() || profile.localAddress.isEmpty() || profile.timeout < 1 || !SSValidator::validateMethod(profile.method)) {
        return false;
    }
    else {
        return true;
    }
}

const bool &Connection::isRunning() const
{
    return running;
}

void Connection::latencyTest()
{
    QHostAddress serverAddr(profile.serverAddress);
    if (serverAddr.isNull()) {
        //TODO use a non-blocking function
        QList<QHostAddress> results = QHostInfo::fromName(profile.serverAddress).addresses();
        if (!results.isEmpty()) {
            serverAddr = results.first();
        }
    }
    QSS::AddressTester *addrTester = new QSS::AddressTester(serverAddr, profile.serverPort, this);
    connect(addrTester, &QSS::AddressTester::lagTestFinished, this, &Connection::onLagTestFinished);
    connect(addrTester, &QSS::AddressTester::lagTestFinished, addrTester, &QSS::AddressTester::deleteLater);
    addrTester->startLagTest();
}

void Connection::start()
{
    profile.lastTime = QDateTime::currentDateTime();
    latencyTest();//perform a latency test automatically when start() is called

    QSS::Profile qssprofile;
    qssprofile.server = profile.serverAddress;
    qssprofile.server_port = profile.serverPort;
    qssprofile.local_address = profile.localAddress;
    qssprofile.local_port = profile.localPort;
    qssprofile.method = profile.method;
    qssprofile.password = profile.password;
    qssprofile.timeout = profile.timeout;

    controllerThread->setup(profile.debug, qssprofile);
    controllerThread->start();
}

void Connection::stop()
{
    controllerThread->quit();
    controllerThread->wait();
    if (running != false) {
        running = false;
        emit stateChanged(running);
    }
}

void Connection::onNewLog(const QString &str)
{
    if(!log.endsWith('\n') && !log.isEmpty()) {
        log.append('\n');
    }
    log.append(str);
    emit newLogAvailable(str);
}

void Connection::onLagTestFinished(int lag)
{
    profile.lag = lag;
    emit pingFinished(lag);
}
