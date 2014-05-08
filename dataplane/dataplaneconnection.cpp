#include "dataplaneconnection.h"
#include "bonjoursql.h"
#include "proxyclient.h"

DataPlaneConnection::DataPlaneConnection(QString uid, AbstractPlaneConnection *parent) :
    AbstractPlaneConnection(uid, parent), friendUid(uid)
{
}

void DataPlaneConnection::removeConnection() {
    BonjourSQL* qSql = BonjourSQL::getInstance();

    qDebug() << "Removing connection!";
    if (friendUid.toInt() < qSql->getLocalUid().toInt()) { // friend is smaller, I am server
        //client->disconnect();
        client->stop();
        client = NULL;
        curMode = Receiving;
    } else {
        //server->disconnect();
        server = NULL;
        curMode = Emitting;
    }
}

bool DataPlaneConnection::addMode(plane_mode mode, QObject* socket) {
    mutex.lock();
    if ((curMode == Both) || (curMode == mode)) {
        mutex.unlock();
        return false;
    }
    if (mode == Receiving) {
        qDebug() << "Adding receiving mode for dataplane for uid" << friendUid;
        ServerWorker* sslSocket = dynamic_cast<ServerWorker*>(socket);
        if (!sslSocket) {
            qDebug() << "NOT a server worker! returing false";
            mutex.unlock();
            return false;
        }
        server = sslSocket;
    }
    else {
        qDebug() << "Adding emitting mode for dataplane!";
        DataPlaneClient* sslSocket = dynamic_cast<DataPlaneClient*>(socket);
        if (!sslSocket) {
            mutex.unlock();
            return false;
        }
        client = sslSocket;
    }

    if (curMode == Closed)  {
        curMode = mode;
        emit connected();
    }
    else curMode = Both;

    if (curMode == Both)
        this->removeConnection();

    mutex.unlock();
    return true;
}

void DataPlaneConnection::readBuffer(const char* buf) {
    mutex.lock();
    qDebug() << "DataPlane buffer" << buf;
    QString packet(buf);

    QStringList list = packet.split("\r\n");
    QString header;
    if (list.at(0) == "DATA") {
        QString hash;
        int sockType;
        int length;
        const char* packetBuf; // packet
        header = header % "DATA\r\n";
        for (int i = 1; i < list.length() - 1 ; i++) {
            header = header % list.at(i) % "\r\n";
            if (list.at(i).isEmpty()) {
                if (!list.at(i + 1).isEmpty()) {
                    QByteArray headerBytes = header.toUtf8();
                    packetBuf = buf + headerBytes.length();
                }
            } else {
                QStringList keyValuePair = list.at(i).split(":");
                QString key = keyValuePair.at(0);
                if (key == "Hash") {
                    hash = keyValuePair.at(1);
                } else if (key == "Length") {
                    length = keyValuePair.at(1).toInt();
                } else if (key == "Trans") {
                    if (keyValuePair.at(1) == "tcp") {
                        sockType = SOCK_STREAM;
                    } else {
                        sockType = SOCK_DGRAM;
                    }
                }
            }
        }

        // get Proxy and send through it!
        //Proxy* prox = Proxy::getProxy(hash);
        //if (!prox) {
            // read tcp or udp header to get src port
            // the first 16 bits of UDP or TCP header are the src_port
            qint16* srcPort = static_cast<qint16*>(malloc(sizeof(qint16)));
            memcpy(srcPort, packetBuf, sizeof(qint16));
            *srcPort = ntohs(*srcPort);


            // just send the bloody packet
            qDebug() << "sending the bloody packet!";
            QProcess* raw = new QProcess();
            QStringList sendRawArgs;
            sendRawArgs.append(QString::number(sockType));
            sendRawArgs.append(QString::number(IPPROTO_TCP));
            sendRawArgs.append("::1");
            raw->start(QString(HELPERPATH) + "sendRaw", sendRawArgs);

            raw->write(packetBuf, length);
            qDebug() << "raw has written";
            qDebug() << raw->readAll();

            /*qDebug() << "new client proxy thread!";
            QThread* proxyThread = new QThread();
            prox = new ProxyClient(hash, sockType, *srcPort, this);
            prox->moveToThread(proxyThread);
            connect(proxyThread, SIGNAL(started()), prox, SLOT(run()));
            connect(proxyThread, SIGNAL(finished()), proxyThread, SLOT(deleteLater()));
            // TODO delete proxy client also on finished() ?
            proxyThread->start();*/

            free(srcPort);
        //}
        //prox->sendBytes(packetBuf, length);
    }

    // get client proxy and send data through it
    // TODO
    mutex.unlock();
}

void DataPlaneConnection::sendBytes(const char *buf, int len, QString& hash, int sockType) {
    mutex.lock();
    if (curMode == Closed) {
        qWarning() << "Trying to sendBytes on Closed state for uid" << friendUid;
    }

    // make the DATA header
    QString header;
    QString trans = (sockType == SOCK_DGRAM) ? "udp" : "tcp";
    header = header  % "DATA\r\n"
            % "Hash:" % hash % "\r\n"
            % "Trans:" % trans % "\r\n"
            % "Length:" % QString::number(len) % "\r\n"
            % "\r\n";
    QByteArray headerBytes = header.toUtf8();
    int headerLen = headerBytes.length();

    int packetLen = len + headerLen;
    char dataPacket[packetLen];
    char* headerC = headerBytes.data();
    strncpy(dataPacket, headerC, headerLen);
    strncpy(dataPacket + headerLen, buf, len);

    qDebug() << "datapacket!" << dataPacket;

    if (curMode == Emitting) {
        qDebug() << "Send bytes as dataplane client";
        client->sendBytes(dataPacket, packetLen);
    } else if (curMode == Receiving) {
        qDebug() << "Send bytes as dataplane server";
        server->sendBytes(dataPacket, packetLen);
    } else {
        qWarning() << "Should not happen, trying to send bytes in Both mode for uid" << friendUid;
    }
    mutex.unlock();
}
