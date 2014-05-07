#ifndef SERVERWORKER_H
#define SERVERWORKER_H

#include <QObject>
#include "dataplaneconfig.h"

class DataPlaneConnection;

class ServerWorker : public QObject
{
    Q_OBJECT
private:
    addrUnion server_addr, client_addr;
    SSL *ssl;
    int fd;
    DataPlaneConnection *con;

public:
    explicit ServerWorker(addrUnion server_addr, addrUnion client_addr, SSL* ssl, DataPlaneConnection* con, QObject *parent = 0);
    void disconnect();
    void sendBytes(const char* buf, int len);
signals:

public slots:
    void connection_handle();
};

#endif // SERVERWORKER_H
