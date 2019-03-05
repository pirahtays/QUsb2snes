#include <QEventLoop>
#include <QLoggingCategory>

#include "retroarchdevice.h"

Q_LOGGING_CATEGORY(log_retroarch, "RETROARCH")
#define sDebug() qCDebug(log_retroarch)

RetroArchDevice::RetroArchDevice(QUdpSocket* sock)
{
    m_sock = sock;
    m_state = READY;
    sDebug() << "Retroarch device created";
    m_timer = new QTimer();
    m_timer->setInterval(5);
    m_timer->setSingleShot(true);
    dataRead = QByteArray();
    connect(m_timer, SIGNAL(timeout()), this, SLOT(timedCommandDone()));
    connect(m_sock, SIGNAL(readyRead()), this, SLOT(onUdpReadyRead()));
    bigGet = false;
    checkingRetroarch = false;
}


QString RetroArchDevice::name() const
{
    return "EMU RetroArch";
}


USB2SnesInfo RetroArchDevice::parseInfo(const QByteArray &data)
{
    Q_UNUSED(data);
    USB2SnesInfo    info;
    info.romPlaying = "not available";
    info.version = "1.0.0";
    info.flags << "NO_ROM_ACCESS";
    return info;
}

QList<ADevice::FileInfos> RetroArchDevice::parseLSCommand(QByteArray &dataI)
{
    return QList<ADevice::FileInfos>();
}

bool RetroArchDevice::open()
{
    return m_state != CLOSED;
}

void RetroArchDevice::close()
{
    m_state = CLOSED;
    m_sock->close();
}

// READ_CORE_RAM 200 20
// READ_CORE_RAM 200 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

void RetroArchDevice::onUdpReadyRead()
{
    QByteArray data = m_sock->readAll();
    sDebug() << "<<" << data;
    if (data.isEmpty())
    {
        emit closed();
        return ;
    }
    QList<QByteArray> tList = data.trimmed().split(' ');
    sDebug() << tList;
    if (tList.at(2) != "-1")
    {
        tList = tList.mid(2);
        data = tList.join();
        sDebug() << "Sending : " << QByteArray::fromHex(data).toHex();
        emit getDataReceived(QByteArray::fromHex(data));
    } else {
        sDebug() << "Not giving data : sending" << lastRCRSize << "bytes";
        emit getDataReceived(QByteArray(lastRCRSize, 0));
    }
    if (bigGet)
    {
        if (sizeRequested == sizeBigGet)
        {
            bigGet = false;
            sizeBigGet = 0;
            sizeRequested = 0;
            addrBigGet = 0;
            m_state  = READY;
            emit commandFinished();
        } else {
            unsigned int mSize = 0;
            if (sizeRequested + 78 <= sizeBigGet)
                mSize = 78;
            else
                mSize = sizeBigGet - sizeRequested;
            sizeRequested += mSize;
            addrBigGet += mSize;
            read_core_ram(addrBigGet, mSize);
         }
         return;
    }
    emit commandFinished();
    m_state = READY;
}

void    RetroArchDevice::read_core_ram(unsigned int addr, unsigned int size)
{
    QByteArray data = "READ_CORE_RAM " + QByteArray::number(addr, 16) + " " + QByteArray::number(size);
    sDebug() << ">>" << data;
    lastRCRSize = size;
    m_sock->write(data);
}

void RetroArchDevice::timedCommandDone()
{
    sDebug() << "Fake cmd finished";
    m_state = READY;
    emit commandFinished();
}

bool RetroArchDevice::hasFileCommands()
{
    return false;
}

bool RetroArchDevice::hasControlCommands()
{
    return false;
}

void RetroArchDevice::fileCommand(SD2Snes::opcode op, QVector<QByteArray> args)
{

}

void RetroArchDevice::fileCommand(SD2Snes::opcode op, QByteArray args)
{

}

void RetroArchDevice::controlCommand(SD2Snes::opcode op, QByteArray args)
{

}

void RetroArchDevice::putFile(QByteArray name, unsigned int size)
{

}

static int addr_to_addr(int addr)
{
    if (addr >= 0xF50000 && addr <= 0xF70000)
        addr -= 0xF50000;
    else {
        if (addr >= 0xE00000)
            addr = addr - 0xE00000 + 0x20000;
        else
            return -1;
    }
    return addr;
}

void RetroArchDevice::getAddrCommand(SD2Snes::space space, unsigned int addr, unsigned int size)
{
    sDebug() << "GetAddress " << space << addr << size;
    m_state = BUSY;
    if (space != SD2Snes::SNES)
        return;
    addr = addr_to_addr(addr);
    if (addr == -1)
    {
        emit getDataReceived(QByteArray(size, 0));
        m_timer->start();
        return ;
    }
    if (size > 78)
    {
        bigGet = true;
        sizeBigGet = size;
        sizeRequested = 78;
        size = 78;
        addrBigGet = addr;
    }
    read_core_ram(addr, size);
}

void RetroArchDevice::getAddrCommand(SD2Snes::space space, QList<QPair<unsigned int, quint8> > &args)
{

}

void RetroArchDevice::putAddrCommand(SD2Snes::space space, unsigned int addr0, unsigned int size)
{
    int addr = addr0;
    m_state = BUSY;
    if (space != SD2Snes::space::SNES)
        return;
    addr = addr_to_addr(addr);
    if (addr == -1)
        return ;
    sDebug() << "WRITING TO RAM/SRAM" << addr;
    dataToWrite = "WRITE_CORE_RAM " + QByteArray::number(addr, 16) + " ";
}

void RetroArchDevice::putAddrCommand(SD2Snes::space space, QList<QPair<unsigned int, quint8> > &args)
{

}

void RetroArchDevice::putAddrCommand(SD2Snes::space space, unsigned char flags, unsigned int addr, unsigned int size)
{
    putAddrCommand(space, addr, size);
}

void RetroArchDevice::sendCommand(SD2Snes::opcode opcode, SD2Snes::space space, unsigned char flags, const QByteArray &arg, const QByteArray arg2)
{

}

void RetroArchDevice::infoCommand()
{
    sDebug() << "Info command";
    m_timer->start();
}

void RetroArchDevice::writeData(QByteArray data)
{
    dataToWrite.append(data.toHex(' '));
    sDebug() << "<<" << dataToWrite;
    m_sock->write(dataToWrite);
    m_timer->start();
}
