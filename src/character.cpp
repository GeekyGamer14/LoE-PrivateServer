#include <QFile>
#include <QDir>
#include "widget.h"
#include "character.h"
#include "utils.h"
#include "serialize.h"
#include "message.h"
#include "sendMessage.h"
#include "items.h"

#define DEBUG_LOG false

SceneEntity::SceneEntity()
{
    modelName = QString();
    id = 0;
    netviewId = 0;
    pos=UVector(0,0,0);
    rot=UQuaternion(0,0,0,0);
    sceneName = QString();
}

Pony::Pony(Player *Owner)
    : SceneEntity(), owner(Owner)
{
    modelName = "PlayerBase";
    name = "";
    wornSlots = 0;
}

Pony::type Pony::getType()
{
    // Variable UInt32
    unsigned char num3;
    int num = 0;
    int num2 = 0;
    int i=0;
    do
    {
        num3 = ponyData[i]; i++;
        num |= (num3 & 0x7f) << num2;
        num2 += 7;
    } while ((num3 & 0x80) != 0);
    unsigned off = (uint) num  + i;
    return (type)(quint8)ponyData[off];
}

Player::Player()
    : pony{Pony(this)}
{
    connected=false;
    inGame=0;
    nReceivedDups=0;
    lastPingNumber=0;
    lastPingTime=timestampNow();
    port=0;
    IP=QString();
    receivedDatas = new QByteArray();
    for (int i=0;i<33;i++)
        udpSequenceNumbers[i]=0;
    for (int i=0;i<33;i++)
        udpRecvSequenceNumbers[i]=0;
    udpRecvMissing.clear();

    // Prepare timers
    chatRollCooldownEnd = QDateTime::currentDateTime();
    udpSendReliableTimer =  new QTimer;
    udpSendReliableTimer->setInterval(UDP_RESEND_TIMEOUT);
    udpSendReliableTimer->setSingleShot(true);
    connect(udpSendReliableTimer, SIGNAL(timeout()), this, SLOT(udpResendLast()));
    udpSendReliableGroupTimer =  new QTimer;
    udpSendReliableGroupTimer->setInterval(UDP_GROUPING_TIMEOUT);
    udpSendReliableGroupTimer->setSingleShot(true);
    connect(udpSendReliableGroupTimer, SIGNAL(timeout()), this, SLOT(udpDelayedSend()));
}

Player::~Player()
{
    disconnect(udpSendReliableGroupTimer);
    disconnect(udpSendReliableTimer);
    delete udpSendReliableGroupTimer;
    delete udpSendReliableTimer;
    delete receivedDatas;
}

void Player::reset()
{
    name.clear();
    connected=false;
    inGame=0;
    nReceivedDups=0;
    lastPingNumber=0;
    lastPingTime=timestampNow();
    port=0;
    IP.clear();
    receivedDatas->clear();
    lastValidReceivedAnimation.clear();
    pony = Pony(this);
    for (int i=0;i<33;i++)
        udpSequenceNumbers[i]=0;
    for (int i=0;i<33;i++)
        udpRecvSequenceNumbers[i]=0;
    udpRecvMissing.clear();
}

void Player::resetNetwork()
{
    connected=false;
    nReceivedDups=0;
    lastPingNumber=0;
    lastPingTime=timestampNow();
    port=0;
    IP.clear();
    receivedDatas->clear();
    for (int i=0;i<33;i++)
        udpSequenceNumbers[i]=0;
    for (int i=0;i<33;i++)
        udpRecvSequenceNumbers[i]=0;
    udpRecvMissing.clear();
}

bool Player::savePlayers(QList<Player*>& playersData)
{
    QFile playersFile("data/players/players.dat");
    if (!playersFile.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        win.logStatusMessage("Error saving players database", sysTag);
        win.stopServer();
        return false;
    }

    for (int i=0;i<playersData.size();i++)
    {
        playersFile.write(playersData[i]->name.toLatin1());
        playersFile.write("\31");
        playersFile.write(playersData[i]->passhash.toLatin1());
        if (i+1!=playersData.size())
            playersFile.write("\n");
    }
    return true;
}

QList<Player*> Player::loadPlayers()
{
    QList<Player*> players;
    QFile playersFile("data/players/players.dat");
    if (!playersFile.open(QIODevice::ReadOnly))
    {
        win.logStatusMessage("Error reading players database", sysTag);
        win.stopServer();
        return players;
    }
    QList<QByteArray> data = playersFile.readAll().split('\n');
    if (data.size()==1 && data[0].isEmpty())
    {
        win.logMessage("Player database is empty. Continuing happily", sysTag);
        return players;
    }
    for (int i=0;i<data.size();i++)
    {
        QList<QByteArray> line = data[i].split('\31');
        if (line.size()!=2)
        {
            win.logStatusMessage("Error reading players database", sysTag);
            win.stopServer();
            return players;
        }
        Player* newPlayer = new Player;
        newPlayer->name = line[0];
        newPlayer->passhash = line[1];
        players << newPlayer;
    }
    win.logMessage(QString("Got ")+QString().setNum(players.size())+" players in database", sysTag);
    return players;
}

Player* Player::findPlayer(QList<Player*>& players, QString uname)
{
    for (int i=0; i<players.size(); i++)
    {
        if (players[i]->name == uname)
            return players[i];
    }

    Player* emptyPlayer = new Player();
    return emptyPlayer;
}

Player* Player::findPlayer(QList<Player*>& players, QString uIP, quint16 uport)
{
    for (int i=0; i<players.size(); i++)
    {
        if (players[i]->IP == uIP && players[i]->port == uport)
            return players[i];
    }

    Player* emptyPlayer = new Player();
    return emptyPlayer;
}

Player* Player::findPlayer(QList<Player*>& players, quint16 netviewId)
{
    for (int i=0; i<players.size(); i++)
    {
        if (players[i]->pony.netviewId == netviewId)
            return players[i];
    }

    Player* emptyPlayer = new Player();
    return emptyPlayer;
}

void Player::savePonies(Player *player, QList<Pony> ponies)
{
    win.logMessage("UDP: Saving ponies for "+QString().setNum(player->pony.netviewId)+" ("+player->name+")", sysTag);

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(player->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+player->name.toLatin1()+"/ponies.dat");
    file.open(QIODevice::ReadWrite | QIODevice::Truncate);
    for (int i=0; i<ponies.size(); i++)
    {
        file.write(ponies[i].ponyData);
        file.write(vectorToData(ponies[i].pos));
        file.write(stringToData(ponies[i].sceneName.toLower()));
    }
}

QList<Pony> Player::loadPonies(Player* player)
{
    QList<Pony> ponies;
    QFile file(QDir::currentPath()+"/data/players/"+player->name.toLatin1()+"/ponies.dat");
    if (!file.open(QIODevice::ReadOnly))
        return ponies;

    QByteArray data = file.readAll();

    int i=0;
    while (i<data.size())
    {
        Pony pony{player};
        // Read the ponyData
        unsigned strlen;
        unsigned lensize=0;
        {
            unsigned char num3; int num=0, num2=0;
            do {
                num3 = data[i+lensize]; lensize++;
                num |= (num3 & 0x7f) << num2;
                num2 += 7;
            } while ((num3 & 0x80) != 0);
            strlen = (uint) num;
        }
        int ponyDataSize = strlen+lensize+43;
        pony.ponyData = data.mid(i,ponyDataSize);
        pony.name = dataToString(pony.ponyData); // The name is the first elem
        //win.logMessage("Found pony : "+pony.name);
        i+=ponyDataSize;

        // Read pos
        UVector pos = dataToVector(data.mid(i,12));
        pony.pos = pos;
        i+=12;

        // Read sceneName
        unsigned strlen2;
        unsigned lensize2=0;
        {
            unsigned char num3; int num=0, num2=0;
            do {
                num3 = data[i+lensize2]; lensize2++;
                num |= (num3 & 0x7f) << num2;
                num2 += 7;
            } while ((num3 & 0x80) != 0);
            strlen2 = (uint) num;
        }
        pony.sceneName = data.mid(i+lensize2, strlen2).toLower();
        i+=strlen2+lensize2;

        // Create quests
        for (int i=0; i<win.quests.size(); i++)
        {
            Quest quest = win.quests[i];
            quest.setOwner(player);
            pony.quests << quest;
        }

        ponies << pony;
    }

    return ponies;
}

void Player::removePlayer(QList<Player*>& players, QString uIP, quint16 uport)
{
    for (int i=0; i<players.size(); i++)
    {
        if (players[i]->IP == uIP && players[i]->port == uport)
            players.removeAt(i);
    }
}

void Player::disconnectPlayerCleanup(Player* player)
{
    static QMutex playerCleanupMutex;

    // Save the pony
    QList<Pony> ponies = loadPonies(player);
    for (int i=0; i<ponies.size(); i++)
        if (ponies[i].ponyData == player->pony.ponyData)
            ponies[i] = player->pony;
    savePonies(player, ponies);
    player->pony.saveQuests();
    player->pony.saveInventory();

    QString uIP = player->IP;
    quint16 uPort = player->port;

    Scene* scene = findScene(player->pony.sceneName);
    if (scene->name.isEmpty())
        win.logMessage("UDP: Can't find scene for player cleanup", udpTag);

    //win.logMessage("playerCleanup locking");
    playerCleanupMutex.lock();
    removePlayer(scene->players, uIP, uPort);
    for (int i=0; i<scene->players.size(); i++)
        sendNetviewRemove(scene->players[i], player->pony.netviewId);
    player->udpDelayedSend(); // We're about to remove the player, we can't delay the send
    player->udpSendReliableTimer->stop();
    player->udpSendReliableGroupTimer->stop();
    removePlayer(win.udpPlayers, uIP, uPort);
    delete player;
    //win.logMessage("playerCleanup unlocking");
    playerCleanupMutex.unlock();
}

void Player::udpResendLast()
{
    //win.logMessage("udpResendLast locking");
    if (!udpSendReliableMutex.tryLock())
    {
        win.logMessage("udpResendLast failed to lock.", udpTag);
        return; // Avoid deadlock if sendMessage just locked but didn't have the time to stop the timers
    }
    //udpSendReliableMutex.lock();
    QByteArray msg = udpSendReliableQueue.first();
    if (msg.isEmpty())
    {
        win.logMessage("udpResendLast: Empty message", udpTag);
        udpSendReliableTimer->start();
        //win.logMessage("udpResendLast unlocking");
        udpSendReliableMutex.unlock();
        return;
    }

#if DEBUG_LOG
    win.logMessage("Resending message : "+QString(msg.toHex().data()));
#endif

    // Simulate packet loss if enabled (DEBUG ONLY!)
#if UDP_SIMULATE_PACKETLOSS
    if (qrand() % 100 <= UDP_PERCENT_DROPPED)
    {
        win.logMessage("UDP: ResendLast packet dropped !");
        udpSendReliableTimer->start();
        //win.logMessage("udpResendLast unlocking");
        udpSendReliableMutex.unlock();
        return;
    }
    else
        win.logMessage("UDP: ResendLast packet got throught");
#endif

    if (win.udpSocket->writeDatagram(msg,QHostAddress(IP),port) != msg.size())
    {
        win.logMessage("UDP: Error sending last message", udpTag);
        win.logStatusMessage("Restarting UDP server...", udpTag);
        win.udpSocket->close();
        if (!win.udpSocket->bind(win.gamePort, QUdpSocket::ReuseAddressHint|QUdpSocket::ShareAddress))
        {
            win.logStatusMessage("UDP: Unable to start server on port "+QString().setNum(win.gamePort), udpTag);
            win.stopServer();
            return;
        }
    }

    udpSendReliableTimer->start();

    //win.logMessage("udpResendLast unlocking");
    udpSendReliableMutex.unlock();
}

void Player::udpDelayedSend()
{
    //win.logMessage("udpDelayedSend locking");
    if (!udpSendReliableMutex.tryLock())
    {
        win.logMessage("udpDelayedSend failed to lock.", udpTag);
        return; // Avoid deadlock if sendMessage just locked but didn't have the time to stop the timers
    }
    //udpSendReliableMutex.lock();
#if DEBUG_LOG
    win.logMessage("Sending delayed grouped message : "+QString(udpSendReliableGroupBuffer.toHex()), udpTag);
#endif

    // Move the grouped message to the reliable queue
    udpSendReliableQueue.append(udpSendReliableGroupBuffer);

    // If this is the only message queued, send it now
    // If it isn't, we need to wait until the previous one was ACK'd
    if (udpSendReliableQueue.size() >= 1)
    {
        // Simulate packet loss if enabled (DEBUG ONLY!)
#if UDP_SIMULATE_PACKETLOSS
        if (qrand() % 100 <= UDP_PERCENT_DROPPED)
        {
            win.logMessage("UDP: Delayed send packet dropped !");
            udpSendReliableGroupBuffer.clear();
            if (!udpSendReliableTimer->isActive())
                udpSendReliableTimer->start();
            //win.logMessage("udpDelayedSend unlocking");
            udpSendReliableMutex.unlock();
            return;
        }
        else
            win.logMessage("UDP: Delayed send packet got throught");
#endif

        if (win.udpSocket->writeDatagram(udpSendReliableGroupBuffer,QHostAddress(IP),port) != udpSendReliableGroupBuffer.size())
        {
            win.logMessage("UDP: Error sending last message", udpTag);
            win.logStatusMessage("Restarting UDP server...", udpTag);
            // why did i ever add tagging this is hard im only 1/4 of the way there
            win.udpSocket->close();
            if (!win.udpSocket->bind(win.gamePort, QUdpSocket::ReuseAddressHint|QUdpSocket::ShareAddress))
            {
                win.logStatusMessage("UDP: Unable to start server on port "+QString().setNum(win.gamePort), udpTag);
                win.stopServer();
                return;
            }
        }
    }

    udpSendReliableGroupBuffer.clear();

    if (!udpSendReliableTimer->isActive())
        udpSendReliableTimer->start();

    //win.logMessage("udpDelayedSend unlocking");
    udpSendReliableMutex.unlock();
}

void Pony::saveQuests()
{
    win.logMessage("UDP: Saving quests for "+QString().setNum(netviewId)+" ("+owner->name+")", udpTag);

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/quests.dat");
    if (!file.open(QIODevice::ReadWrite))
    {
        win.logMessage("Error saving quests for "+QString().setNum(netviewId)+" ("+owner->name+")", udpTag);
        return;
    }
    QByteArray questData = file.readAll();

    // Try to find an existing entry for this pony, if found delete it. Then go at the end.
    for (int i=0; i<questData.size();)
    {
        // Read the name
        QString entryName = dataToString(questData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(questData.mid(i));
        //win.logMessage("saveQuests : Reading entry "+entryName);

        quint16 entryDataSize = 4 * dataToUint16(questData.mid(i+nameSize));
        if (entryName == this->name) // Delete the entry, we'll rewrite it at the end
        {
            questData.remove(i,nameSize+entryDataSize+2);
            break;
        }
        else
            i += nameSize+entryDataSize+2;
    }

    // Now add our data at the end of the file
    QByteArray newEntry = stringToData(this->name);
    newEntry += (quint8)(quests.size() & 0xFF);
    newEntry += (quint8)((quests.size() >> 8) & 0xFF);
    for (const Quest& quest : quests)
    {
        newEntry += (quint8)(quest.id & 0xFF);
        newEntry += (quint8)((quest.id >> 8) & 0xFF);
        newEntry += (quint8)(quest.state & 0xFF);
        newEntry += (quint8)((quest.state >> 8) & 0xFF);
    }
    questData += newEntry;
    file.resize(0);
    file.write(questData);
    file.close();
}

void Pony::loadQuests()
{
    win.logMessage("UDP: Loading quests for "+QString().setNum(netviewId)+" ("+owner->name+")", udpTag);

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/quests.dat");
    if (!file.open(QIODevice::ReadOnly))
    {
        win.logMessage("Error loading quests for "+QString().setNum(netviewId)+" ("+owner->name+")", sysTag);
        return;
    }
    QByteArray questData = file.readAll();
    file.close();

    // Try to find an existing entry for this pony and load it.
    for (int i=0; i<questData.size();)
    {
        // Read the name
        QString entryName = dataToString(questData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(questData.mid(i));
        i+=nameSize;
        //win.logMessage("loadQuests : Reading entry "+entryName);

        quint16 nquests = dataToUint16(questData.mid(i));
        i+=2;
        if (entryName == this->name) // Read the entry
        {
            for (int j=0; j<nquests; j++)
            {
                quint16 questId = dataToUint16(questData.mid(i));
                quint16 questState = dataToUint16(questData.mid(i+2));
                i+=4;
                for (Quest& quest : quests)
                {
                    if (quest.id == questId)
                    {
                        quest.state = questState;
                        break;
                    }
                }
            }
            return;
        }
        else
            i += nquests * 4;
    }
}

void Pony::saveInventory()
{
    win.logMessage("UDP: Saving inventory for "+QString().setNum(netviewId)+" ("+owner->name+")", udpTag);

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/inventory.dat");
    if (!file.open(QIODevice::ReadWrite))
    {
        win.logMessage("Error saving inventory for "+QString().setNum(netviewId)+" ("+owner->name+")", sysTag);
        return;
    }
    QByteArray invData = file.readAll();

    // Try to find an existing entry for this pony, if found delete it. Then go at the end.
    for (int i=0; i<invData.size();)
    {
        // Read the name
        QString entryName = dataToString(invData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(invData.mid(i));
        //win.logMessage("saveInventory : Reading entry "+entryName);

        quint8 invSize = invData[i+nameSize+4];
        quint8 wornSize = invData[i+nameSize+4+1+invSize*9]; // Serialized sizeof InventoryItem is 9
        if (entryName == this->name) // Delete the entry, we'll rewrite it at the end
        {
            invData.remove(i,nameSize+4+1+invSize*9+1+wornSize*5);
            break;
        }
        else // Skip this entry
            i += nameSize+4+1+invSize*9+1+wornSize*5;
    }

    // Now add our data at the end of the file
    QByteArray newEntry = stringToData(this->name);
    newEntry += uint32ToData(nBits);
    newEntry += uint8ToData(inv.size());
    for (const InventoryItem& item : inv)
    {
        newEntry += uint8ToData(item.index);
        newEntry += uint32ToData(item.id);
        newEntry += uint32ToData(item.amount);
    }
    newEntry += uint8ToData(worn.size());
    for (const WearableItem& item : worn)
    {
        newEntry += uint8ToData(item.index);
        newEntry += uint32ToData(item.id);
    }
    invData += newEntry;
    file.resize(0);
    file.write(invData);
    file.close();
}

bool Pony::loadInventory()
{
    win.logMessage("UDP: Loading inventory for "+QString().setNum(netviewId)+" ("+owner->name+")", udpTag);

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/inventory.dat");
    if (!file.open(QIODevice::ReadOnly))
    {
        win.logMessage("Error loading inventory for "+QString().setNum(netviewId)+" ("+owner->name+")", sysTag);
        return false;
    }
    QByteArray invData = file.readAll();
    file.close();

    // Try to find an existing entry for this pony, if found load it.
    for (int i=0; i<invData.size();)
    {
        // Read the name
        QString entryName = dataToString(invData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(invData.mid(i));
        //win.logMessage("loadInventory : Reading entry "+entryName);

        quint8 invSize = invData[i+nameSize+4];
        quint8 wornSize = invData[i+nameSize+4+1+invSize*9]; // Serialized sizeof InventoryItem is 9
        if (entryName == this->name)
        {
            i += nameSize;
            nBits = dataToUint32(invData.mid(i));
            i+=5; // Skip nBits and invSize
            inv.clear();
            for (int j=0; j<invSize; j++)
            {
                InventoryItem item;
                item.index = invData[i];
                i++;
                item.id = dataToUint32(invData.mid(i));
                i+=4;
                item.amount = dataToUint32(invData.mid(i));
                i+=4;
                inv.append(item);
            }
            i++; // Skip wornSize
            worn.clear();
            wornSlots = 0;
            for (int j=0; j<wornSize; j++)
            {
                WearableItem item;
                item.index = invData[i];
                i++;
                item.id = dataToUint32(invData.mid(i));
                i+=4;
                worn.append(item);
                wornSlots |= win.wearablePositionsMap[item.id];
            }
            return true;
        }
        else // Skip this entry
            i += nameSize+4+1+invSize*9+1+wornSize*5;
    }
    return false; // Entry not found
}

void Pony::addInventoryItem(quint32 id, quint32 qty)
{
    bool found=false;
    int firstFreeIndex=0;
    while (!found && firstFreeIndex < MAX_INVENTORY_SIZE)
    {
        found=true;
        for (InventoryItem& item : inv)
        {
            if (item.id == id)
            {
                item.amount += qty;
                return;
            }
            else if (item.index == firstFreeIndex)
            {
                found=false;
                firstFreeIndex++;
                break;
            }
        }
    }
    if (!found)
        return;
    InventoryItem newItem(firstFreeIndex, id, qty);
    inv << newItem;
    sendAddItemRPC(owner, newItem);
}

void Pony::removeInventoryItem(quint32 id, quint32 qty)
{
    for (int i=0; i<inv.size(); i++)
    {
        if (inv[i].id == id)
        {
            sendDeleteItemRPC(owner, inv[i].index, qty);
            if (qty < inv[i].amount)
            {
                inv[i].amount -= qty;
                return;
            }
            else if (qty == inv[i].amount)
            {
                inv.removeAt(i);
                return;
            }
            else
            {
                qty -= inv[i].amount;
                inv.removeAt(i);
            }
        }
    }
}

bool Pony::hasInventoryItem(quint32 id, quint32 qty)
{
    for (const InventoryItem& item : inv)
    {
        if (item.id == id)
        {
            if (item.amount >= qty)
                return true;
            else
                qty -= item.amount;
        }
    }
    return qty==0;
}

void Pony::unwearItemAt(quint8 index)
{
    bool found=false;
    for (int i=0; i<worn.size();i++)
    {
        if (worn[i].index == index+1)
        {
            found=true;
            int itemSlots = win.wearablePositionsMap[worn[i].id];
            wornSlots = (wornSlots | itemSlots) ^ itemSlots;
            addInventoryItem(worn[i].id, 1);
            worn.removeAt(i);
            break;
        }
    }
    if (!found)
    {
        win.logMessage("Couldn't unwear item, index "+QString().setNum(index)+" not found", udpTag);
        return;
    }
    sendUnwearItemRPC(owner, index);

    Scene* scene = findScene(sceneName);
    if (scene->name.isEmpty())
        win.logMessage("UDP: Can't find the scene for unwearItem RPC, aborting", udpTag);
    else
    {
        for (Player* dest : scene->players)
            if (dest->pony.netviewId != netviewId)
                sendUnwearItemRPC(this, dest, index);
    }
}

bool Pony::tryWearItem(quint8 invSlot)
{
    //win.logMessage("Invslot is "+QString().setNum(invSlot));
    uint32_t id = -1;
    uint32_t itemSlots;
    for (int i=0; i<inv.size(); i++)
    {
        if (inv[i].index == invSlot)
        {
            id = inv[i].id;

            itemSlots = win.wearablePositionsMap[id];
            if (wornSlots & itemSlots)
            {
                win.logMessage("Can't wear item: slots occupied", udpTag);
                return false;
            }

            sendDeleteItemRPC(owner,inv[i].index,1);
            if (inv[i].amount>1)
                inv[i].amount--;
            else
                inv.removeAt(i);
            break;
        }
    }
    if (id == (uint32_t)-1)
    {
        // wtf?
        win.logMessage("Index not found", sysTag);
        return false;
    }
    wornSlots |= itemSlots;

    WearableItem item;
    item.id = id;
    item.index = wearablePositionsToSlot(itemSlots);
    //win.logMessage("Wearing at slot "+QString().setNum(item.index));
    worn << item;
    sendWearItemRPC(owner, item);

    Scene* scene = findScene(sceneName);
    if (scene->name.isEmpty())
        win.logMessage("UDP: Can't find the scene for wearItem RPC, aborting", udpTag);
    else
    {
        for (Player* dest : scene->players)
            if (dest->pony.netviewId != netviewId)
                sendWearItemRPC(this, dest, item);
    }

    return true;
}
