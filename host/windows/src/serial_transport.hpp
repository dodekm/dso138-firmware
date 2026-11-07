#pragma once

#include "byte_transport.hpp"

#include <QList>
#include <QString>
#include <memory>

namespace dso
{

// Constructs the production COM-port adapter owned by Session.
std::unique_ptr<ByteTransport> makeSerialTransport();

struct SerialPortDescriptor final
{
    QString portName;
    QString description;
};

// Keeps Qt SerialPort discovery inside the transport adapter boundary.
QList<SerialPortDescriptor> availableSerialPorts();

} // namespace dso
