#include "session.hpp"
#include "serial_transport.hpp"

namespace dso
{

Session::Session(QObject *parent) : Session(makeSerialTransport(), parent)
{
}

} // namespace dso
