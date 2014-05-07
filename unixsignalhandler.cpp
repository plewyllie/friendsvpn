#include "unixsignalhandler.h"
#include <signal.h>

UnixSignalHandler* UnixSignalHandler::instance = NULL;

UnixSignalHandler::UnixSignalHandler(QObject *parent) :
    QObject(parent)
{
    setup_unix_signal_handlers();
}

UnixSignalHandler* UnixSignalHandler::getInstance() {
    static QMutex mutex;
    mutex.lock();
    if (instance == NULL) {
        instance = new UnixSignalHandler();
    }
    mutex.unlock();
    return instance;
}


int UnixSignalHandler::setup_unix_signal_handlers() {
    struct sigaction term;

    term.sa_handler = UnixSignalHandler::termSignalHandler;
    sigemptyset(&term.sa_mask);
    term.sa_flags |= SA_RESTART;

    if (sigaction(SIGINT, &term, 0) > 0)
        return 1;
    if (sigaction(SIGTERM, &term, 0) > 0)
        return 2;

    return 0;
}

void UnixSignalHandler::addQProcess(QProcess *p) {
    listOfProcessToKill.append(p);
}

void UnixSignalHandler::termSignalHandler(int) {
    UnixSignalHandler* u = UnixSignalHandler::getInstance();
    foreach (QProcess* p, u->listOfProcessToKill) {
        p->close(); // close process
    }
    exit(0);
}

void UnixSignalHandler::addIp(QString ip, QString interface) {
    // TODO
}
