#include "proxy.h"
#include "unixsignalhandler.h"
#include "config.h"
#include <QCryptographicHash>
#include <string.h>
#include <QtConcurrent>
#include <pcap.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

QHash<QByteArray, Proxy*> Proxy::proxyHashes;
IpResolver* Proxy::resolver = IpResolver::getInstance();
RawSockets* Proxy::rawSockets = NULL;
QString Proxy::defaultIface = IpResolver::getDefaultInterface();
QQueue<QString> Proxy::poolOfIps;
QMutex Proxy::poolOfIpsMutex;

Proxy::~Proxy() {
    proxyHashes.remove(idHash);

    if (bindSocket.state() == QProcess::Running) {
        bindSocket.write("OK\n");
        bindSocket.waitForReadyRead();
        bindSocket.waitForFinished();
    }

    while (!pcapWorkers.empty()) {
        qDebug() << "Delete pcap worker";
        delete pcapWorkers.pop();
    }

    // the children must take care of removing the listenIp if necessary
}

void Proxy::commonInit(QByteArray md5) {
    if (!rawSockets)
        rawSockets = RawSockets::getInstance();
    if (proxyHashes.contains(md5)) {
        throw 1; // already exists, we throw int "1"
    }
    idHash = md5;
    proxyHashes.insert(md5, this);
    UnixSignalHandler* u = UnixSignalHandler::getInstance();
    connect(u, SIGNAL(exiting()), this, SLOT(deleteLater()), Qt::DirectConnection);
}

Proxy::Proxy(int srcPort, int sockType, QByteArray md5)
    : listenPort(srcPort), sockType(sockType)
{
    commonInit(md5);
    if (sockType == SOCK_STREAM) ipProto = IPPROTO_TCP;
    else ipProto = IPPROTO_UDP;
}

Proxy::Proxy(int srcPort, const QString& regType, QByteArray md5) : listenPort(srcPort)
{
    commonInit(md5);
    if (regType.contains("tcp")) {
        sockType = SOCK_STREAM;
        ipProto = IPPROTO_TCP;
    } else {
        sockType = SOCK_DGRAM;
        ipProto = IPPROTO_UDP;
    }
}

QString Proxy::newIP() {
    poolOfIpsMutex.lock();
    while (poolOfIps.length() < 1) {
        qWarning() << "No ipv6 available!"; // should rarely happen!
        // it causes long delay for initiating connection if this happens
        poolOfIpsMutex.unlock();
        QtConcurrent::run(gennewIP);
        QThread::sleep(1);
        poolOfIpsMutex.lock();
    }
    QString newIp = poolOfIps.dequeue();
    int length = poolOfIps.length();
    poolOfIpsMutex.unlock();
    if (length < IP_BUFFER_LENGTH / 2) {
        QtConcurrent::run(gennewIP);
    }
    return newIp;
}

void Proxy::gennewIP() {
    poolOfIpsMutex.lock();
    int length = poolOfIps.length();
    poolOfIpsMutex.unlock();

    UnixSignalHandler* u = UnixSignalHandler::getInstance();
    while (length < IP_BUFFER_LENGTH) {
        bool newIp = true; // the new IP has not been assigned to iface yet or is not a duplicate
        struct newip newip;
        do {
            // random ULA
            newip = randomIP();
            // check it doesn't exist
            QProcess testIfconfig;

            testIfconfig.start("/sbin/ifconfig", QStringList(defaultIface));
            testIfconfig.waitForReadyRead();
            char buf[3000];
            int length;
            while ((length = testIfconfig.readLine(buf, 3000))) {
                QString curLine(buf);
                QStringList list = curLine.split(" ", QString::SkipEmptyParts);
                foreach (QString value, list) {
                    if (value.contains(newip.newipPrefix)) {
                        newIp = false;
                    }
                }
            }
            testIfconfig.close();
        } while (!newIp);

        // add it with ifconfig
        QProcess ifconfig;
        QStringList newipArgs;
        newipArgs.append(defaultIface);
        newipArgs.append(newip.newipPrefix);
        ifconfig.start(QCoreApplication::applicationDirPath() +QString(HELPERPATH) + "ifconfighelp", newipArgs);
        ifconfig.waitForStarted();
        ifconfig.waitForFinished();

        qDebug() << "Trying to create IP...";
        if (ifconfig.exitCode() == 3) {
            qDebug() << "Duplicate has been found";
        } else if (ifconfig.exitCode() == 0) {
            qDebug() << "New IP created!";
            // add to local cache!
        #ifdef __APPLE__ // XXX better way of determining local loopback interface ?
            resolver->addMapping(newip.newipStripped, "", "lo0");
        #elif __GNUC__
            resolver->addMapping(newip.newipStripped, "", "lo");
        #endif
            u->addIp(newip.newipStripped);
            poolOfIpsMutex.lock();
            poolOfIps.enqueue(newip.newipStripped);
            poolOfIpsMutex.unlock();
            length++;
        } else {
            qWarning() << "Ifconfig helper exited with exit code" << ifconfig.exitCode();
            qWarning() << QCoreApplication::applicationDirPath() +QString(HELPERPATH) + "ifconfighelp";
            qWarning() << newipArgs;
            UnixSignalHandler::termSignalHandler(0);
        }
    }
}


/**
 * if i >= 16 (returns 'a')
 */
char Proxy::intToHexChar(int i) {
    if (i < 10)
        return (char)(((int)'0')+i);
    switch (i) {
        case 10: return 'a';
        case 11: return 'b';
        case 12: return 'c';
        case 13: return 'd';
        case 14: return 'e';
        case 15: return 'f';
        default: return 'a';
    }
}

struct prefix Proxy::getPrefix() {
    QProcess ifconfig;
    ifconfig.start("/sbin/ifconfig", QStringList(defaultIface));
    ifconfig.waitForFinished();
    QString ifconfigString = ifconfig.readAll();
    QTextStream stream(&ifconfigString, QIODevice::ReadOnly);
    while (!stream.atEnd()) {
        QString curLine = stream.readLine();
        QStringList list = curLine.split(" ", QString::SkipEmptyParts);

        if (list.at(0).contains("inet6")) {
#ifdef __APPLE__
#ifdef TEST
            if (!list.at(1).startsWith("fe80") && list.at(1).startsWith("fd3b")) { // remove fd3b & maybe remove ULA.
#else
            if (!list.at(1).startsWith("fe80")) { // remove fd3b & maybe remove ULA.
#endif
                QString ip = list.at(1);
                ifconfig.close();

                struct prefix p;
                p.str = stripIp(ip, list.at(3).toInt());
                p.len = list.at(3).toInt();

                qDebug() << "P len is" << p.len;

                if (!((p.len == 128) || (p.len == 0))) {
                    return p;
                }
            }
#elif __GNUC__
#ifdef TEST
            if (!list.at(2).startsWith("fe80") && list.at(2).startsWith("fd3b")) {
#else
            if (!list.at(2).startsWith("fe80")) {
#endif
                QString ip = list.at(2);
                int slashIndex = ip.indexOf("/");
                QString prefixLength = ip.right(ip.length() - slashIndex - 1);
                ip.truncate(slashIndex);
                ifconfig.close();

                struct prefix p;
                p.str = stripIp(ip, prefixLength.toInt());
                p.len = prefixLength.toInt();
                return p;
            }
#endif
        }
    }
    ifconfig.close();
    struct prefix p; // prefix of len 0 => invalid.
    p.len = 0;
    return p;
}

QString Proxy::stripIp(QString ip, int prefix) {
    /* convert to network format to clear bytes out of prefix */
    struct in6_addr ipv6network;
    inet_pton(AF_INET6, ip.toUtf8().data(), &ipv6network);

    int left = 128 - prefix;
    for (int i = 127; i >= 0 && left > 0; i--) { // 16 bytes in ipv6network
        unsigned char mask = (~(1 << i % 8));

        // go to host order
        unsigned char hostByte = 0;
        for (int k = 0; k < 8; k++) {
            hostByte |= ((ipv6network.s6_addr[i / 8] >> k) & 1) << (7-k);
        }
        hostByte &= mask;

        // go back to network byte order
        unsigned char newVal = 0;
        for (int k = 0; k < 8; k++) {
            newVal |= ((hostByte >> k) & 1) << (7-k);
        }

        ipv6network.s6_addr[i / 8] = newVal;
        left--;
    }


    char newIp[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &ipv6network, newIp, sizeof(newIp));
    QString toReturn(newIp);

    // remove the last ":" since we just want the prefix and not the whole IP
    if (toReturn.count(":") > 4) toReturn.truncate(toReturn.length() - 1);

    return toReturn;
}

struct newip Proxy::randomIP() {
    // get prefix
    struct prefix p = getPrefix();
    if (p.len == 0) {
        qWarning() << "Prefix len cannot be 128 or 0";
        UnixSignalHandler::getInstance()->doExit();
    }

    // generate random IP
    static bool first = true;
    if (first) {
        srand(time(NULL));
        first = false;
    } else {
        srand(rand());
    }
    char* prefixBuff = p.str.toUtf8().data();
    char v6buf[40];
    strcpy(v6buf, prefixBuff);

    // we don't bother with bits but just use blocks of bytes after the "prefix"
    int nbLeft = (128 - p.len) / 4; // divide by 4 since each char represents 4 bits in hex
    int index = p.str.length();

    int column = 0;
    while (nbLeft > 0) {
        if (column == 4) {
            v6buf[index] = ':';
            index++;
            column = 0;
            continue;
        }
        v6buf[index] = intToHexChar(rand() % 16);
        index++;
        column++;
        nbLeft--;
    }
    v6buf[index] = '\0';

    struct newip newIpToRet;

    QString toRet(v6buf);
    toRet.truncate(toRet.length());

    newIpToRet.newipStripped = toRet;
    newIpToRet.newipPrefix = toRet % "/" + QString::number(p.len);

    return newIpToRet;
}

Proxy* Proxy::getProxy(QByteArray md5) {
    if (proxyHashes.contains(md5)) {
        return proxyHashes.value(md5);
    }
    return NULL;
}

void Proxy::run_pcap(const char* dstIp) {
    port = listenPort;

    bool bound = false;
    while (!bound) {
        qDebug() << "Proxy client not bound yet";
        // create socket and bind for the kernel
        QStringList bindSocketArgs;
        bindSocketArgs.append(QString::number(sockType));
        bindSocketArgs.append(QString::number(ipProto));
        bindSocketArgs.append(QString::number(port));
        bindSocketArgs.append(listenIp);
        bindSocket.start(QCoreApplication::applicationDirPath() +QString(HELPERPATH) + "newSocket", bindSocketArgs);
        bindSocket.waitForStarted();
        bindSocket.waitForReadyRead();
        QString retStr = bindSocket.readAll();
        qDebug() << "newSocket args:" << bindSocketArgs;
        if (retStr != "OK\n") {
            bindSocket.waitForFinished();
            qDebug() << "Error on bind" << bindSocket.exitCode();
            if (bindSocket.exitCode() == EADDRNOTAVAIL) {
                // loop again until IP is available but just sleep a moment
                qDebug() << "Bind ERROR: EADDRNOTAVAIL";
                QThread::sleep(2);
            } else if (bindSocket.exitCode() == EADDRINUSE) {
                if (port < 60000) {
                    port = 60001;
                } else {
                    port++;
                }
                if (port >= 65535) {
                    qWarning("Port escalation higher than 65535");
                    UnixSignalHandler::termSignalHandler(0);
                }
                qDebug() << "Could not bind on port " << listenPort << "going to use " << QString::number(port);
            } /*else if (bindSocket.exitCode() == EADDRINUSE) {
                qDebug() << "Ip" << listenIp << "and port" << port << "already bound";
                bound = true; // happens when some advertise two services on same port
            }*/
        } else {
            bound = true;
        }
    }

    QStack<QString> listenInterfaces;
    if (dstIp != 0) {
        struct ip_mac_mapping map = resolver->getMapping(dstIp);
        if (map.interface == "") {
            qDebug() << "No mapping for" << dstIp;
            return;
        }
        listenInterfaces.push(map.interface);
    } else {
        listenInterfaces = resolver->getActiveInterfaces();
    }


    while (!listenInterfaces.empty()) {
        // listen for packets with pcap, forward on the secure UDP link
        QStringList args;
        QString ifa = listenInterfaces.pop();
        args.append(ifa);
        QString transportStr;
        sockType == SOCK_DGRAM ? transportStr = "udp" : transportStr = "tcp";
        args.append("ip6 dst host " + listenIp + " and " + transportStr + " and dst port " + QString::number(port));

        QThread* pcapWorkerThread = new QThread(0);
        PcapWorker* pcapWorker = new PcapWorker(args, this, pcapWorkerThread);
        pcapWorker->moveToThread(pcapWorkerThread);
        connect(pcapWorkerThread, SIGNAL(started()), pcapWorker, SLOT(run()));
        //connect(pcapWorkerThread, SIGNAL(finished()), pcapWorkerThread, SLOT(deleteLater()));
        pcapWorkers.push(pcapWorker);
        pcapWorkerThread->start();
    }
}


