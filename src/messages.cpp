#include "message.h"
#include "character.h"
#include "widget.h"
#include "serialize.h"
#ifndef chatTag
#include "tag.h"
#endif

#define DEBUG_LOG false

// File-global game-entering mutex (to prevent multiple instantiates)
static QMutex levelLoadMutex;

void sendPonies(Player* player)
{
    // The full request is like a normal sendPonies but with all the serialized ponies at the end
    QList<Pony> ponies = Player::loadPonies(player);
    quint32 poniesDataSize=0;
    for (int i=0;i<ponies.size();i++)
        poniesDataSize+=ponies[i].ponyData.size();

    QByteArray data(5,0);
    data[0] = 1; // Request number
    data[1] = (quint8)(ponies.size()&0xFF); // Number of ponies
    data[2] = (quint8)((ponies.size()>>8)&0xFF); // Number of ponies
    data[3] = (quint8)((ponies.size()>>16)&0xFF); // Number of ponies
    data[4] = (quint8)((ponies.size()>>24)&0xFF); // Number of ponies

    for (int i=0;i<ponies.size();i++)
        data += ponies[i].ponyData;

    win.logMessage(QString("UDP: Sending characters data to ")+QString().setNum(player->pony.netviewId), udpTag);
    sendMessage(player, MsgUserReliableOrdered4, data);
}

void sendEntitiesList(Player *player)
{
    levelLoadMutex.lock(); // Protect player->inGame
    if (player->inGame == 0) // Not yet in game, send player's ponies list (Characters scene)
    {
        levelLoadMutex.unlock();
#if DEBUG_LOG
        win.logMessage("UDP: Sending ponies list", udpTag);
#endif
        sendPonies(player);
        return;
    }
    else if (player->inGame > 1) // Not supposed to happen, let's do it anyway
    {
        //levelLoadMutex.unlock();
        win.logMessage(QString("UDP: Entities list already sent to ")+QString().setNum(player->pony.netviewId)
                       +", resending anyway", udpTag);
        //return;
    }
    else // Loading finished, sending entities list
    win.logMessage(QString("UDP: Sending entities list to ")+QString().setNum(player->pony.netviewId), udpTag);
    Scene* scene = findScene(player->pony.sceneName); // Spawn all the players on the client
    for (int i=0; i<scene->players.size(); i++)
        sendNetviewInstantiate(&scene->players[i]->pony, player);

    // Send npcs
    for (int i=0; i<win.npcs.size(); i++)
        if (win.npcs[i]->sceneName.toLower() == player->pony.sceneName.toLower())
        {
            win.logMessage("UDP: Sending NPC "+win.npcs[i]->name, udpTag);
            sendNetviewInstantiate(win.npcs[i], player);
        }

    // Spawn some mobs in Zecoras
    if (scene->name.toLower() == "zecoras")
    {
        sendNetviewInstantiate(player,"mobs/dragon", win.getNewId(), win.getNewNetviewId(), {-33.0408, 0.000425577, 101.766}, {0,-1,0,1});
    }

    player->inGame = 2;
    levelLoadMutex.unlock();

    // Send stats of the client's pony
    sendSetMaxStatRPC(player, 0, 100);
    sendSetStatRPC(player, 0, 100);
    sendSetMaxStatRPC(player, 1, 100);
    sendSetStatRPC(player, 1, 100);
}

void sendPonySave(Player *player, QByteArray msg)
{
    if (player->inGame < 2) // Not supposed to happen, ignoring the request
    {
        win.logMessage("UDP: Savegame requested too soon by "+QString().setNum(player->pony.netviewId), udpTag);
        return;
    }

    quint16 netviewId = (quint8)msg[6] + ((quint16)(quint8)msg[7]<<8);
    Player* refresh = Player::findPlayer(win.udpPlayers, netviewId); // Find players

    // Find NPC
    Pony* npc = NULL;
    for (int i=0; i<win.npcs.size(); i++)
        if (win.npcs[i]->netviewId == netviewId)
            npc = win.npcs[i];

    // If we found a matching NPC, send him and exits
    if (npc != NULL)
    {
        win.logMessage("UDP: Sending ponyData and worn items for NPC "+npc->name, udpTag);
        sendPonyData(npc, player);
        //sendWornRPC(npc, player, npc->worn);
        return;
    }

    if (netviewId == player->pony.netviewId) // Current player
    {
                                 // \/ hopefully
        if (player->inGame == 3) // Hopefully that'll fix people stuck on the default cam without creating clones
        {
            win.logMessage("UDP: Savegame already sent to "+QString().setNum(player->pony.netviewId)
                           +", resending anyway", udpTag);
        }
        else
            win.logMessage(QString("UDP: Sending pony save for/to ")+QString().setNum(netviewId), udpTag);

        // Set current/max stats
        sendSetMaxStatRPC(player, 0, 100);
        sendSetStatRPC(player, 0, 100);
        sendSetMaxStatRPC(player, 1, 100);
        sendSetStatRPC(player, 1, 100);

        sendPonyData(player);

        // Send inventory
        sendInventoryRPC(player, player->pony.inv, player->pony.worn, player->pony.nBits);

        // Send skills
        QList<QPair<quint32, quint32> > skills;
        skills << QPair<quint32, quint32>(10, 0); // Ground pound (all races)
        if (player->pony.getType() == Pony::EarthPony)
        {
            skills << QPair<quint32, quint32>(5, 0); // Seismic buck
        }
        else if (player->pony.getType() == Pony::Pegasus)
        {
            skills << QPair<quint32, quint32>(11, 0); // Dual Cyclone
        }
        else if (player->pony.getType() == Pony::Unicorn)
        {
            skills << QPair<quint32, quint32>(2, 0); // Teleport
            skills << QPair<quint32, quint32>(9, 0); // Rainbow Fields
        }
        sendSkillsRPC(player, skills);

        // Set current/max stats again (that's what the official server does, not my idea !)
        sendSetMaxStatRPC(player, 0, 100);
        sendSetStatRPC(player, 0, 100);
        sendSetMaxStatRPC(player, 1, 100);
        sendSetStatRPC(player, 1, 100);

        refresh->inGame = 3;
    }
    else if (!refresh->IP.isEmpty())
    {
        win.logMessage(QString("UDP: Sending pony save for ")+QString().setNum(refresh->pony.netviewId)
                       +" to "+QString().setNum(player->pony.netviewId), udpTag);

        //sendWornRPC(refresh, player, refresh->worn);

        sendSetStatRPC(refresh, player, 0, 100);
        sendSetMaxStatRPC(refresh, player, 0, 100);
        sendSetStatRPC(refresh, player, 1, 100);
        sendSetMaxStatRPC(refresh, player, 1, 100);

        sendPonyData(&refresh->pony, player);

        if (!refresh->lastValidReceivedAnimation.isEmpty())
            sendMessage(player, MsgUserReliableOrdered12, refresh->lastValidReceivedAnimation);
    }
    else
    {
        win.logMessage("UDP: Error sending pony save : netviewId "+QString().setNum(netviewId)+" not found", udpTag);
    }
}

void sendNetviewInstantiate(Player *player, QString key, quint16 NetviewId, quint16 ViewId, UVector pos, UQuaternion rot)
{
    QByteArray data(1,1);
    data += stringToData(key);
    QByteArray data2(4,0);
    data2[0]=(quint8)(NetviewId&0xFF);
    data2[1]=(quint8)((NetviewId>>8)&0xFF);
    data2[2]=(quint8)(ViewId&0xFF);
    data2[3]=(quint8)((ViewId>>8)&0xFF);
    data += data2;
    data += vectorToData(pos);
    data += quaternionToData(rot);
    sendMessage(player, MsgUserReliableOrdered6, data);
}

void sendNetviewInstantiate(Player *player)
{
    win.logMessage("UDP: Send instantiate for/to "+QString().setNum(player->pony.netviewId), udpTag);
    QByteArray data(1,1);
    data += stringToData("PlayerBase");
    QByteArray data2(4,0);
    data2[0]=(quint8)(player->pony.netviewId&0xFF);
    data2[1]=(quint8)((player->pony.netviewId>>8)&0xFF);
    data2[2]=(quint8)(player->pony.id&0xFF);
    data2[3]=(quint8)((player->pony.id>>8)&0xFF);
    data += data2;
    data += vectorToData(player->pony.pos);
    data += quaternionToData(player->pony.rot);
    sendMessage(player, MsgUserReliableOrdered6, data);

    win.logMessage(QString("Instantiate at ")+QString().setNum(player->pony.pos.x)+" "
                   +QString().setNum(player->pony.pos.y)+" "
                   +QString().setNum(player->pony.pos.z), udpTag);
}

void sendNetviewInstantiate(Pony *src, Player *dst)
{
    win.logMessage("UDP: Send instantiate for "+QString().setNum(src->netviewId)
                   +" to "+QString().setNum(dst->pony.netviewId), udpTag);
    QByteArray data(1,1);
    data += stringToData("PlayerBase");
    QByteArray data2(4,0);
    data2[0]=(quint8)(src->netviewId&0xFF);
    data2[1]=(quint8)((src->netviewId>>8)&0xFF);
    data2[2]=(quint8)(src->id&0xFF);
    data2[3]=(quint8)((src->id>>8)&0xFF);
    data += data2;
    data += vectorToData(src->pos);
    data += quaternionToData(src->rot);
    sendMessage(dst, MsgUserReliableOrdered6, data);

   //win.logMessage(QString("Instantiate at ")+QString().setNum(rSrc.pony.pos.x)+" "
   //                +QString().setNum(rSrc.pony.pos.y)+" "
   //                +QString().setNum(rSrc.pony.pos.z));
}

void sendNetviewRemove(Player *player, quint16 netviewId)
{
    win.logMessage("UDP: Removing netview "+QString().setNum(netviewId)+" to "+QString().setNum(player->pony.netviewId), udpTag);

    QByteArray data(3,2);
    data[1] = (quint8)(netviewId&0xFF);
    data[2] = (quint8)((netviewId>>8)&0xFF);
    sendMessage(player, MsgUserReliableOrdered6, data);
}

void sendSetStatRPC(Player *player, quint8 statId, float value)
{
    QByteArray data(4,50);
    data[0] = (quint8)(player->pony.netviewId&0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8)&0xFF);
    data[3] = statId;
    data += floatToData(value);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendSetMaxStatRPC(Player* player, quint8 statId, float value)
{
    QByteArray data(4,51);
    data[0] = (quint8)(player->pony.netviewId&0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8)&0xFF);
    data[3] = statId;
    data += floatToData(value);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendSetStatRPC(Player* affected, Player* dest, quint8 statId, float value)
{
    QByteArray data(4,50);
    data[0] = (quint8)(affected->pony.netviewId&0xFF);
    data[1] = (quint8)((affected->pony.netviewId>>8)&0xFF);
    data[3] = statId;
    data += floatToData(value);
    sendMessage(dest, MsgUserReliableOrdered18, data);
}

void sendSetMaxStatRPC(Player* affected, Player* dest, quint8 statId, float value)
{
    QByteArray data(4,51);
    data[0] = (quint8)(affected->pony.netviewId&0xFF);
    data[1] = (quint8)((affected->pony.netviewId>>8)&0xFF);
    data[3] = statId;
    data += floatToData(value);
    sendMessage(dest, MsgUserReliableOrdered18, data);
}

void sendWornRPC(Player* player)
{
    sendWornRPC(player, player->pony.worn);
}

void sendWornRPC(Player *player, QList<WearableItem> &worn)
{
    QByteArray data(3, 4);
    data[0] = (quint8)(player->pony.netviewId&0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8)&0xFF);
    data += 32; // Max Worn Items
    data += (uint8_t)worn.size();
    for (int i=0;i<worn.size();i++)
    {
        data += (quint8)((worn[i].index-1)&0xFF);
        data += (quint8)(worn[i].id&0xFF);
        data += (quint8)((worn[i].id>>8)&0xFF);
        data += (quint8)((worn[i].id>>16)&0xFF);
        data += (quint8)((worn[i].id>>24)&0xFF);
    }
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendWornRPC(Pony *wearing, Player *dest, QList<WearableItem> &worn)
{
    QByteArray data(3, 4);
    data[0] = (quint8)(wearing->netviewId&0xFF);
    data[1] = (quint8)((wearing->netviewId>>8)&0xFF);
    data += 32; // Max Worn Items
    data += worn.size();
    for (int i=0;i<worn.size();i++)
    {
        data += (quint8)((worn[i].index-1)&0xFF);
        data += (quint8)(worn[i].id&0xFF);
        data += (quint8)((worn[i].id>>8)&0xFF);
        data += (quint8)((worn[i].id>>16)&0xFF);
        data += (quint8)((worn[i].id>>24)&0xFF);
    }
    sendMessage(dest, MsgUserReliableOrdered18, data);
}

void sendInventoryRPC(Player* player)
{
    sendInventoryRPC(player, player->pony.inv, player->pony.worn, player->pony.nBits);
}

void sendInventoryRPC(Player *player, QList<InventoryItem>& inv, QList<WearableItem>& worn, quint32 nBits)
{
    QByteArray data(5, 5);
    data[0] = (quint8)(player->pony.netviewId & 0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8) & 0xFF);
    data[3] = MAX_INVENTORY_SIZE; // Max Inventory Size
    data[4] = (quint8)inv.size();
    for (int i=0;i<inv.size();i++)
    {
        data += (quint8)inv[i].index;
        data += (quint8)(inv[i].id & 0xFF);
        data += (quint8)((inv[i].id>>8) & 0xFF);
        data += (quint8)((inv[i].id>>16) & 0xFF);
        data += (quint8)((inv[i].id>>24) & 0xFF);
        data += (quint8)(inv[i].amount & 0xFF);
        data += (quint8)((inv[i].amount>>8) & 0xFF);
        data += (quint8)((inv[i].amount>>16) & 0xFF);
        data += (quint8)((inv[i].amount>>24) & 0xFF);
    }
    data += MAX_WORN_ITEMS; // Max Worn Items
    data += worn.size();
    for (int i=0;i<worn.size();i++)
    {
        data += (quint8)worn[i].index-1;
        data += (quint8)(worn[i].id & 0xFF);
        data += (quint8)((worn[i].id>>8) & 0xFF);
        data += (quint8)((worn[i].id>>16) & 0xFF);
        data += (quint8)((worn[i].id>>24) & 0xFF);
    }
    data += (quint8)(nBits & 0xFF);
    data += (quint8)((nBits>>8) & 0xFF);
    data += (quint8)((nBits>>16) & 0xFF);
    data += (quint8)((nBits>>24) & 0xFF);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendSetBitsRPC(Player* player)
{
    QByteArray data(3, 0x10);
    data[0] = (quint8)(player->pony.netviewId & 0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8) & 0xFF);
    data += uint32ToData(player->pony.nBits);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendSkillsRPC(Player* player, QList<QPair<quint32, quint32> > &skills)
{
    QByteArray data(8, 0xC3);
    data[0] = (quint8)(player->pony.netviewId&0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8)&0xFF);
    data[3] = 0x00; // Use dictionnary flag
    data[4] = (quint8)(skills.size()&0xFF);
    data[5] = (quint8)((skills.size()>>8)&0xFF);
    data[6] = (quint8)((skills.size()>>16)&0xFF);
    data[7] = (quint8)((skills.size()>>24)&0xFF);
    for (int i=0;i<skills.size();i++)
    {
        data += (quint8)(skills[i].first&0xFF);
        data += (quint8)((skills[i].first>>8)&0xFF);
        data += (quint8)((skills[i].first>>16)&0xFF);
        data += (quint8)((skills[i].first>>24)&0xFF);
        data += (quint8)(skills[i].second&0xFF);
        data += (quint8)((skills[i].second>>8)&0xFF);
        data += (quint8)((skills[i].second>>16)&0xFF);
        data += (quint8)((skills[i].second>>24)&0xFF);
    }
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendPonyData(Player *player)
{
    // Sends the ponyData
    //win.logMessage(QString("UDP: Sending the ponyData for/to "+QString().setNum(player->pony.netviewId)));
    QByteArray data(3,0xC8);
    data[0] = (quint8)(player->pony.netviewId&0xFF);
    data[1] = (quint8)((player->pony.netviewId>>8)&0xFF);
    data += player->pony.ponyData;
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendPonyData(Pony *src, Player *dst)
{
    // Sends the ponyData
    //win.logMessage(QString("UDP: Sending the ponyData for "+QString().setNum(src->pony.netviewId)
    //                       +" to "+QString().setNum(dst->pony.netviewId)));
    QByteArray data(3,0xC8);
    data[0] = (quint8)(src->netviewId&0xFF);
    data[1] = (quint8)((src->netviewId>>8)&0xFF);
    data += src->ponyData;
    sendMessage(dst, MsgUserReliableOrdered18, data);
}

void sendLoadSceneRPC(Player* player, QString sceneName) // Loads a scene and send to the default spawn
{
    win.logMessage(QString("UDP: Loading scene \"") + sceneName + "\" on "+QString().setNum(player->pony.netviewId), udpTag);
    Vortex vortex = findVortex(sceneName, 0);
    if (vortex.destName.isEmpty())
    {
        win.logMessage("UDP: Scene not in vortex DB. Aborting scene load.", udpTag);
        return;
    }

    Scene* scene = findScene(sceneName);
    Scene* oldScene = findScene(player->pony.sceneName);
    if (scene->name.isEmpty() || oldScene->name.isEmpty())
    {
        win.logMessage("UDP: Can't find the scene, aborting", udpTag);
        return;
    }

    // Update scene players
    //win.logMessage("sendLoadSceneRPC: locking");
    levelLoadMutex.lock();
    player->inGame=1;
    player->pony.pos = vortex.destPos;
    player->pony.sceneName = sceneName.toLower();
    player->lastValidReceivedAnimation.clear(); // Changing scenes resets animations
    Player::removePlayer(oldScene->players, player->IP, player->port);
    if (oldScene->name.toLower() != sceneName.toLower())
    {
        // Send remove RPC to the other players of the old scene
        for (int i=0; i<oldScene->players.size(); i++)
            sendNetviewRemove(oldScene->players[i], player->pony.netviewId);

        // Send instantiate to the players of the new scene
        for (int i=0; i<scene->players.size(); i++)
            if (scene->players[i]->inGame>=2)
                sendNetviewInstantiate(&player->pony, scene->players[i]);
    }
    scene->players << player;

    QByteArray data(1,5);
    data += stringToData(sceneName.toLower());
    sendMessage(player,MsgUserReliableOrdered6,data); // Sends a 48
    //win.logMessage("sendLoadSceneRPC: unlocking");
    levelLoadMutex.unlock();
}

void sendLoadSceneRPC(Player* player, QString sceneName, UVector pos) // Loads a scene and send to the given pos
{
    win.logMessage(QString(QString("UDP: Loading scene \"")+sceneName
                           +"\" to "+QString().setNum(player->pony.netviewId)
                           +" at "+QString().setNum(pos.x)+" "
                           +QString().setNum(pos.y)+" "
                           +QString().setNum(pos.z)), udpTag);

    Scene* scene = findScene(sceneName);
    Scene* oldScene = findScene(player->pony.sceneName);
    if (scene->name.isEmpty() || oldScene->name.isEmpty())
    {
        win.logMessage("UDP: Can't find the scene, aborting", udpTag);
        return;
    }

    // Update scene players
    //win.logMessage("sendLoadSceneRPC pos: locking");
    levelLoadMutex.lock();
    player->inGame=1;
    player->pony.pos = pos;
    player->pony.sceneName = sceneName.toLower();
    player->lastValidReceivedAnimation.clear(); // Changing scenes resets animations
    Player::removePlayer(oldScene->players, player->IP, player->port);
    if (oldScene->name.toLower() != sceneName.toLower())
    {
        // Send remove RPC to the other players of the old scene
        for (int i=0; i<oldScene->players.size(); i++)
            sendNetviewRemove(oldScene->players[i], player->pony.netviewId);

        // Send instantiate to the players of the new scene
        for (int i=0; i<scene->players.size(); i++)
            if (scene->players[i]->inGame>=2)
                sendNetviewInstantiate(&player->pony, scene->players[i]);
    }
    scene->players << player;

    QByteArray data(1,5);
    data += stringToData(sceneName.toLower());
    sendMessage(player,MsgUserReliableOrdered6,data); // Sends a 48
    //win.logMessage("sendLoadSceneRPC pos: unlocking");
    levelLoadMutex.unlock();
}

void sendChatMessage(Player* player, QString message, QString author, quint8 chatType)
{
    QByteArray idAndAccess(5,0);
    idAndAccess[0] = (quint8)(player->pony.netviewId&0xFF);
    idAndAccess[1] = (quint8)((player->pony.netviewId << 8)&0xFF);
    idAndAccess[2] = (quint8)((player->pony.id&0xFF));
    idAndAccess[3] = (quint8)((player->pony.id << 8)&0xFF);
    idAndAccess[4] = 0x0; // Access level
    QByteArray data(2,0);
    data[0] = 0xf; // RPC ID
    data[1] = chatType;
    data += stringToData(author);
    data += stringToData(message);
    data += idAndAccess;

    sendMessage(player,MsgUserReliableOrdered4,data); // Sends a 46
}

void sendMove(Player* player, float x, float y, float z)
{
    QByteArray data(1,0);
    data[0] = 0xce; // Request number
    data += floatToData(x);
    data += floatToData(y);
    data += floatToData(z);
    win.logMessage(QString("UDP: Moving character"), udpTag);
    sendMessage(player,MsgUserReliableOrdered4, data);
}

void sendBeginDialog(Player* player)
{
    QByteArray data(1,0);
    data[0] = 11; // Request number
    sendMessage(player,MsgUserReliableOrdered4, data);
}

void sendDialogMessage(Player* player, QString& message, QString NPCName, quint16 iconId)
{
    QByteArray data(1,0);
    data[0] = 0x11; // Request number
    data += stringToData(message);
    data += stringToData(NPCName);
    data += (quint8)(iconId&0xFF);
    data += (quint8)((iconId>>8)&0xFF);

    sendMessage(player,MsgUserReliableOrdered4, data);
}

void sendDialogOptions(Player* player, QList<QString>& answers)
{
    QByteArray data(5,0);
    data[0] = 0xC; // Request number
    data[1] = (quint8)(answers.size()&0xFF);
    data[2] = (quint8)((answers.size()>>8)&0xFF);
    data[3] = (quint8)((answers.size()>>16)&0xFF);
    data[4] = (quint8)((answers.size()>>24)&0xFF);
    for (int i=0; i<answers.size(); i++)
        data += stringToData(answers[i]);

    sendMessage(player,MsgUserReliableOrdered4, data);
}

void sendEndDialog(Player* player)
{
    QByteArray data(1,0);
    data[0] = 13; // Request number
    sendMessage(player,MsgUserReliableOrdered4, data);
}

void sendWearItemRPC(Player* player, const WearableItem& item)
{
    QByteArray data;
    data += uint16ToData(player->pony.netviewId);
    data += 0x8;
    data += uint32ToData(item.id);
    data += uint8ToData(item.index);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendWearItemRPC(Pony* wearing, Player* dest, const WearableItem& item)
{
    QByteArray data;
    data += uint16ToData(wearing->netviewId);
    data += 0x8;
    data += uint32ToData(item.id);
    data += uint8ToData(item.index);
    sendMessage(dest, MsgUserReliableOrdered18, data);
}

void sendUnwearItemRPC(Player* player, uint8_t slot)
{
    QByteArray data;
    data += uint16ToData(player->pony.netviewId);
    data += 0x9;
    data += uint8ToData(slot);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendUnwearItemRPC(Pony* wearing, Player* dest, uint8_t slot)
{
    QByteArray data;
    data += uint16ToData(wearing->netviewId);
    data += 0x9;
    data += uint8ToData(slot);
    sendMessage(dest, MsgUserReliableOrdered18, data);
}

void sendAddItemRPC(Player* player, const InventoryItem& item)
{
    QByteArray data;
    data += uint16ToData(player->pony.netviewId);
    data += 0x6;
    data += uint32ToData(item.id);
    data += uint32ToData(item.amount);
    data += uint32ToData(item.index);
    sendMessage(player, MsgUserReliableOrdered18, data);
}

void sendDeleteItemRPC(Player* player, uint8_t index, uint32_t qty)
{
    QByteArray data;
    data += uint16ToData(player->pony.netviewId);
    data += 0x7;
    data += uint8ToData(index);
    data += uint32ToData(qty);
    sendMessage(player, MsgUserReliableOrdered18, data);
}
