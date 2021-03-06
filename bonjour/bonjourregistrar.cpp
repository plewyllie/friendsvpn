#include "bonjourregistrar.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <QDebug>

BonjourRegistrar::BonjourRegistrar(QObject *parent)
  : QObject(parent), dnssref(0), dnssref_pa(0), bonjourSocket(0), dnsRecord(0)
{
    connect(this, SIGNAL(error(DNSServiceErrorType)), this, SLOT(handleError(DNSServiceErrorType)));
}

BonjourRegistrar::~BonjourRegistrar()
{
    qDebug() << "Deleting bonjour registrar";
    if (dnssref) {
        DNSServiceRefDeallocate(dnssref);
        dnssref = 0;
    }
    if (dnssref_pa) {
        DNSServiceRefDeallocate(dnssref_pa);
        dnssref_pa = 0;
    }
}

void BonjourRegistrar::registerService(const BonjourRecord &record) {
#ifdef __APPLE__ /* register is not supported by AVAHI compatibility layer */
    if (dnssref || dnssref_pa) {
        qWarning("Already registered a service");
        return;
    } else if (!record.resolved) {
        qWarning("Record was not resolved, aborting");
        return;
    }
    DNSServiceErrorType err = DNSServiceCreateConnection(&dnssref_pa);
    if (err != kDNSServiceErr_NoError) { emit error(err); }

    struct in6_addr newip6 = { };
    inet_pton(AF_INET6, record.ips.at(0).toUtf8().data(), &newip6);
    err = DNSServiceRegisterRecord(dnssref_pa, &dnsRecord, kDNSServiceFlagsUnique, 0,
                                   QString(record.hostname + "." + record.replyDomain).toUtf8().data(),
                                   kDNSServiceType_AAAA,
                                   kDNSServiceClass_IN, 16, &newip6, 240, registerRecordCallback, this);
    if (err != kDNSServiceErr_NoError) { emit error(err); }
    else {
        int sockfd = DNSServiceRefSockFD(dnssref_pa);
        if (sockfd == -1) {
            emit error(kDNSServiceErr_Invalid);
        } else {
            recordSocket = new QSocketNotifier(sockfd, QSocketNotifier::Read, this);
            connect(recordSocket, SIGNAL(activated(int)), this, SLOT(recordSocketReadyRead()));
        }
    }

    QByteArray txtBytes = record.txt;
    char* txtChar = txtBytes.data();
    unsigned char txtRecord[txtBytes.length()];
    memcpy(&txtRecord, txtChar, txtBytes.length());

    err = DNSServiceRegister(&dnssref,
          0, 0, record.serviceName.toUtf8().constData(),
          record.registeredType.toUtf8().constData(),
          record.replyDomain.isEmpty() ? 0
                    : record.replyDomain.toUtf8().constData(),
          QString(record.hostname + "." + record.replyDomain).toUtf8().data(), ntohs(record.port),
                             record.txt.length(), txtRecord, bonjourRegisterService,
          this);
    if (err != kDNSServiceErr_NoError) {
        emit error(err);
    } else {
        int sockfd = DNSServiceRefSockFD(dnssref);
        if (sockfd == -1) {
            emit error(kDNSServiceErr_Invalid);
        } else {
            bonjourSocket = new QSocketNotifier(sockfd,
                                 QSocketNotifier::Read, this);
            connect(bonjourSocket, SIGNAL(activated(int)),
                  this, SLOT(bonjourSocketReadyRead()));
        }
    }
#endif
}

void BonjourRegistrar::registerRecordCallback(DNSServiceRef, DNSRecordRef, const DNSServiceFlags flags,
                                              DNSServiceErrorType errorCode, void *context) {
    BonjourRegistrar* registrar = static_cast<BonjourRegistrar*>(context);
    if (errorCode != kDNSServiceErr_NoError) {
        emit registrar->error(errorCode);
    }
    if (!(flags & kDNSServiceFlagsMoreComing)) fflush(stdout);
}

void BonjourRegistrar::handleError(DNSServiceErrorType error) {
    qDebug() << "BonjourRegistrar had error" << error;

    //  deallocate the dnssrefs
    if (dnssref) {
        DNSServiceRefDeallocate(dnssref);
        dnssref = 0;
    }
    if (dnssref_pa) {
        DNSServiceRefDeallocate(dnssref_pa);
        dnssref_pa = 0;
    }
}

void BonjourRegistrar::bonjourSocketReadyRead() {
    DNSServiceErrorType err = DNSServiceProcessResult(dnssref);
    if (err != kDNSServiceErr_NoError)
        emit error(err);
}

void BonjourRegistrar::recordSocketReadyRead() {
    DNSServiceErrorType err = DNSServiceProcessResult(dnssref_pa);
    if (err != kDNSServiceErr_NoError)
        emit error(err);
}

void BonjourRegistrar::bonjourRegisterService(
     DNSServiceRef, DNSServiceFlags,
     DNSServiceErrorType errorCode, const char *name,
     const char *regType, const char *domain, void *data) {
    BonjourRegistrar *registrar = static_cast<BonjourRegistrar *>(data);
    if (errorCode != kDNSServiceErr_NoError) {
        emit registrar->error(errorCode);
    } else {
        registrar->finalRecord =
        BonjourRecord(QString::fromUtf8(name),
                      QString::fromUtf8(regType),
                      QString::fromUtf8(domain));
        emit registrar->serviceRegistered(registrar->finalRecord);
   }
}

