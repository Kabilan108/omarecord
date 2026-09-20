#include "annotation-model.hpp"

#include <QByteArray>
#include <algorithm>
#include <cmath>

namespace {

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
}

qreal AnnotationModel::opacity(const Stroke &stroke, qint64 now) const {
  if (hold_) {
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
  strokes_.removeIf([&](const Stroke &stroke) { return opacity(stroke, now) <= 0.0; });
}

bool AnnotationModel::fading(qint64 now) const {
  if (hold_) {
    return false;
  }
  return std::any_of(strokes_.cbegin(), strokes_.cend(), [&](const Stroke &stroke) {
    return now - stroke.finishedAt >= settings_.fadeAfterMs;
  });
}

qint64 AnnotationModel::msUntilNextFade(qint64 now) const {
  if (hold_ || strokes_.isEmpty()) {
    return -1;
  }
  qint64 soonest = -1;
  for (const Stroke &stroke : strokes_) {
    const qint64 wait = std::max<qint64>(0, stroke.finishedAt + settings_.fadeAfterMs - now);
    soonest = soonest < 0 ? wait : std::min(soonest, wait);
  }
  return soonest;
}
