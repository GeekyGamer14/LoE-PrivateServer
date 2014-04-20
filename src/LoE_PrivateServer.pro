#-------------------------------------------------
#
# Project created by QtCreator 2013-06-21T21:58:54
#
#-------------------------------------------------

QT       += core gui network widgets

TARGET = LoE_PrivateServer
TEMPLATE = app

SOURCES += main.cpp \
        widget.cpp \
	tcp.cpp \
	udp.cpp \
	messages.cpp \
	utils.cpp \
	pingTimeout.cpp \
	character.cpp \
	scene.cpp \
	dataType.cpp \
	sync.cpp \
    receiveMessage.cpp \
    sendMessage.cpp \
    serverCommands.cpp \
    quest.cpp \
    serialize.cpp \
    items.cpp \
    receiveAck.cpp \
    receiveChatMessage.cpp

HEADERS  += widget.h \
	character.h \
	message.h \
	utils.h \
	scene.h \
	dataType.h \
	sync.h \
    quest.h \
    serialize.h \
    items.h \
    sendMessage.h \
    receiveAck.h \
    receiveChatMessage.h

FORMS    += widget.ui

# include coreservices (required for timestamps) only on mac
macx {
    QMAKE_LFLAGS += -F /System/Library/Frameworks/CoreServices.framework/
    LIBS += -framework CoreServices
}

CONFIG += C++11

OTHER_FILES += \
    128.png
