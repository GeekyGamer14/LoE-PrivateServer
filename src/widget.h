#ifndef WIDGET_H
#define WIDGET_H

#include <QtWidgets/QWidget>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTcpServer>
#include <QByteArray>
#include <QTimer>
#include <QList>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QtConcurrent/QtConcurrentMap>
#include <QCryptographicHash>
#include <QMutex>
#include <QMap>
#include <cstdint>

//#include <windows.h>
#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#endif

#include "character.h"
#include "scene.h"
#include "sync.h"
#include "quest.h"

#define PLAYERSPATH "data/players/"
#define NETDATAPATH "data/netData/"
#define CONFIGFILEPATH "data/server.ini"
#define SERVERSLISTFILEPATH "data/serversList.cfg"
#define sysTag "SYSTEM"
#define chatTag "CHAT"
#define udpTag "UDP"
#define tcpTag "TCP"

namespace Ui {
class Widget;
}

class Widget : public QWidget
{
    Q_OBJECT

    /// Main functions
public slots:
    void sendCmdLine();
    void checkPingTimeouts();
public:
    explicit Widget(QWidget *parent = 0);
    ~Widget();
    // going for the android tag approach for filters, heh.
    void logMessage(QString msg, QString tag);
    void logStatusMessage(QString msg, QString tag);
    void startServer();
    void stopServer(); // Calls stopServer(true)
    void stopServer(bool log);
    int getNewNetviewId();
    int getNewId();
    // Filters
    int udpMessageCounter;
    QString udpMessages[];
    int chatMessageCounter;
    QString chatMessages[];
    int tcpMessageCounter;
    QString tcpMessages[];
    int globalMessageCounter;
    QString globalMessages[];

    /// UDP/TCP
public slots:
    void udpProcessPendingDatagrams();
    void tcpConnectClient();
    void tcpDisconnectClient();
    void tcpProcessPendingDatagrams();
public:
    void tcpProcessData(QByteArray data, QTcpSocket *socket);

public:
    float startTimestamp;
    QUdpSocket *udpSocket;
    QList<Player*> tcpPlayers; // Used by the TCP login server
    QList<Player*> udpPlayers; // Used by the UDP game server
    QList<Pony*> npcs; // List of npcs from the npcs DB
    QList<Quest> quests; // List of quests from the npcs DB
    QMap<uint32_t, uint32_t> wearablePositionsMap; // Maps item IDs to their wearable positions.
    int loginPort; // Port for the login server
    int gamePort; // Port for the game server
    QList<Scene> scenes; // List of scenes from the vortex DB
    int lastNetviewId;
    int lastId;
    QMutex lastIdMutex; // Protects lastId and lastNetviewId
    int syncInterval;

private:
    Ui::Widget* ui;
    QTcpServer* tcpServer;
    QList<QPair<QTcpSocket*, QByteArray*>> tcpClientsList;
    QTcpSocket remoteLoginSock; // Socket to the remote login server, if we use one
    QByteArray* tcpReceivedDatas;
    Player* cmdPeer; // Player selected for the server commands
    QTimer* pingTimer;
    Sync sync;
    bool* usedids;

    // Config
    QString remoteLoginIP; // IP of the remote login server
    int remoteLoginPort; // Port of the remote login server
    int remoteLoginTimeout; // Time before we give up connecting to the remote login server
    bool useRemoteLogin; // Whever or not to use the remote login server
    int maxConnected; // Max numbre of players connected at the same time, can deny login
    int maxRegistered; // Max number of registered players in database, can deny registration
    int pingTimeout; // Max time between recdption of pings, can disconnect player
    int pingCheckInterval; // Time between ping timeout checks
    bool logInfos; // Can disable logMessage, but doesn't affect logStatusMessage
    QString saltPassword; // Used to check passwords between login and game servers, must be the same on all the servers involved
    bool enableSessKeyValidation; // Enable Session Key Validation
    bool enableLoginServer; // Starts a login server
    bool enableGameServer; // Starts a game server
    bool enableMultiplayer; // Sync players' positions
    bool enableGetlog; // Enable GET /log requests
};

// Global import from main
extern Widget win;

#endif // WIDGET_H
