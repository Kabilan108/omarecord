/** @fileoverview Newline-delimited JSON over a Unix socket the overlay
 * listens on; the orchestrator connects, one client at a time. */
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <optional>

/** Parses one inbound line; nullopt when it is not a JSON object. */
[[nodiscard]] std::optional<QJsonObject> parseCommandLine(const QByteArray &line);
/** Formats `{"event":NAME}` with a trailing newline. */
[[nodiscard]] QByteArray formatEventLine(const QString &event);

class QLocalSocket;

class OverlayProtocol final : public QObject {
  Q_OBJECT
public:
  explicit OverlayProtocol(QObject *parent = nullptr);

  /** Removes any stale socket file and starts listening. */
  bool listen(const QString &path, QString &error);
  [[nodiscard]] bool hasClient() const { return client_ != nullptr; }
  void sendEvent(const QString &event);

signals:
  void commandReceived(const QJsonObject &command);
  void clientConnected();

private:
  void acceptClient();
  void readClient();

  QLocalServer server_;
  QLocalSocket *client_ = nullptr;
  QByteArray pending_;
};
