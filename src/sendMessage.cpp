#include "sendMessage.h"
#include "character.h"
#include "widget.h"
#include "utils.h"
#include "serialize.h"
void sendMessage(Player* player,quint8 messageType, QByteArray data)
{
    QByteArray msg(3,0);
    // MessageType
    msg[0] = messageType;
    // Sequence
    msg[1] = 0;
    msg[2] = 0;
    if (messageType == MsgPing)
    {
        msg.resize(6);
        // Sequence
        msg[1] = 0;
        msg[2] = 0;
        // Payload size
        msg[3] = 8;
        msg[4] = 0;
        // Ping number
        player->lastPingNumber++;
        msg[5]=(quint8)player->lastPingNumber;
    }
    else if (messageType == MsgPong)
    {
        msg.resize(6);
        // Payload size
        msg[3] = 8*5;
        msg[4] = 0;
        // Ping number
        msg[5]=(quint8)player->lastPingNumber;
        // Timestamp
        msg += floatToData(timestampNow());
    }
    else if (messageType == MsgUserUnreliable)
    {
        msg.resize(5);
        // Sequence
        msg[1] = (quint8)(player->udpSequenceNumbers[32]&0xFF);
        msg[2] = (quint8)((player->udpSequenceNumbers[32]>>8)&0xFF);
        // Data size
        msg[3] = (quint8)((8*(data.size()))&0xFF);
        msg[4] = (quint8)(((8*(data.size())) >> 8)&0xFF);
        // Data
        msg += data;
        player->udpSequenceNumbers[32]++;

        //win.logMessage(QString("Sending sync data :")+msg.toHex());
    }
    else if (messageType >= MsgUserReliableOrdered1 && messageType <= MsgUserReliableOrdered32)
    {
        //win.logMessage("sendMessage locking");
        player->udpSendReliableMutex.lock();
        player->udpSendReliableGroupTimer->stop();
        msg.resize(5);
        // Sequence
        msg[1] = (quint8)(player->udpSequenceNumbers[messageType-MsgUserReliableOrdered1]&0xFF);
        msg[2] = (quint8)((player->udpSequenceNumbers[messageType-MsgUserReliableOrdered1] >> 8)&0xFF);
        // Payload size
        msg[3] = (quint8)((8*(data.size()))&0xFF);
        msg[4] = (quint8)(((8*(data.size())) >> 8)&0xFF);
        // strlen
        msg+=data;
        player->udpSequenceNumbers[messageType-MsgUserReliableOrdered1] += 2;

        if (player->udpSendReliableGroupBuffer.size() + msg.size() > 1024) // Flush the buffer before starting a new grouped msg
            player->udpDelayedSend();
        player->udpSendReliableGroupBuffer.append(msg);

        player->udpSendReliableGroupTimer->start(); // When this timeouts, the content of the buffer will be sent reliably

        //win.logMessage("sendMessage unlocking");
        player->udpSendReliableMutex.unlock();
        return; // This isn't a normal send, but a delayed one with the timer callbacks
    }
    else if (messageType == MsgAcknowledge)
    {
        msg.resize(5);
        // Payload size
        msg[3] = (quint8)((data.size()*8)&0xFF);
        msg[4] = (quint8)(((data.size()*8)>>8)&0xFF);
        msg.append(data); // Format of packet data n*(Ack type, Ack seq, Ack seq)
    }
    else if (messageType == MsgConnectResponse)
    {
        //win.logMessage("sendMessage locking");
        player->udpSendReliableMutex.lock();
        player->udpSendReliableGroupTimer->stop();
        msg.resize(5);
        // Payload size
        msg[3] = 0x88;
        msg[4] = 0x00;

        //win.logMessage("UDP: Header data : "+msg.toHex());

        // AppId + UniqueId
        msg += data;
        msg += floatToData(timestampNow());

        if (player->udpSendReliableGroupBuffer.size() + msg.size() > 1024) // Flush the buffer before starting a new grouped msg
            player->udpDelayedSend();
        player->udpSendReliableGroupBuffer.append(msg);

        player->udpSendReliableGroupTimer->start(); // When this timeouts, the content of the buffer will be sent reliably

        //win.logMessage("sendMessage unlocking");
        player->udpSendReliableMutex.unlock();
        return; // This isn't a normal send, but a delayed one with the timer callbacks
    }
    else if (messageType == MsgDisconnect)
    {
        msg.resize(6);
        // Payload size
        msg[3] = (quint8)(((data.size()+1)*8)&0xFF);
        msg[4] = (quint8)((((data.size()+1)*8)>>8)&0xFF);
        // Message length
        msg[5] = (quint8)data.size();
        // Disconnect message
        msg += data;
    }
    else
    {
        win.logStatusMessage("sendMessage: Unknown message type", udpTag);
        return;
    }

    // Simulate packet loss if enabled (DEBUG ONLY!)
#if UDP_SIMULATE_PACKETLOSS
    if (qrand() % 100 <= UDP_PERCENT_DROPPED)
    {
        win.logMessage("UDP: Packet dropped!", udpTag);
        return;
    }
    else
        win.logMessage("UDP: Packet got throught", udpTag);
#endif

    if (win.udpSocket->writeDatagram(msg,QHostAddress(player->IP),player->port) != msg.size())
    {
        win.logMessage("UDP: Error sending message to "+QString().setNum(player->pony.netviewId)
                       +" : "+win.udpSocket->errorString(), udpTag);
        win.logMessage("Restarting UDP server...", udpTag);
        win.udpSocket->close();
        if (!win.udpSocket->bind(win.gamePort, QUdpSocket::ReuseAddressHint|QUdpSocket::ShareAddress))
        {
            win.logStatusMessage("UDP: Unable to start server on port "+QString().setNum(win.gamePort), udpTag);
            win.stopServer();
            return;
        }
    }
}
