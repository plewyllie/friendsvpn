#include "controlplaneserver.h"
#include "connectioninitiator.h"

ControlPlaneServer::ControlPlaneServer(QSslCertificate servCert, QSslKey myKey,
                                       QHostAddress listenAdr, int listenPort, QObject *parent) :
    QObject(parent)
{
    this->listenAdr = listenAdr;
    this->listenPort = listenPort;

    tcpSrv = new QTcpServer(this);

    cfg = QSslConfiguration();
    sslSockList = QList<SslSocket*>();

    cfg.setLocalCertificate(servCert);
    cfg.setPrivateKey(myKey);
}

ControlPlaneServer::~ControlPlaneServer()
{
    qDebug() << "Destroy control plane server";
    foreach (QSslSocket* sock, sslSockList) {
        sock->close();
        delete sock;
    }

    tcpSrv->close();
    delete tcpSrv;
}

void ControlPlaneServer::start() {
    tcpSrv->listen(listenAdr, listenPort);
    connect(tcpSrv, SIGNAL(newConnection()), this, SLOT(newIncoming()));
}

void ControlPlaneServer::newIncoming() {
    qDebug() << "New incoming control Plane !";
    QTcpSocket* socket = tcpSrv->nextPendingConnection();
    SslSocket* sslSock = new SslSocket();

    sslSock->setSslConfiguration(cfg);
    sslSock->setSocketDescriptor(socket->socketDescriptor());
    connect(sslSock, SIGNAL(encrypted()), this, SLOT(sslSockReady()));
    connect(sslSock, SIGNAL(disconnected()), this, SLOT(sslDisconnected()));
    // XXX ignore safety concerns about the self signed certificate...
    // connect(sslSock, SIGNAL(sslErrors(const QList<QSslError>&)), sslSock, SLOT(ignoreSslErrors()));
    sslSockList.append(sslSock);
    sslSock->startServerEncryption(); // XXX encrypted() never sent on linux ubuntu 12.04 & fedora.
}

void ControlPlaneServer::sslSockReady() {
    SslSocket* sslSock = qobject_cast<SslSocket*>(sender());
    connect(sslSock, SIGNAL(readyRead()), this, SLOT(sslSockReadyRead()));
    // send HELLO packet
    QString hello("Uid:" + init->getMyUid() + "\r\n");
    sslSock->write("HELLO\r\n");
    sslSock->write(hello.toLatin1().constData());
    sslSock->flush();
}

void ControlPlaneServer::sslSockError(const QList<QSslError>& errors) {
    SslSocket* sslSock = qobject_cast<SslSocket*>(sender());
    qDebug() << "ssl error";
    qDebug() << errors;
}


void ControlPlaneServer::sslSockReadyRead() {
    SslSocket* sslSock = qobject_cast<SslSocket*>(sender());
    static QMutex mutexx;
    if (!sslSock->isAssociated()) {
        mutexx.lock();
        if (sslSock->isAssociated()) // if we acquired lock, retest if was not associated
            mutexx.unlock();
    }
    if (!sslSock->isAssociated()) { // not associated with a ControlPlaneConnection
        char buf[300];
        sslSock->readLine(buf, 300);
        QString bufStr(buf);
        if (bufStr.startsWith("HELLO")) {
            sslSock->readLine(buf, 300);
            QString uidStr(buf);
            uidStr.chop(2); // drop \r\0
            //qDebug() << uidStr.remove(0, 4);
            // drop the Uid: part with the .remove and get the CPConnection* correspoding to this UID
            ControlPlaneConnection* con = init->getConnection(uidStr.remove(0, 4));
            con->addMode(Receiving, sslSock); // add server mode
            sslSock->setControlPlaneConnection(con); // associate the sslSock with it
            mutexx.unlock();
        }
    } else { // socket is associated with controlplaneconnection
        QByteArray bytesBuf = sslSock->readAll();
        sslSock->getControlPlaneConnection()->readBuffer(bytesBuf.data(), bytesBuf.length());
    }
}

void ControlPlaneServer::sslDisconnected() {
    SslSocket* sslSock = qobject_cast<SslSocket*>(sender());
    sslSockList.removeAll(sslSock);
    if (sslSock->isAssociated())
        sslSock->getControlPlaneConnection()->removeMode(Receiving);
    sslSock->deleteLater();
}

