#include "items.h"
#include "dataType.h"
#include "widget.h"
#include <QString>
#include <QStringList>
#include <cmath>

QMap<uint32_t, uint32_t> parseItemsXml(QByteArray data)
{
    QMap<uint32_t, uint32_t> map;

    while (1)
    {
        int idIndex = data.indexOf("<ID>")+4;
        int slotsIndex = data.indexOf("<WearableSlots>")+15;
        if (idIndex == -1 || slotsIndex == -1)
            break;
        int idIndexEnd = data.indexOf("</ID>", idIndex);
        int slotsIndexEnd = data.indexOf("</WearableSlots>", slotsIndex);
        if (idIndexEnd == -1 || slotsIndexEnd == -1)
            break;

        QString idStr = data.mid(idIndex, idIndexEnd - idIndex);
        QString slotsStr = data.mid(slotsIndex, slotsIndexEnd - slotsIndex);

        //win.logMessage("Found id "+idStr+", slots : "+slotsStr);

        bool ok;
        int id = idStr.toInt(&ok);
        if (!ok)
            break;

        QStringList slotsList = slotsStr.split(' ');
        if (slotsList.isEmpty())
            break;

        uint32_t itemSlots=0;
        for (QString slot : slotsList)
        {
            if (slot == "None")
            {
                itemSlots = 0;
                break;
            }
            else if (slot == "Tail")    itemSlots |= (uint32_t)WearablePositions::Tail;
            else if (slot == "Pants")   itemSlots |= (uint32_t)WearablePositions::Pants;
            else if (slot == "FrontSocks")   itemSlots |= (uint32_t)WearablePositions::FrontSocks;
            else if (slot == "BackSocks")   itemSlots |= (uint32_t)WearablePositions::BackSocks;
            else if (slot == "FrontShoes")   itemSlots |= (uint32_t)WearablePositions::FrontShoes;
            else if (slot == "BackShoes")   itemSlots |= (uint32_t)WearablePositions::BackShoes;
            else if (slot == "Saddle")   itemSlots |= (uint32_t)WearablePositions::Saddle;
            else if (slot == "Shirt")   itemSlots |= (uint32_t)WearablePositions::Shirt;
            else if (slot == "Necklace")   itemSlots |= (uint32_t)WearablePositions::Necklace;
            else if (slot == "Mouth")   itemSlots |= (uint32_t)WearablePositions::Mouth;
            else if (slot == "Mask")   itemSlots |= (uint32_t)WearablePositions::Mask;
            else if (slot == "Eyes")   itemSlots |= (uint32_t)WearablePositions::Eyes;
            else if (slot == "Ears")   itemSlots |= (uint32_t)WearablePositions::Ears;
            else if (slot == "FrontKnees")   itemSlots |= (uint32_t)WearablePositions::FrontKnees;
            else if (slot == "BackKnees")   itemSlots |= (uint32_t)WearablePositions::BackKnees;
            else if (slot == "SaddleBags")   itemSlots |= (uint32_t)WearablePositions::SaddleBags;
            else if (slot == "Hat")   itemSlots |= (uint32_t)WearablePositions::Hat;
            else
            {
                win.logMessage("Unknown wearable slots while parsing Items.xml", sysTag);
            }
        }
        map[id] = itemSlots;
        data = data.mid(slotsIndexEnd);
    }

    return map;
}

uint8_t wearablePositionsToSlot(uint32_t positions)
{
    // We don't have base 2 log, so we'll do it the hard way

    if (positions & (uint32_t)WearablePositions::Hat)              return 32;
    else if (positions & (uint32_t)WearablePositions::SaddleBags)  return 31;
    else if (positions & (uint32_t)WearablePositions::BackKnees)   return 15;
    else if (positions & (uint32_t)WearablePositions::FrontKnees)  return 14;
    else if (positions & (uint32_t)WearablePositions::Ears)        return 13;
    else if (positions & (uint32_t)WearablePositions::Eyes)        return 12;
    else if (positions & (uint32_t)WearablePositions::Mask)        return 11;
    else if (positions & (uint32_t)WearablePositions::Mouth)       return 10;
    else if (positions & (uint32_t)WearablePositions::Necklace)    return 9;
    else if (positions & (uint32_t)WearablePositions::Shirt)       return 8;
    else if (positions & (uint32_t)WearablePositions::Saddle)      return 7;
    else if (positions & (uint32_t)WearablePositions::BackShoes)   return 6;
    else if (positions & (uint32_t)WearablePositions::FrontShoes)  return 5;
    else if (positions & (uint32_t)WearablePositions::BackSocks)   return 4;
    else if (positions & (uint32_t)WearablePositions::FrontSocks)  return 3;
    else if (positions & (uint32_t)WearablePositions::Pants)       return 2;
    else if (positions & (uint32_t)WearablePositions::Tail)        return 1;
    else return 0;
}

uint32_t slotToWearablePositions(uint8_t slot)
{
    if (slot == 0)
        return (uint32_t)WearablePositions::None;
    else if (slot >= 0x20)
        return -1;
    else
        return (slot-1)*(slot-1);
}
