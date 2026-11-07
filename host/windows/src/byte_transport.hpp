#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>

namespace dso
{

// Byte-stream boundary owned by Session. Session is the source of truth for
// transport lifecycle; adapters report readiness and terminal loss as events.
// Session calls transport methods synchronously from its own Qt thread, so an
// injected adapter must share that thread for its entire owned lifetime. Since
// Session owns the adapter through std::unique_ptr, injected QObject adapters
// must be parentless.
class ByteTransport : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    // Returns false if the requested endpoint could not be opened.
    virtual bool open(const QString &portName) = 0;
    virtual void close() = 0;
    // Reports whether the adapter currently has an open endpoint.
    virtual bool isOpen() const = 0;

    // Drains bytes currently available; callers must allow replies to arrive
    // in multiple fragments.
    virtual QByteArray readAll() = 0;

    // Returns the number of bytes accepted. Session treats a short frame write
    // as a transport failure, so adapters must not silently drop a suffix.
    virtual qint64 write(const QByteArray &bytes) = 0;
    virtual QString errorString() const
    {
        return QStringLiteral("Transport open failed");
    }

  signals:
    // Indicates that stream bytes may be read; it does not imply a full reply.
    void readyRead();

    // Reports a terminal transport failure that requires the session to close.
    void lost(QString reason);
};

} // namespace dso
