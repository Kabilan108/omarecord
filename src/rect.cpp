#include "rect.hpp"

#include <QRegularExpression>

QJsonObject RegionRect::toJson() const {
  return {{QStringLiteral("output"), output},
          {QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("w"), rect.width()},
          {QStringLiteral("h"), rect.height()}};
}

QString RegionRect::toGeometry() const {
  return QStringLiteral("%1x%2+%3+%4")
      .arg(rect.width())
      .arg(rect.height())
      .arg(rect.x())
      .arg(rect.y());
}

std::optional<QRect> parseGeometry(const QString &text) {
  static const QRegularExpression pattern(
      QStringLiteral("^(\\d+)x(\\d+)(\\+-?\\d+|-\\d+)(\\+-?\\d+|-\\d+)$"));
  const auto match = pattern.match(text.trimmed());
  if (!match.hasMatch()) {
    return std::nullopt;
  }
  const auto offset = [&](int group) {
    QString text = match.captured(group);
    if (text.startsWith(QLatin1Char('+'))) {
      text.remove(0, 1);
    }
    return text.toInt();
  };
  const QRect rect(offset(3), offset(4), match.captured(1).toInt(),
                   match.captured(2).toInt());
  if (rect.isEmpty()) {
    return std::nullopt;
  }
  return rect;
}
