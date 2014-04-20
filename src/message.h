#ifndef MESSAGE_H
#define MESSAGE_H

#include <QPair>
#include <QMutex>
#include <QByteArray>
#include <QStringList>
#include "dataType.h"

enum MessageTypes {
    MsgUnconnected = 0,
    MsgUserUnreliable = 1,
    MsgPing = 0x81,
    MsgPong = 0x82,
    MsgConnect = 0x83,
    MsgConnectResponse = 0x84,
    MsgConnectionEstablished = 0x85,
    MsgAcknowledge = 0x86,
    MsgDisconnect = 0x87,
    MsgUserReliableOrdered1 = 0x43,
    MsgUserReliableOrdered2 = 0x44,
    MsgUserReliableOrdered3 = 0x45,
    MsgUserReliableOrdered4 = 0x46,
    MsgUserReliableOrdered5 = 0x47,
    MsgUserReliableOrdered6 = 0x48,
    MsgUserReliableOrdered11 = 0x4d,
    MsgUserReliableOrdered12 = 0x4e,
    MsgUserReliableOrdered18 = 0x54,
    MsgUserReliableOrdered20 = 0x56,
    MsgUserReliableOrdered32 = 0x62
};

enum ChatType
{
    ChatNone = 0,
    ChatUntyped = 1,
    // NOTE system doesn't take author as an argument
    ChatSystem = 2,
    ChatGeneral = 4,
    ChatLocal = 8,
    ChatParty = 16,
    ChatGuild = 32,
    ChatWhisper = 64
};

// Public functions
class Player;
class Pony;
void receiveMessage(Player* player);
void sendMessage(Player* player, quint8 messageType, QByteArray data=QByteArray());
void sendEntitiesList(Player* player);
void sendPonySave(Player* player, QByteArray msg);
void sendPonies(Player* player);
void sendPonyData(Player* player);
void sendPonyData(Pony *src, Player* dst);
void sendNetviewInstantiate(Player* player, QString key, quint16 NetviewId, quint16 ViewId, UVector pos, UQuaternion rot);
void sendNetviewInstantiate(Player* player);
void sendNetviewInstantiate(Pony *src, Player* dst);
void sendNetviewRemove(Player* player, quint16 netviewId);
void sendSetStatRPC(Player* player, quint8 statId, float value);
void sendSetMaxStatRPC(Player* player, quint8 statId, float value);
void sendSetStatRPC(Player* affected, Player* dest, quint8 statId, float value); // Set stat of affected on client dest
void sendSetMaxStatRPC(Player* affected, Player* dest, quint8 statId, float value); // Set stat of affected on client dest
void sendWornRPC(Player* player, QList<WearableItem>& worn); // Send items worn by player to himself
void sendWornRPC(Player* player); // Send items worn by player to himself
void sendWornRPC(Pony *wearing, Player* dest, QList<WearableItem>& worn); // Send items worn by wearing to dest
void sendWearItemRPC(Pony* wearing, Player* dest, const WearableItem& item); // Wear this item
void sendWearItemRPC(Player* player, const WearableItem& item); // Wear this item
void sendUnwearItemRPC(Player* player, uint8_t slot); // Unwear this item
void sendUnwearItemRPC(Pony* wearing, Player* dest, uint8_t slot); // Unwear this item
void sendInventoryRPC(Player* player, QList<InventoryItem>& inv, QList<WearableItem>& worn, quint32 nBits);
void sendInventoryRPC(Player* player);
void sendSetBitsRPC(Player* player); // Resend how many bits we have to the clients
void sendSkillsRPC(Player* player, QList<QPair<quint32, quint32> >& skills);
void sendLoadSceneRPC(Player* player, QString sceneName);
void sendLoadSceneRPC(Player* player, QString sceneName, UVector pos);
void sendChatMessage(Player* player, QString message, QString author, quint8 chatType);
void sendMove(Player* player, float x, float y, float z);
void sendBeginDialog(Player* player);
void sendDialogMessage(Player* player, QString& message, QString NPCName, quint16 iconId=0);
void sendDialogOptions(Player* player, QList<QString> &answers);
void sendEndDialog(Player* player);
void sendAddItemRPC(Player* player, const InventoryItem& item);
void sendDeleteItemRPC(Player* player, uint8_t index, uint32_t qty);

#endif // MESSAGE_H
