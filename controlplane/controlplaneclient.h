#ifndef CONTROLPLANECLIENT_H
#define CONTROLPLANECLIENT_H

#include <QObject>
#include <QHostAddress>
#include <QSslCertificate>
#include <QSslKey>
#include "controlplaneconnection.h"
#include "sslsocket.h"
class ConnectionInitiator;

/**
 * @brief The ControlPlaneClient class is used to make outgoing control plane connections.
 * The method run has to be called for the connection to be made, this enables to launch this
 * method inside a new QThread.
 */
class ControlPlaneClient: public QObject
{
    Q_OBJECT
private:
    ConnectionInitiator* init;

    SslSocket* sslClient;
    QSslCertificate servCert;
    QHostAddress addr;
    int port;
    QString friendUid;

public:
    ControlPlaneClient(QSslCertificate servCert, QSslKey myKey, QHostAddress addr,
                       int port, QString friendUid, QObject* parent = 0);
    ~ControlPlaneClient();
private slots:
    /**
     * @brief connectionReady is entered when the SSL handshake has succeeded
     */
    void connectionReady();
    /**
     * @brief sslClientReadyRead data is ready to be read
     */
    void sslClientReadyRead();
    void sslErrors(const QList<QSslError>& errs);
public slots:
    /**
     * @brief run will connect to the host given in the constructor; it won't do anything if
     * servCert was a NULL certificate. A qWarning() is emitted in that case.
     */
    void run();
    void sslDisconnected();
};

#endif // CONTROLPLANECLIENT_H
