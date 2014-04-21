#include "message.h"
#include "character.h"
#include "widget.h"
#include "sync.h"
#include "utils.h"
#include "serialize.h"
#include "receiveAck.h"
#include "receiveChatMessage.h"

#define DEBUG_LOG false

void receiveMessage(Player* player)
{
    QByteArray msg = *(player->receivedDatas);
    int msgSize=5 + (((unsigned char)msg[3]) + (((unsigned char)msg[4]) << 8))/8;

    // Check the sequence (seq) of the received messag
    if ((unsigned char)msg[0] >= MsgUserReliableOrdered1 && (unsigned char)msg[0] <= MsgUserReliableOrdered32)
    {
        quint16 seq = (quint8)msg[1] + ((quint8)msg[2]<<8);
        quint8 channel = ((unsigned char)msg[0])-MsgUserReliableOrdered1;
        if (seq <= player->udpRecvSequenceNumbers[channel] && player->udpRecvSequenceNumbers[channel]!=0)
        {
            // If this is a missing packet, accept it
            MessageHead missingMsg;
            missingMsg.channel = channel;
            missingMsg.seq = seq;
            if (player->udpRecvMissing.contains(missingMsg))
            {
                win.logMessage("UDP: Processing retransmission (-"+QString().setNum(player->udpRecvSequenceNumbers[channel]-seq)
                               +") from "+QString().setNum(player->pony.netviewId), udpTag);
                for (int i=0; i<player->udpRecvMissing.size(); i++)
                    if (player->udpRecvMissing[i] == missingMsg)
                        player->udpRecvMissing.remove(i);
            }
            else
            {
                // We already processed this packet, we should discard it
                win.logMessage("UDP: Discarding double message (-"+QString().setNum(player->udpRecvSequenceNumbers[channel]-seq)
                               +") from "+QString().setNum(player->pony.netviewId), udpTag);
#if DEBUG_LOG
                win.logMessage("UDP: Message was : "+QString(player->receivedDatas->left(msgSize).toHex().data()), udpTag);
#endif
                player->nReceivedDups++;
                if (player->nReceivedDups >= 100) // Kick the player if he's infinite-looping on us
                {
                    win.logMessage(QString("UDP: Kicking "+QString().setNum(player->pony.netviewId)+": Too many packet dupes."), udpTag);
                    sendMessage(player,MsgDisconnect, "You were kicked. Reason: Too many packet dupes");
                    Player::disconnectPlayerCleanup(player); // Save game and remove the player
                    return;
                }
                *(player->receivedDatas) = player->receivedDatas->mid(msgSize);

                // Ack if needed, so that the client knows to move on already.
                if ((unsigned char)msg[0] >= MsgUserReliableOrdered1 && (unsigned char)msg[0] <= MsgUserReliableOrdered32) // UserReliableOrdered
                {
                    //win.logMessage("UDP: ACKing discarded message");
                    QByteArray data(3,0);
                    data[0] = (quint8)(msg[0]); // ack type
                    data[1] = (quint8)(((quint8)msg[1])/2); // seq
                    data[2] = (quint8)(((quint8)msg[2])/2); // seq
                    sendMessage(player, MsgAcknowledge, data);
                }
                if (player->receivedDatas->size())
                    receiveMessage(player);
                return; // When we're done with the recursion, we still need to skip this message.
            }
        }
        else if (seq > player->udpRecvSequenceNumbers[channel]+2) // If a message was skipped, keep going.
        {
            win.logMessage("UDP: Unordered message (+"+QString().setNum(seq-player->udpRecvSequenceNumbers[channel])
                           +") received from "+QString().setNum(player->pony.netviewId), udpTag);
            player->udpRecvSequenceNumbers[channel] = seq;

            // Mark the packets we skipped as missing
            for (int i=player->udpRecvSequenceNumbers[channel]+2; i<seq; i+=2)
            {
                MessageHead missing;
                missing.channel = channel;
                missing.seq = i;
                player->udpRecvMissing.append(missing);
            }
        }
        else
        {
            if (player->nReceivedDups > 0) // If he stopped sending dupes, forgive him slowly.
                player->nReceivedDups--;
            //win.logMessage("UDP: Received message (="+QString().setNum(seq)
            //               +") from "+QString().setNum(player->pony.netviewId));
            player->udpRecvSequenceNumbers[channel] = seq;
        }
    }

    // Process the received message
    if ((unsigned char)msg[0] == MsgPing) // Ping
    {
        //win.logMessage("UDP: Ping received from "+player->IP+":"+QString().setNum(player->port)
        //        +" ("+QString().setNum((timestampNow() - player->lastPingTime))+"s)");
        player->lastPingNumber = (quint8)msg[5];
        player->lastPingTime = timestampNow();
        sendMessage(player,MsgPong);
    }
    else if ((unsigned char)msg[0] == MsgPong) // Pong
    {
        win.logMessage("UDP: Unexpected pong received!", udpTag);
    }
    else if ((unsigned char)msg[0] == MsgConnect) // Connect SYN
    {
        msg.resize(18); // Supprime le message LocalHail et le Timestamp
        msg = msg.right(13); // Supprime le Header
#if DEBUG_LOG
        win.logMessage(QString("UDP: Connecting..."));
#endif
        for (int i=0; i<32; i++) // Reset sequence counters
            player->udpSequenceNumbers[i]=0;

        if (!player->connected)
            sendMessage(player, MsgConnectResponse, msg);
    }
    else if ((unsigned char)msg[0] == MsgConnectionEstablished) // Connect ACK
    {
        win.logMessage("UDP: Connected to client", udpTag);
        player->connected=true;
        for (int i=0; i<32; i++) // Reset sequence counters
            player->udpSequenceNumbers[i]=0;
        onConnectAckReceived(player); // Clean the reliable message queue from SYN|ACKs

        // Start game
#if DEBUG_LOG
        win.logMessage(QString("UDP: Starting game"), udpTag);
#endif
        // Set player id
        win.lastIdMutex.lock();
        player->pony.id = win.getNewId();
        player->pony.netviewId = win.getNewNetviewId();
        win.lastIdMutex.unlock();
        win.logMessage("UDP: Set id request: " + QString().setNum(player->pony.id) + "/" + QString().setNum(player->pony.netviewId), udpTag);
        QByteArray id(3,0); // Set player Id request
        id[0]=4;
        id[1]=(quint8)(player->pony.id&0xFF);
        id[2]=(quint8)((player->pony.id>>8)&0xFF);
        sendMessage(player,MsgUserReliableOrdered6,id);

        // Load characters screen request
        QByteArray data(1,5);
        data += stringToData("characters");
        sendMessage(player,MsgUserReliableOrdered6,data);
    }
    else if ((unsigned char)msg[0] == MsgAcknowledge) // Acknowledge
    {
        onAckReceived(msg, player);
    }
    else if ((unsigned char)msg[0] == MsgDisconnect) // Disconnect
    {
        win.logMessage("UDP: Client disconnected", udpTag);
        Player::disconnectPlayerCleanup(player); // Save game and remove the player
        return; // We can't use Player& player anymore, it refers to freed memory.
    }
    else if ((unsigned char)msg[0] >= MsgUserReliableOrdered1 && (unsigned char)msg[0] <= MsgUserReliableOrdered32) // UserReliableOrdered
    {
        //win.logMessage("UDP: Data received (hex) : \n"+player->receivedDatas->toHex().constData());

        QByteArray data(3,0);
        data[0] = (quint8)msg[0]; // ack type
        data[1] = (quint8)(((quint8)msg[1])/2); // seq
        data[2] = (quint8)(((quint8)msg[2])/2); // seq
        sendMessage(player, MsgAcknowledge, data);

        if ((unsigned char)msg[0]==MsgUserReliableOrdered6 && (unsigned char)msg[3]==8 && (unsigned char)msg[4]==0 && (unsigned char)msg[5]==6 ) // Prefab (player/mobs) list instantiate request
        {
            sendEntitiesList(player); // Called when the level was loaded on the client side
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered6 && (unsigned char)msg[3]==0x18 && (unsigned char)msg[4]==0 && (unsigned char)msg[5]==8 ) // Player game info (inv/ponyData/...) request
        {
            sendPonySave(player, msg); // Called when instantiate finished on the client side
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered4 && (unsigned char)msg[5]==0xf) // Chat
        {
            receiveChatMessage(msg, player);
        }
        else if ((quint8)msg[0]==MsgUserReliableOrdered4 && (quint8)msg[5]==0x1 && player->inGame!=0) // Edit ponies request error (happens if you click play twice quicly, for example)
        {
            win.logMessage("UDP: Rejecting game start request from "+QString().setNum(player->pony.netviewId)
                           +": player already in game", udpTag);
            // Fix the buggy state we're now in
            // Reload to hide the "saving ponies" message box
            QByteArray data(1,5);
            data += stringToData(player->pony.sceneName);
            sendMessage(player,MsgUserReliableOrdered6,data);
            // Try to cancel the loading callbacks with inGame=1
            player->inGame = 1;
        }
        else if ((quint8)msg[0]==MsgUserReliableOrdered4 && (quint8)msg[5]==0x1 && player->inGame==0) // Edit ponies request
        {
            QList<Pony> ponies = Player::loadPonies(player);
            QByteArray ponyData = msg.right(msg.size()-10);
            Pony pony{player};
            if ((unsigned char)msg[6]==0xff && (unsigned char)msg[7]==0xff && (unsigned char)msg[8]==0xff && (unsigned char)msg[9]==0xff)
            {
                // Create the new pony for this player
                pony.ponyData = ponyData;
                pony.name = dataToString(ponyData);
                pony.sceneName = "ponyville";
                pony.pos = findVortex(pony.sceneName, 0).destPos;
                ponies += pony;
            }
            else
            {
                quint32 id = (quint8)msg[6] +((quint8)msg[7]<<8) + ((quint8)msg[8]<<16) + ((quint8)msg[9]<<24);
                if (ponies.size()<0 || (quint32)ponies.size() <= id)
                {
                    win.logMessage("UDP: Received invalid id in 'edit ponies' request. Disconnecting user.", udpTag);
                    sendMessage(player,MsgDisconnect, "You were kicked for sending invalid data.");
                    Player::disconnectPlayerCleanup(player); // Save game and remove the player
                    return; // It's ok, since we just disconnected the player
                }
                ponies[id].ponyData = ponyData;
                pony = ponies[id];
            }
            pony.id = player->pony.id;
            pony.netviewId = player->pony.netviewId;
            player->pony = pony;

            Player::savePonies(player, ponies);

            player->pony.loadQuests();
            if (!player->pony.loadInventory()) // Create a default inventory if we can't find one saved
            {
                InventoryItem raincloudHat{0,73};
                InventoryItem goggles{1,17};
                InventoryItem hat{2,62};
                InventoryItem bag{3,60};
                InventoryItem thbbbpt{4,65};
                InventoryItem fluffleHat{5,66};
                player->pony.inv << raincloudHat << goggles << hat << bag << thbbbpt << fluffleHat;
                player->pony.nBits = 15;
                player->pony.saveInventory();
            }

            sendLoadSceneRPC(player, player->pony.sceneName, player->pony.pos);
            // Send instantiate to the players of the new scene
            Scene* scene = findScene(player->pony.sceneName);
            for (int i=0; i<scene->players.size(); i++)
                if (scene->players[i]->pony.netviewId!=player->pony.netviewId && scene->players[i]->inGame>=2)
                    sendNetviewInstantiate(&player->pony, scene->players[i]);

            //Send the 46s init messages
            //win.logMessage(QString("UDP: Sending the 46 init messages"));
            sendMessage(player,MsgUserReliableOrdered4,QByteArray::fromHex("141500000000")); // Sends a 46, init friends
            sendMessage(player,MsgUserReliableOrdered4,QByteArray::fromHex("0e00000000")); // Sends a 46, init journal
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered20 && (unsigned char)msg[3]==0x18 && (unsigned char)msg[4]==0) // Vortex messages
        {
            if (player->inGame>=2)
            {
                quint8 id = (quint8)msg[5];
                Vortex vortex = findVortex(player->pony.sceneName, id);
                if (vortex.destName.isEmpty())
                    win.logMessage("Can't find vortex "+QString().setNum(id)+" on map "+player->pony.sceneName, udpTag);
                else
                    sendLoadSceneRPC(player, vortex.destName, vortex.destPos);
            }
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered4 && (unsigned char)msg[5]==0x2) // Delete pony request
        {
            win.logMessage(QString("UDP: Deleting a character"), udpTag);
            QList<Pony> ponies = Player::loadPonies(player);
            quint32 id = (quint8)msg[6] +((quint8)msg[7]<<8) + ((quint8)msg[8]<<16) + ((quint8)msg[9]<<24);
            ponies.removeAt(id);

            Player::savePonies(player,ponies);
            sendPonies(player);
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered12 && (unsigned char)msg[7]==0xCA) // Animation
        {
            //win.logMessage("UDP: Broadcasting animation");
            // Send to everyone
            Scene* scene = findScene(player->pony.sceneName);
            if (scene->name.isEmpty())
                win.logMessage("UDP: Can't find the scene for animation message, aborting", udpTag);
            else
            {
                if (player->lastValidReceivedAnimation.isEmpty() ||
                    (quint8)player->lastValidReceivedAnimation[3] != (quint8)0x01 || (quint8)msg[5 + 3] == 0x00)
                {
                    // Don't accept invalid animation (0x01 Flying 0x00 Landing)
                    // XXX The game lets players send nonsense (dancing while sitting down), those should be filtered
                    player->lastValidReceivedAnimation = msg.mid(5, msgSize - 5);
                    for (int i=0; i<scene->players.size(); i++)
                    {
                        if (scene->players[i] == player)
                            continue; // Don't send the animation to ourselves, it'll be played regardless
                        else if (scene->players[i]->inGame>=2)
                            sendMessage(scene->players[i], MsgUserReliableOrdered12, player->lastValidReceivedAnimation); // Broadcast
                    }
                }
            }
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered11 && (unsigned char)msg[7]==0x3D) // Skill
        {
            QByteArray reply;
            if (dataToUint32(msg.mid(8)) == 2) // Teleport is a special case
            {
                if (msgSize == 28)
                {
                    reply += msg.mid(5, 7); // Netview, RPC, skill IDs
                    reply += msg.mid(16, 4*3); // Pos X, Y, Z (floats)
                    reply += uint32ToData(0); // Skill upgrade (0)
                    reply += floatToData(timestampNow());
                }
                else if (msgSize == 18)
                {
                    // Targeted teleport. First try to find the target in the udp players
                    quint16 targetNetId = dataToUint16(msg.mid(16));
                    Player* target = Player::findPlayer(win.udpPlayers, targetNetId);
                    Pony* targetPony = nullptr;
                    if (target->pony.netviewId == targetNetId && target->connected)
                        targetPony = &target->pony;
                    else
                    {
                        // The target isn't a player. Check if it's a NPC
                        for (Pony* npc : win.npcs)
                            if (npc->netviewId == targetNetId)
                                targetPony = npc;
                    }

                    if (targetPony != nullptr)
                    {
                        reply += msg.mid(5, 7);
                        reply += floatToData(targetPony->pos.x);
                        reply += floatToData(targetPony->pos.y);
                        reply += floatToData(targetPony->pos.z);
                        reply += uint32ToData(0); // Skill upgrade (0)
                        reply += floatToData(timestampNow());
                    }
                    else
                        win.logMessage("UDP: Teleport target not found", udpTag);
                }
            }
            else
                reply =  msg.mid(5, msgSize - 5);
            //win.logMessage("UDP: Broadcasting skill");
            // Send to everyone
            Scene* scene = findScene(player->pony.sceneName);
            if (scene->name.isEmpty())
                win.logMessage("UDP: Can't find the scene for skill message, aborting", udpTag);
            else
            {
                for (int i=0; i<scene->players.size(); i++)
                    if (scene->players[i]->inGame>=2)
                        sendMessage(scene->players[i], MsgUserReliableOrdered11,reply); // Broadcast
            }
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered11 && (unsigned char)msg[7]==0x08) // Wear request
        {
            quint8 index = msg[9];
            Scene* scene = findScene(player->pony.sceneName);
            if (scene->name.isEmpty())
                win.logMessage("UDP: Can't find the scene for wear message, aborting", udpTag);
            else
            {
                if (player->pony.tryWearItem(index))
                {
                    for (int i=0; i<scene->players.size(); i++)
                        if (scene->players[i]->inGame>=2
                                && scene->players[i]->pony.netviewId != player->pony.netviewId)
                            sendWornRPC(&player->pony, scene->players[i], player->pony.worn);
                }
                else
                    win.logMessage("Error trying to wear item", udpTag);
            }
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered11 && (unsigned char)msg[7]==0x04) // Get worn items request
        {
            quint16 targetId = ((quint16)(quint8)msg[5]) + (((quint16)(quint8)msg[6])<<8);
            Player* target = Player::findPlayer(win.udpPlayers, targetId);
            if (target->pony.netviewId == targetId && target->connected)
                sendWornRPC(&target->pony, player, target->pony.worn);
            else
            {
                Pony* targetNpc = nullptr;
                for (Pony* npc : win.npcs)
                {
                    if (npc->netviewId == targetId)
                    {
                        targetNpc = npc;
                        break;
                    }
                }
                if (targetNpc)
                    sendWornRPC(targetNpc, player, targetNpc->worn);
                else
                    win.logMessage("Can't find netviewId "+QString().setNum(targetId)+" to send worn items", udpTag);
            }
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered11 && (unsigned char)msg[7]==0x09) // Unwear item request
        {
            quint16 targetId = ((quint16)(quint8)msg[5]) + (((quint16)(quint8)msg[6])<<8);
            Player* target = Player::findPlayer(win.udpPlayers, targetId);
            if (target->pony.netviewId == targetId)
                target->pony.unwearItemAt(dataToUint8(msg.mid(8)));
            else
                win.logMessage("Can't find netviewId "+QString().setNum(targetId)+" to unwear item", udpTag);
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered11 && (unsigned char)msg[7]==0x31) // Run script (NPC) request
        {
            quint16 targetId = ((quint16)(quint8)msg[5]) + (((quint16)(quint8)msg[6])<<8);
            //win.logMessage("Quest "+QString().setNum(targetId)+" requested");
            for (int i=0; i<player->pony.quests.size(); i++)
                if (player->pony.quests[i].id == targetId)
                {
                    player->pony.lastQuest = i;
                    player->pony.quests[i].runScript();
                    break;
                }
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered4 && (unsigned char)msg[5]==0xB) // Continue dialog
        {
            //win.logMessage("Resuming script for quest "+QString().setNum(player->pony.lastQuest));
            player->pony.quests[player->pony.lastQuest].processAnswer();
        }
        else if ((unsigned char)msg[0]==MsgUserReliableOrdered4 && (unsigned char)msg[5]==0xC) // Continue dialog (with answer)
        {
            quint32 answer = ((quint32)(quint8)msg[6])
                            + (((quint32)(quint8)msg[7])<<8)
                            + (((quint32)(quint8)msg[8])<<16)
                            + (((quint32)(quint8)msg[9])<<24);
            //win.logMessage("Resuming script with answer "+QString().setNum(answer)
            //               +" for quest "+QString().setNum(player->pony.lastQuest));
            player->pony.quests[player->pony.lastQuest].processAnswer(answer);
        }
        else
        {
            // Display data
            quint32 unknownMsgSize =  (((quint16)(quint8)msg[3]) +(((quint16)(quint8)msg[4])<<8)) / 8;
            win.logMessage("UDP: Unknown message received : "
                           +QString(player->receivedDatas->left(unknownMsgSize+5).toHex().data()), udpTag);
            *player->receivedDatas = player->receivedDatas->mid(unknownMsgSize+5);
            msgSize=0;
        }
    }
    else if ((unsigned char)msg[0]==MsgUserUnreliable) // Sync (position) update
    {
        if (dataToUint16(msg.mid(5)) == player->pony.netviewId)
            Sync::receiveSync(player, msg);
    }
    else
    {
        // Display data
        win.logMessage("Unknown data received (UDP) (hex): ", udpTag);
        win.logMessage(QString(player->receivedDatas->toHex().data()), udpTag);
        quint32 unknownMsgSize = (((quint16)(quint8)msg[3]) +(((quint16)(quint8)msg[4])<<8)) / 8;
        *player->receivedDatas = player->receivedDatas->mid(unknownMsgSize+5);
        msgSize=0;
    }

    *player->receivedDatas = player->receivedDatas->mid(msgSize);
    if (player->receivedDatas->size())
        receiveMessage(player);
}
