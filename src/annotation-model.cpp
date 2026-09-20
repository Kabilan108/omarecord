#include "annotation-model.hpp"

#include <QByteArray>
#include <QLineF>
#include <QRectF>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr qreal kMinimumHitSlack = 8.0;
constexpr qreal kHitPadding = 4.0;

qreal distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b) {
  const QPointF ab = b - a;
  const qreal lengthSquared = QPointF::dotProduct(ab, ab);
  if (lengthSquared <= 0.0) {
    return QLineF(point, a).length();
  }
  const qreal t = std::clamp(QPointF::dotProduct(point - a, ab) / lengthSquared, 0.0, 1.0);
  return QLineF(point, a + ab * t).length();
}

qreal distanceToStroke(const Stroke &stroke, const QPointF &point) {
  const QList<QPointF> &points = stroke.points;
  if (points.size() < 2) {
    return std::numeric_limits<qreal>::infinity();
  }
  if (stroke.tool == Tool::Rectangle) {
    const QRectF rect = QRectF(points.first(), points.last()).normalized();
    const std::array<QPointF, 4> corners = {rect.topLeft(), rect.topRight(), rect.bottomRight(),
                                            rect.bottomLeft()};
    qreal best = std::numeric_limits<qreal>::infinity();
    for (size_t i = 0; i < corners.size(); ++i) {
      best = std::min(best, distanceToSegment(point, corners[i], corners[(i + 1) % 4]));
    }
    return best;
  }
  if (stroke.tool == Tool::Arrow) {
    return distanceToSegment(point, points.first(), points.last());
  }
  qreal best = std::numeric_limits<qreal>::infinity();
  for (qsizetype i = 0; i + 1 < points.size(); ++i) {
    best = std::min(best, distanceToSegment(point, points.at(i), points.at(i + 1)));
  }
  return best;
}

qint64 secondsFromEnvironment(const char *name, qint64 fallbackMs) {
  const QByteArray raw = qgetenv(name);
  if (raw.isEmpty()) {
    return fallbackMs;
  }
  bool ok = false;
  const double seconds = raw.toDouble(&ok);
  if (!ok || !std::isfinite(seconds) || seconds < 0.0) {
    return fallbackMs;
  }
  return static_cast<qint64>(std::llround(seconds * 1000.0));
}

} // namespace

FadeSettings fadeSettingsFromEnvironment() {
  FadeSettings defaults;
  return {
      secondsFromEnvironment("OMARECORD_FADE_SECONDS", defaults.fadeAfterMs),
      secondsFromEnvironment("OMARECORD_FADE_DURATION", defaults.fadeDurationMs),
  };
}

AnnotationModel::AnnotationModel(FadeSettings settings) : settings_(settings) {}

void AnnotationModel::begin(Tool tool, const QColor &color, qreal width,
                            const QPointF &point) {
  active_ = Stroke{tool, color, width, {point, point}, 0};
}

void AnnotationModel::extend(const QPointF &point) {
  if (!active_) {
    return;
  }
  if (active_->tool == Tool::Arrow || active_->tool == Tool::Rectangle) {
    active_->points.last() = point;
  } else {
    active_->points.append(point);
  }
}

void AnnotationModel::finish(qint64 now) {
  if (!active_) {
    return;
  }
  Stroke stroke = std::move(*active_);
  active_.reset();
  const bool moved = std::any_of(stroke.points.cbegin(), stroke.points.cend(),
                                 [&](const QPointF &p) { return p != stroke.points.first(); });
  if (!moved) {
    return;
  }
  stroke.finishedAt = now;
  strokes_.append(std::move(stroke));
}

void AnnotationModel::cancel() { active_.reset(); }

void AnnotationModel::setHold(bool hold, qint64 now) {
  if (hold == hold_) {
    return;
  }
  hold_ = hold;
  if (!hold_) {
    for (Stroke &stroke : strokes_) {
      stroke.finishedAt = now;
    }
  }
}

void AnnotationModel::clear() {
  strokes_.clear();
  active_.reset();
  selected_.reset();
}

bool AnnotationModel::isSelected(const Stroke &stroke) const {
  return selected_ && &strokes_.at(*selected_) == &stroke;
}

void AnnotationModel::select(int index) {
  if (index < 0 || index >= strokes_.size()) {
    return;
  }
  selected_ = index;
}

void AnnotationModel::deselect(qint64 now) {
  if (!selected_) {
    return;
  }
  strokes_[*selected_].finishedAt = now;
  selected_.reset();
}

std::optional<int> AnnotationModel::hitTest(const QPointF &point) const {
  for (qsizetype index = strokes_.size() - 1; index >= 0; --index) {
    const Stroke &stroke = strokes_.at(index);
    const qreal slack = std::max(stroke.width / 2.0 + kHitPadding, kMinimumHitSlack);
    if (distanceToStroke(stroke, point) <= slack) {
      return static_cast<int>(index);
    }
  }
  return std::nullopt;
}

void AnnotationModel::moveSelected(const QPointF &delta, qint64 now) {
  if (!selected_) {
    return;
  }
  Stroke &stroke = strokes_[*selected_];
  for (QPointF &point : stroke.points) {
    point += delta;
  }
  stroke.finishedAt = now;
}

void AnnotationModel::removeSelected() {
  if (!selected_) {
    return;
  }
  strokes_.removeAt(*selected_);
  selected_.reset();
}

void AnnotationModel::removeLast() {
  if (strokes_.isEmpty()) {
    return;
  }
  if (selected_ && *selected_ == strokes_.size() - 1) {
    selected_.reset();
  }
  strokes_.removeLast();
}

qreal AnnotationModel::opacity(const Stroke &stroke, qint64 now) const {
  if (hold_ || isSelected(stroke)) {
    return 1.0;
  }
  const qint64 sinceFadeStart = now - stroke.finishedAt - settings_.fadeAfterMs;
  if (sinceFadeStart <= 0) {
    return 1.0;
  }
  if (settings_.fadeDurationMs <= 0 || sinceFadeStart >= settings_.fadeDurationMs) {
    return 0.0;
  }
  return 1.0 - static_cast<qreal>(sinceFadeStart) / static_cast<qreal>(settings_.fadeDurationMs);
}

void AnnotationModel::prune(qint64 now) {
  if (hold_) {
    return;
  }
  QList<Stroke> kept;
  std::optional<int> selected;
  for (qsizetype index = 0; index < strokes_.size(); ++index) {
    const Stroke &stroke = strokes_.at(index);
    if (opacity(stroke, now) <= 0.0) {
      continue;
    }
    if (selected_ && *selected_ == index) {
      selected = static_cast<int>(kept.size());
    }
    kept.append(stroke);
  }
  strokes_ = std::move(kept);
  selected_ = selected;
}

bool AnnotationModel::fading(qint64 now) const {
  if (hold_) {
    return false;
  }
  return std::any_of(strokes_.cbegin(), strokes_.cend(), [&](const Stroke &stroke) {
    return !isSelected(stroke) && now - stroke.finishedAt >= settings_.fadeAfterMs;
  });
}

qint64 AnnotationModel::msUntilNextFade(qint64 now) const {
  if (hold_ || strokes_.isEmpty()) {
    return -1;
  }
  qint64 soonest = -1;
  for (const Stroke &stroke : strokes_) {
    if (isSelected(stroke)) {
      continue;
    }
    const qint64 wait = std::max<qint64>(0, stroke.finishedAt + settings_.fadeAfterMs - now);
    soonest = soonest < 0 ? wait : std::min(soonest, wait);
  }
  return soonest;
}
