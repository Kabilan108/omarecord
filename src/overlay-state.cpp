#include "overlay-state.hpp"

#include <QJsonValue>

ToolbarLayout computeToolbarLayout(const QRect &rect, const QRect &output,
                                   const QSize &toolbarSize) {
  const QRect local = rect.translated(-output.topLeft());
  const int outerTop = local.top() - kBorderWidth;
  const int outerBottom = local.bottom() + 1 + kBorderWidth;
  const int width = toolbarSize.width();
  const int height = toolbarSize.height();
  int x = local.left() + (local.width() - width) / 2;
  x = std::clamp(x, 0, std::max(0, output.width() - width));

  ToolbarLayout layout;
  if (outerTop - kToolbarGap - height >= 0) {
    layout.placement = ToolbarPlacement::Above;
    layout.rect = QRect(x, outerTop - kToolbarGap - height, width, height);
  } else if (outerBottom + kToolbarGap + height <= output.height()) {
    layout.placement = ToolbarPlacement::Below;
    layout.rect = QRect(x, outerBottom + kToolbarGap, width, height);
  } else {
    layout.placement = ToolbarPlacement::CornerPill;
    layout.rect = QRect(output.width() - kCornerMargin - width, kCornerMargin, width, height);
    layout.collapsed = QRect(output.width() - kCornerMargin - kCornerPillSize, kCornerMargin,
                             kCornerPillSize, kCornerPillSize);
    return layout;
  }
  layout.collapsed = layout.rect;
  return layout;
}

QString formatElapsed(int seconds) {
  seconds = std::max(0, seconds);
  return QStringLiteral("%1:%2")
      .arg(seconds / 60, 2, 10, QLatin1Char('0'))
      .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

OverlayState::OverlayState(QObject *parent) : QObject(parent) {}

void OverlayState::applyCommand(const QJsonObject &command) {
  const QString cmd = command.value(QStringLiteral("cmd")).toString();
  if (cmd == QStringLiteral("paused")) {
    const QJsonValue value = command.value(QStringLiteral("value"));
    if (!value.isBool()) {
      return;
    }
    paused_ = value.toBool();
    emit changed();
  } else if (cmd == QStringLiteral("elapsed")) {
    const QJsonValue value = command.value(QStringLiteral("seconds"));
    if (!value.isDouble()) {
      return;
    }
    elapsed_ = static_cast<int>(value.toDouble());
    emit changed();
  } else if (cmd == QStringLiteral("quit")) {
    quit_ = true;
    emit quitRequestedChanged();
  }
}

void OverlayState::toggleDrawing() { setDrawing(!drawing_); }

void OverlayState::setDrawing(bool drawing) {
  if (drawing == drawing_) {
    return;
  }
  drawing_ = drawing;
  emit drawingChanged(drawing_);
  emit changed();
}

void OverlayState::requestPause() { emit eventRequested(QStringLiteral("pause")); }

void OverlayState::requestStop() { emit eventRequested(QStringLiteral("stop")); }
