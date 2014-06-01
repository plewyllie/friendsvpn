#include "dataplaneconnection.h"
#include "bonjoursql.h"
#include "proxyclient.h"
#include "controlplane/controlplaneconnection.h"
#include <QCryptographicHash>


DataPlaneConnection::DataPlaneConnection(QString uid, AbstractPlaneConnection *parent) :
    AbstractPlaneConnection(uid, parent), client(NULL), server(NULL), lastRcvdTimestamp(time(NULL))
{
    this->connect(this, SIGNAL(disconnected()), SLOT(disconnect()));
}

void DataPlaneConnection::removeConnection() {
    BonjourSQL* qSql = BonjourSQL::getInstance();

    if (friendUid.toULongLong() < qSql->getLocalUid().toULongLong()) { // friend is smaller, I am server
        if (client)
            client->stop();
        client = NULL;
        curMode = Receiving;
    } else {
        if (server)
            server->stop();
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
        ServerWorker* sslSocket = dynamic_cast<ServerWorker*>(socket);
        if (!sslSocket) {
            mutex.unlock();
            return false;
        }
        server = sslSocket;
    }
    else {
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

void DataPlaneConnection::readBuffer(const char* buf, int bufSize) {
    qDebug() << "readBuffer of size " << bufSize;
    if (bufSize == sizeof(struct dpHeader)) {
        qDebug() << "readBuffer only contains a dpHeader but no payload";
        return;
    }
    lastRcvdTimestamp = time(NULL); // we received a packet, update time
    struct dpHeader *header = (struct dpHeader*) buf;
    const char* packetBuf = buf + sizeof(struct dpHeader); // packet

    QByteArray hash(header->md5, 16);
    char srcIpc[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &(header->srcIp), srcIpc, INET6_ADDRSTRLEN);
    QString srcIp(srcIpc);
    // get server Proxy and send through it!
    Proxy* prox = Proxy::getProxy(hash); // try and get server (hash)
    qDebug() << "Test proxy";
    if (!prox) {
        qDebug() << "!prox";
        // compute client proxy
        // read tcp or udp header to get src port

        // the first 16 bits of UDP or TCP header are the src_port
        quint16* srcPort = static_cast<quint16*>(malloc(sizeof(quint16)));
        memcpy(srcPort, packetBuf, sizeof(quint16));
        *srcPort = ntohs(*srcPort);

        QByteArray clientHash = QCryptographicHash::hash(QString(hash + srcIp + QString::number(*srcPort)).toUtf8(), QCryptographicHash::Md5);
        prox = Proxy::getProxy(clientHash);
        if (!prox) {
            prox = new ProxyClient(clientHash, hash, srcIp, header->sockType, *srcPort, this);
            prox->run();
            clientProxys.push(dynamic_cast<ProxyClient*>(prox));
            free(srcPort);
        }
    }
    qDebug() << "prox sendBytes";
    prox->sendBytes(packetBuf, ntohs(header->len), srcIp);
    qDebug() << "prox bytes sent";
}

void DataPlaneConnection::sendBytes(const char *buf, int len, QByteArray& hash, int sockType, QString& srcIp) {
    qDebug() << "dataplane connection sendBytes";
    if (time(NULL) - lastRcvdTimestamp > TIMEOUT_DELAY) {
        // is distant host still alive ?
        // get controlplaneconnection and ask it
        ConnectionInitiator* in = ConnectionInitiator::getInstance();
        ControlPlaneConnection* c = in->getConnection(friendUid);
        c->alive(); // we continue, the controlplane will close if needed
    }

    // make the DATA header
    struct dpHeader header;
    header.sockType = sockType;
    header.len = htons(qint16(len));
    memcpy(header.md5, hash, 16); // 16 bytes
    inet_pton(AF_INET6, srcIp.toUtf8().data(), &(header.srcIp));

    char* packet = static_cast<char*>(malloc(len + sizeof(struct dpHeader)));
    memcpy(packet, &header, sizeof(struct dpHeader));
    memcpy(packet + sizeof(struct dpHeader), buf, len);

    sendPacket(packet, len + sizeof(struct dpHeader));
    free(packet);
}

void DataPlaneConnection::sendPacket(const char *buf, int len) {
    mutex.lock();
    qDebug() << "Sending" << len << "bytes";
    if (curMode == Closed) {
        qWarning() << "Trying to sendBytes on Closed state for uid" << friendUid;
    } else if (curMode == Emitting) {
        client->sendBytes(buf, len);
    } else if (curMode == Receiving) {
        server->sendBytes(buf, len);
    } else {
        qWarning() << "Should not happen, trying to send bytes in Both mode for uid" << friendUid;
    }
    qDebug() << "Sent!";
    mutex.unlock();
}

void DataPlaneConnection::disconnect() {
    mutex.lock();
    if (curMode == Both) {
        client->stop();
        server->stop();
    } else {
        if (curMode == Receiving) {
            server->stop();
        }
        if (curMode == Emitting) {
            client->stop();
        }
    }
    mutex.unlock();
    while (!clientProxys.empty()) {
        Proxy* c = clientProxys.pop();
        if (c)
            delete c;
    }

    curMode = Closed;
    ConnectionInitiator::getInstance()->removeConnection(this);
    this->deleteLater();
}
