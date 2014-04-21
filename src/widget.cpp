#include "widget.h"
#include "ui_widget.h"
#include "character.h"
#include "message.h"
#include "utils.h"
#include "items.h"

#ifndef chatTag
#include "tag.h"
#endif

#if defined _WIN32 || defined WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget),
    cmdPeer(new Player()),
    usedids(new bool[65536])
{
    tcpServer = new QTcpServer(this);
    udpSocket = new QUdpSocket(this);
    tcpReceivedDatas = new QByteArray();
    ui->setupUi(this);

    pingTimer = new QTimer(this);

    qsrand(QDateTime::currentMSecsSinceEpoch());
}

/// Adds the message in the log, and sets it as the status message
void Widget::logStatusMessage(QString msg, QString tag){
    if(tag == udpTag)
        udpMessages[udpMessageCounter++] = msg;

    if(tag == tcpTag)
        tcpMessages[tcpMessageCounter++] = msg;

    if(tag == chatTag)
        chatMessages[chatMessageCounter++] = msg;

    ui->log->appendPlainText(msg);
    ui->status->setText(msg);
    ui->log->repaint();
    ui->status->repaint();
}

/// Adds the message to the log
void Widget::logMessage(QString msg, QString tag){
    if(tag == udpTag)
        udpMessages[udpMessageCounter++] = msg;

    if(tag == tcpTag)
        tcpMessages[tcpMessageCounter++] = msg;

    if(tag == chatTag)
        chatMessages[chatMessageCounter++] = msg;

    if (!logInfos)
        return;
    ui->log->appendPlainText(msg);
    ui->log->repaint();
}

/// Reads the config file (server.ini) and start the server accordingly
void Widget::startServer()
{
    logStatusMessage("iQuestria Private Server v0.5.2-alpha", sysTag);
    logStatusMessage("Project adapted from GitHub repo tux3/LoE-PrivateServer", sysTag);
#ifdef __APPLE__
    // this fixes the directory in OSX so we can use the relative CONFIGFILEPATH and etc properly
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    CFURLRef resourcesURL = CFBundleCopyBundleURL(mainBundle);
    char path[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8 *)path, PATH_MAX))
    {
        // error!
    }
    CFRelease(resourcesURL);
    // the path we get is to the .app folder, so we go up after we chdir
    chdir(path);
    chdir("..");
#endif
    lastNetviewId=0;
    lastId=1;

    /// Read config
    logStatusMessage("Reading config file...", sysTag);
    QSettings config(CONFIGFILEPATH, QSettings::IniFormat);
    loginPort = config.value("loginPort", 1031).toInt();
    gamePort = config.value("gamePort", 1039).toInt();
    maxConnected = config.value("maxConnected",128).toInt();
    maxRegistered = config.value("maxRegistered",2048).toInt();
    pingTimeout = config.value("pingTimeout", 15).toInt();
    pingCheckInterval = config.value("pingCheckInterval", 5000).toInt();
    logInfos = config.value("logInfosMessages", true).toBool();
    saltPassword = config.value("saltPassword", "Change Me").toString();
    enableSessKeyValidation = config.value("enableSessKeyValidation", true).toBool();
    enableLoginServer = config.value("enableLoginServer", true).toBool();
    enableGameServer = config.value("enableGameServer", true).toBool();
    enableMultiplayer = config.value("enableMultiplayer", true).toBool();
    syncInterval = config.value("syncInterval",DEFAULT_SYNC_INTERVAL).toInt();
    remoteLoginIP = config.value("remoteLoginIP", "127.0.0.1").toString();
    remoteLoginPort = config.value("remoteLoginPort", 1031).toInt();
    remoteLoginTimeout = config.value("remoteLoginTimeout", 5000).toInt();
    useRemoteLogin = config.value("useRemoteLogin", false).toBool();
    enableGetlog = config.value("enableGetlog", true).toBool();

    /// Init servers
    tcpClientsList.clear();
#if defined _WIN32 || defined WIN32
    startTimestamp = GetTickCount();
#elif __APPLE__
    timeval time;
    gettimeofday(&time, NULL);
    startTimestamp = (time.tv_sec * 1000) + (time.tv_usec / 1000);
#else
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    startTimestamp = tp.tv_sec*1000 + tp.tv_nsec/1000/1000;
#endif

    /// Read vortex DB
    if (enableGameServer)
    {
        bool corrupted=false;
        QDir vortexDir("data/vortex/");
        QStringList files = vortexDir.entryList(QDir::Files);
        int nVortex=0;
        for (int i=0; i<files.size(); i++) // For each vortex file
        {
            // Each file is a scene
            Scene scene(files[i].split('.')[0]);

            QFile file("data/vortex/"+files[i]);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                logStatusMessage("Error reading vortex DB", sysTag);
                return;
            }
            QByteArray data = file.readAll();
            data.replace('\r', "");
            QList<QByteArray> lines = data.split('\n');

            // Each line is a vortex
            for (int j=0; j<lines.size(); j++)
            {
                if (lines[j].size() == 0) // Skip empty lines
                    continue;
                nVortex++;
                Vortex vortex;
                bool ok1, ok2, ok3, ok4;
                QList<QByteArray> elems = lines[j].split(' ');
                if (elems.size() < 5)
                {
                    logStatusMessage("Vortex DB is corrupted. Incorrect line ("+QString().setNum(elems.size())+" elems), file " + files[i], sysTag);
                    corrupted=true;
                    break;
                }
                vortex.id = elems[0].toInt(&ok1, 16);
                vortex.destName = elems[1];
                for (int j=2; j<elems.size() - 3;j++) // Concatenate the string between id and poss
                    vortex.destName += " "+elems[j];
                vortex.destPos.x = elems[elems.size()-3].toFloat(&ok2);
                vortex.destPos.y = elems[elems.size()-2].toFloat(&ok3);
                vortex.destPos.z = elems[elems.size()-1].toFloat(&ok4);
                if (!(ok1&&ok2&&ok3&&ok4))
                    // i think its ok now
                {
                    logStatusMessage("Vortex DB is corrupted. Conversion failed, file " + files[i], sysTag);
                    corrupted=true;
                    break;
                }
                scene.vortexes << vortex;
                //win.logMessage("Add vortex "+QString().setNum(vortex.id)+" to "+vortex.destName+" "
                //               +QString().setNum(vortex.destPos.x)+" "
                //               +QString().setNum(vortex.destPos.y)+" "
                //               +QString().setNum(vortex.destPos.z));
            }
            scenes << scene;
        }

        if (corrupted)
        {
            stopServer();
            return;
        }

        logMessage("Loaded " + QString().setNum(nVortex) + " vortexes in " + QString().setNum(scenes.size()) + " scenes", sysTag);
    }

    /// Read/parse Items.xml
    if (enableGameServer)
    {
        QFile itemsFile("data/data/Items.xml");
        if (itemsFile.open(QIODevice::ReadOnly))
        {
            QByteArray data = itemsFile.readAll();
            wearablePositionsMap = parseItemsXml(data);
            win.logMessage("Loaded "+QString().setNum(wearablePositionsMap.size())+" items", sysTag);
        }
        else
        {
            win.logMessage("Couln't open Items.xml", sysTag);
            stopServer();
            return;
        }
    }

    /// Read NPC/Quests DB
    if (enableGameServer)
    {
        try
        {
            unsigned nQuests = 0;
            QDir npcsDir("data/npcs/");
            QStringList files = npcsDir.entryList(QDir::Files);
            for (int i=0; i<files.size(); i++, nQuests++) // For each vortex file
            {
                Quest quest("data/npcs/"+files[i], NULL);
                quests << quest;
                npcs << quest.npc;
            }
            logMessage("Loaded "+QString().setNum(nQuests)+" quests/npcs.", sysTag);
        }
        catch (QString& e)
        {
            enableGameServer = false;
        }
    }

    if (enableLoginServer)
    {
//      logStatusMessage("Loading players database...", sysTag);
        tcpPlayers = Player::loadPlayers();
    }

    // TCP server
    if (enableLoginServer)
    {
        logStatusMessage("Starting TCP login server on port "+QString().setNum(loginPort)+"...", sysTag);
        if (!tcpServer->listen(QHostAddress::Any,loginPort))
        {
            logStatusMessage("TCP: Unable to start server on port "+QString().setNum(loginPort)+": "+tcpServer->errorString(), sysTag);
            stopServer();
            return;
        }

        // If we use a remote login server, try to open a connection preventively.
        if (useRemoteLogin)
            remoteLoginSock.connectToHost(remoteLoginIP, remoteLoginPort);
    }

    // UDP server
    if (enableGameServer)
    {
        logStatusMessage("Starting UDP game server on port "+QString().setNum(gamePort)+"...", sysTag);
        if (!udpSocket->bind(gamePort, QUdpSocket::ReuseAddressHint|QUdpSocket::ShareAddress))
        {
            logStatusMessage("UDP: Unable to start server on port "+QString().setNum(gamePort), sysTag);
            stopServer();
            return;
        }
    }

    if (enableGameServer)
    {
        // Start ping timeout timer
        pingTimer->start(pingCheckInterval);
    }

    if (enableMultiplayer)
        sync.startSync();

    if (enableLoginServer || enableGameServer)
        logStatusMessage("Server started", sysTag);

    connect(ui->sendButton, SIGNAL(clicked()), this, SLOT(sendCmdLine()));
    if (enableLoginServer)
        connect(tcpServer, SIGNAL(newConnection()), this, SLOT(tcpConnectClient()));
    if (enableGameServer)
    {
        connect(udpSocket, SIGNAL(readyRead()),this, SLOT(udpProcessPendingDatagrams()));
        connect(pingTimer, SIGNAL(timeout()), this, SLOT(checkPingTimeouts()));
    }
}

int Widget::getNewNetviewId()
{
    int i;

    for (i = 0; i < 65536; i++) {
        usedids[i] = false;
    }

    for (int c = 0; c < npcs.size(); c++) {
        usedids[npcs[c]->netviewId] = true;
    }
    for (int c = 0; c < udpPlayers.size(); c++) {
        usedids[udpPlayers[c]->pony.netviewId] = true;
    }
    
    i = 0;
    while (usedids[i]) i++;

    return i;
}

int Widget::getNewId()
{
    int i;

    for (i = 0; i < 65536; i++) {
        usedids[i] = false;
    }

    for (int c = 0; c < npcs.size(); c++) {
        usedids[npcs[c]->id] = true;
    }
    for (int c = 0; c < udpPlayers.size(); c++) {
        usedids[udpPlayers[c]->pony.id] = true;
    }
    
    i = 0;
    while (usedids[i]) i++;

    return i;
}

void Widget::stopServer()
{
    stopServer(true);
}

void Widget::stopServer(bool log)
{
    if (log)
        logStatusMessage("Stopping all server operations", sysTag);
    pingTimer->stop();
    tcpServer->close();
    for (int i=0;i<tcpClientsList.size();i++)
        tcpClientsList[i].first->close();
    udpSocket->close();

    sync.stopSync();

    disconnect(ui->sendButton, SIGNAL(clicked()), this, SLOT(sendCmdLine()));
    disconnect(udpSocket, SIGNAL(readyRead()),this, SLOT(udpProcessPendingDatagrams()));
    disconnect(tcpServer, SIGNAL(newConnection()), this, SLOT(tcpConnectClient()));
    disconnect(pingTimer, SIGNAL(timeout()), this, SLOT(checkPingTimeouts()));
    disconnect(this);
}

// Disconnect players, free the sockets, and exit quickly
// Does NOT run the atexits
Widget::~Widget()
{
    logInfos=false; // logMessage while we're trying to destroy would crash.
    //logMessage(QString("UDP: Disconnecting all players"));
    for (;udpPlayers.size();)
    {
        Player* player = udpPlayers[0];
        sendMessage(player, MsgDisconnect, "Connection closed by the server admin");

        // Save the pony
        QList<Pony> ponies = Player::loadPonies(player);
        for (int i=0; i<ponies.size(); i++)
            if (ponies[i].ponyData == player->pony.ponyData)
                ponies[i] = player->pony;
        Player::savePonies(player, ponies);
        player->pony.saveInventory();
        player->pony.saveQuests();

        // Free
        delete player;
        udpPlayers.removeFirst();
    }

    for (int i=0; i<quests.size(); i++)
    {
        delete quests[i].commands;
        delete quests[i].name;
        delete quests[i].descr;
    }

    stopServer(false);
    delete tcpServer;
    delete tcpReceivedDatas;
    delete udpSocket;
    delete pingTimer;
    delete cmdPeer;
    delete[] usedids;

    delete ui;

    // We freed everything that was important, so don't waste time in atexits
#if defined WIN32 || defined _WIN32 || defined __APPLE__
    _exit(EXIT_SUCCESS);
#else
    quick_exit(EXIT_SUCCESS);
#endif
}
