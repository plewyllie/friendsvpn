#include "proxyserver.h"
#include <QCryptographicHash>
#include "bonjour/bonjourrecord.h"
#include "unixsignalhandler.h"
#include <arpa/inet.h>

QString ProxyServer::computeMd5(const QString &friendUid, const QString &name, const QString &regType,
                                 const QString &domain, const QString &hostname, quint16 port) {
    QString allParams = friendUid + name + regType + hostname + QString::number(port);

    QByteArray hash = QCryptographicHash::hash(allParams.toUtf8().data(), QCryptographicHash::Md5);
    //return QString(hash);
    return allParams; // TODO change
}

ProxyServer::ProxyServer(const QString &friendUid, const QString &name, const QString &regType, const QString &domain,
                       const QString &hostname, quint16 port) : Proxy(port, regType,
                                                                      computeMd5(friendUid, name, regType, domain, hostname, port))
{
    qDebug() << "Proxy constructor!";

    QString newip = listenIp;
    // create bonjour rec with new IP
    QList<QString> ip;
    ip.append(newip);
    rec = BonjourRecord(name, regType, domain, hostname, ip, port);

    // get DataPlaneConnection associated with friendUid
    ConnectionInitiator* initiator = ConnectionInitiator::getInstance();
    con = initiator->getDpConnection(friendUid);
}

void ProxyServer::run() {
    run_pcap();

    // advertise by registering the record with a bonjour registrar
    // TODO
}

void ProxyServer::sendBytes(const char *buf, int len, QString dstIp) {
    qDebug() << "I am a server sending bytes!";
    // is this a new client ?
    if (!clients.contains(dstIp)) {
        // make a new raw sock for dstIp
        clients.insert(dstIp, this->initRaw(dstIp, port));
    }
    QProcess* sockRaw = clients.value(dstIp);
    qDebug() << "server writing buf" << buf;

    // the srcPort is changed in the helper :)
    sockRaw->write(buf, len);
    /*
    // exit (TODO remove)
    UnixSignalHandler* u = UnixSignalHandler::getInstance();
    u->termSignalHandler(3);
    */
}

void ProxyServer::receiveBytes(const char* buf, int len, int sockType, QString& srcIp) {
    qDebug() << "server sending " << len << "bytes!";
    qDebug() << buf;
    con->sendBytes(buf, len, idHash, sockType, srcIp);
}
