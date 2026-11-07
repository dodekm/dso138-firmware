#include "serial_transport.hpp"

#include <QSerialPort>
#include <QSerialPortInfo>

namespace dso
{
namespace
{
class SerialTransport final : public ByteTransport
{
  public:
    explicit SerialTransport(QObject *parent = nullptr) : ByteTransport(parent)
    {
        port_.setReadBufferSize(4096);
        connect(&port_, &QSerialPort::readyRead, this, [this] { emit readyRead(); });
        connect(&port_, &QSerialPort::errorOccurred, this,
                [this](QSerialPort::SerialPortError error) {
                    if (port_.isOpen() && error != QSerialPort::NoError &&
                        error != QSerialPort::TimeoutError)
                    {
                        emit lost(port_.errorString());
                    }
                });
    }

    bool open(const QString &portName) override
    {
        if (port_.isOpen())
        {
            port_.close();
        }
        port_.setPortName(portName);
        port_.setBaudRate(QSerialPort::Baud115200);
        const bool opened = port_.open(QIODevice::ReadWrite);
        return opened;
    }

    void close() override
    {
        if (port_.isOpen())
        {
            port_.close();
        }
    }

    bool isOpen() const override
    {
        return port_.isOpen();
    }

    QByteArray readAll() override
    {
        return port_.readAll();
    }

    qint64 write(const QByteArray &bytes) override
    {
        if (!port_.isOpen())
        {
            emit lost(QStringLiteral("Serial port is closed"));
            return -1;
        }
        const qint64 accepted = port_.write(bytes);
        if (accepted != bytes.size())
        {
            emit lost(
                port_.errorString().isEmpty()
                    ? QStringLiteral("Serial write did not accept the complete protocol frame")
                    : port_.errorString());
            return -1;
        }
        return accepted;
    }

    QString errorString() const override
    {
        return port_.errorString();
    }

  private:
    QSerialPort port_;
};
} // namespace

std::unique_ptr<ByteTransport> makeSerialTransport()
{
    return std::make_unique<SerialTransport>();
}

QList<SerialPortDescriptor> availableSerialPorts()
{
    QList<SerialPortDescriptor> result;
    const auto ports = QSerialPortInfo::availablePorts();
    result.reserve(ports.size());
    for (const auto &port : ports)
    {
        result.append({port.portName(), port.description()});
    }
    return result;
}

} // namespace dso
