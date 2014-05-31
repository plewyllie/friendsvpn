#ifndef DATAPLANECONNECTION_H
#define DATAPLANECONNECTION_H

#include <QObject>
#include "abstractplaneconnection.h"
#include "dataplaneclient.h"
#include "dataplaneserver.h"
#include "serverworker.h"
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

class ProxyClient;

/**
 * @brief The dpHeader struct is the PH2PHTP data plane custom header.
 */
struct dpHeader {
    quint8 sockType; /* underlying transport header socket type */
    quint8 empty; /* should be set to 0 */
    quint16 len; /* underlying packet length (including transport header) */
    char md5[16]; /* MD5 identifying ProxyServer */
    struct in6_addr srcIp; /* source IP of the client using the service */
} __attribute__((__packed__));

/**
 * @brief The DataPlaneConnection class
 */
class DataPlaneConnection : public AbstractPlaneConnection
{
    Q_OBJECT
private:
    //QString friendUid; // connection associated with friendUid
    DataPlaneClient* client;
    ServerWorker* server;
    QMutex mutex;

    /**
     * @brief lastRcvdTimestap contains the timestamp of the last received packet
     */
    uint lastRcvdTimestamp;

    /**
     * @brief clientProxys contains the list of pointers of proxy clients for this connection
     */
    QStack<ProxyClient*> clientProxys;

    void removeConnection();

    void sendPacket(const char* buf, int len);
public:
    explicit DataPlaneConnection(QString uid, AbstractPlaneConnection *parent = 0);

    bool addMode(plane_mode, QObject* socket);

    void sendBytes(const char* buf, int len, QByteArray& hash, int sockType, QString& srcIp);
public slots:
    void readBuffer(const char* buf, int len);

    void disconnect();

};

#endif // DATAPLANECONNECTION_H
