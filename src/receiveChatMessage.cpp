#include "receiveChatMessage.h"
#include "character.h"
#include "serialize.h"
#include "message.h"
#include "widget.h"

void receiveChatMessage(QByteArray msg, Player* player)
{
    QString txt = dataToString(msg.mid(7));
    QString author = player->pony.name;
    win.logMessage("[Chat] "+author+": "+txt);
    //logStatusMessage("[CHAT] "+author+": "+txt);

    if (txt.startsWith("/stuck") || txt.startsWith("unstuck me"))
    {
        sendLoadSceneRPC(player, player->pony.sceneName);
    }
    else if (txt == ":commands")
    {
        sendChatMessage(player, "<span color=\"yellow\">List of Commands:</span><br /><em>:roll</em><br /><span color=\"yellow\">Rolls a random number between 00 and 99</span><br /><em>:msg player message</em><br /><span color=\"yellow\">Sends a private message to a player</span><br /><em>:names</em><br /><span color=\"yellow\">Lists all players on the server</span><br /><em>:me action</em><br /><span color=\"yellow\">States your current action</span><br /><em>:tp location</em><br /><span color=\"yellow\">Teleports your pony to the specified region</span>", "[Server]", ChatLocal);
    }
    else if (txt.startsWith(":msg"))
    {
        if(txt.count(" ") < 2)
            sendChatMessage(player, ":msg<br /><span color=\"yellow\">Usage:</span><br /><em>:msg player message</em><br /><span color=\"yellow\">Player names are case-insensitive, ignore spaces and you do not need to type out their full name.</span>", author, ChatLocal);
        else
        {
            for (int i=0; i<win.udpPlayers.size(); i++)
            {
                if (win.udpPlayers[i]->inGame>=2 && win.udpPlayers[i]->pony.name.toLower().remove(" ").startsWith(txt.toLower().section(" ", 1, 1)))
                {
                    txt = txt.remove(0, txt.indexOf(" ", 5) + 1);
                    sendChatMessage(win.udpPlayers[i], "<span color=\"yellow\">[PM] </span>" + txt, author, ChatLocal);
                    sendChatMessage(player, "<span color=\"yellow\">[PM to " + win.udpPlayers[i]->pony.name + "] </span>" + txt, author, ChatLocal);
                }
            }
        }
    }
    else if (txt.startsWith(":names"))
    {
        QString namesmsg = "<span color=\"yellow\">Players currently in game:</span>";

        for (int i=0; i<win.udpPlayers.size(); i++)
            if (win.udpPlayers[i]->inGame>=2)
                namesmsg += "<br />#b" + win.udpPlayers[i]->pony.name + "#b<br /><span color=\"yellow\"> - in " + win.udpPlayers[i]->pony.sceneName + "</span>";

        sendChatMessage(player, namesmsg, "[Server]", ChatLocal);
    }
    else if (txt.startsWith(":tp"))
    {
        if (txt.count(" ") < 1)
        {
          QString msgtosend = ":tp<br /><span color=\"yellow\">Usage:</span><br /><em>:tp location</em><br /><span color=\"yellow\">Available locations:</span><em>";

            for (int i=0; i<win.scenes.size(); i++)
                msgtosend += "<br />" + win.scenes[i].name;

            sendChatMessage(player, msgtosend + "</em>", author, ChatLocal);
        }

        else
            sendLoadSceneRPC(player, txt.remove(0, 4));
    }
    else if (txt == ":me")
    {
        sendChatMessage(player, ":me<br /><span color=\"yellow\">Usage:</span><br /><em>:me action</em>", author, ChatLocal);
    }
    else // Broadcast the message
    {
        int rollnum = -1;
        QString rollstr;
        bool actmsg = false;

        if (txt == ":roll")
        {
            if (player->chatRollCooldownEnd < QDateTime::currentDateTime())
            {
                rollnum = qrand() % 100;
                rollstr.sprintf("<span color=\"yellow\">#b%s#b %02d</span>", author.toLocal8Bit().data(), rollnum);
                player->chatRollCooldownEnd = QDateTime::currentDateTime().addSecs(10);
            }
        }
        if (txt.startsWith(":me "))
        {
            actmsg = true;
            txt.remove(0, 3);
            txt = "<em>#b* " + author + "#b" + txt + "</em>";
        }
        if ((quint8)msg[6] == 8) // Local chat only
        {
            Scene* scene = findScene(player->pony.sceneName);
            if (scene->name.isEmpty())
                win.logMessage("UDP: Can't find the scene for chat message, aborting");
            else
            {
                for (int i=0; i<scene->players.size(); i++)
                {
                    if (scene->players[i]->inGame>=2)
                    {
                        if (rollnum > -1)
                            sendChatMessage(scene->players[i], rollstr, "[Server]", ChatLocal);
                        else if (actmsg)
                            sendChatMessage(scene->players[i], txt, "", ChatLocal);
                        else if (txt.startsWith(">"))
                            sendChatMessage(scene->players[i], "<span color=\"green\">" + txt + "</span>", author, ChatLocal);
                        else
                            sendChatMessage(scene->players[i], txt, author, ChatLocal);
                    }
                }
            }
        }
        else // Send globally
        {
            for (int i=0; i<win.udpPlayers.size(); i++)
            {
                if (win.udpPlayers[i]->inGame>=2)
                {
                    if (rollnum > -1)
                        sendChatMessage(win.udpPlayers[i], rollstr, "[Server]", ChatGeneral);
                    else if (actmsg)
                        sendChatMessage(win.udpPlayers[i], txt, "", ChatGeneral);
                    else if (txt.startsWith(">"))
                        sendChatMessage(win.udpPlayers[i], "<span color=\"green\">" + txt + "</span>", author, ChatGeneral);
                    else
                        sendChatMessage(win.udpPlayers[i], txt, author, ChatGeneral);

                }
            }
        }
    }
}
