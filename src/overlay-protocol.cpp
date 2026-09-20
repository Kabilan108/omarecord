#include "overlay-protocol.hpp"

#include <QJsonDocument>
#include <QLocalSocket>

std::optional<QJsonObject> parseCommandLine(const QByteArray &line) {
  const QByteArray trimmed = line.trimmed();
  if (trimmed.isEmpty()) {
    return std::nullopt;
  }
  const QJsonDocument document = QJsonDocument::fromJson(trimmed);
  if (!document.isObject()) {
    return std::nullopt;
  }
  return document.object();
}

QByteArray formatEventLine(const QString &event) {
  QJsonObject object;
  object.insert(QStringLiteral("event"), event);
  return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

OverlayProtocol::OverlayProtocol(QObject *parent) : QObject(parent) {
  connect(&server_, &QLocalServer::newConnection, this, &OverlayProtocol::acceptClient);
}

bool OverlayProtocol::listen(const QString &path, QString &error) {
  QLocalServer::removeServer(path);
  if (!server_.listen(path)) {
    error = server_.errorString();
    return false;
  }
  return true;
}

void OverlayProtocol::sendEvent(const QString &event) {
  if (!client_) {
    return;
  }
  client_->write(formatEventLine(event));
  client_->flush();
}

void OverlayProtocol::acceptClient() {
  while (QLocalSocket *next = server_.nextPendingConnection()) {
    if (client_) {
      client_->disconnectFromServer();
      client_->deleteLater();
    }
    client_ = next;
    pending_.clear();
    connect(client_, &QLocalSocket::readyRead, this, &OverlayProtocol::readClient);
    connect(client_, &QLocalSocket::disconnected, this, [this, next] {
      if (client_ == next) {
        client_ = nullptr;
      }
      next->deleteLater();
    });
    emit clientConnected();
  }
}

void OverlayProtocol::readClient() {
  pending_ += client_->readAll();
  int newline = -1;
  while ((newline = pending_.indexOf('\n')) >= 0) {
    const QByteArray line = pending_.left(newline);
    pending_.remove(0, newline + 1);
    if (const auto command = parseCommandLine(line)) {
      emit commandReceived(*command);
    }
  }
}
