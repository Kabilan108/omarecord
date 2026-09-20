/** @fileoverview Strokes with fade/hold lifetime, driven by an explicit
 * monotonic clock so the smoke suite can step time without a GUI. */
#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <optional>
#include <QtGlobal>

enum class Tool { Pen, Arrow, Rectangle, Highlighter, Select };

struct Stroke {
  Tool tool = Tool::Pen;
  QColor color;
  qreal width = 4.0;
  /** Pen/highlighter: the polyline. Arrow/rectangle: start and end only. */
  QList<QPointF> points;
  /** Monotonic ms when the pointer was released; the fade clock starts here. */
  qint64 finishedAt = 0;
};

struct FadeSettings {
  qint64 fadeAfterMs = 5000;
  qint64 fadeDurationMs = 700;
};

/** Reads OMARECORD_FADE_SECONDS and OMARECORD_FADE_DURATION (both seconds,
 * fractional allowed); unset or unparsable values keep the defaults. */
[[nodiscard]] FadeSettings fadeSettingsFromEnvironment();

class AnnotationModel final {
public:
  explicit AnnotationModel(FadeSettings settings = {});

  [[nodiscard]] const FadeSettings &settings() const { return settings_; }
  [[nodiscard]] const QList<Stroke> &strokes() const { return strokes_; }
  [[nodiscard]] bool hold() const { return hold_; }
  [[nodiscard]] bool active() const { return active_.has_value(); }
  [[nodiscard]] const Stroke *activeStroke() const {
    return active_ ? &*active_ : nullptr;
  }

  void begin(Tool tool, const QColor &color, qreal width, const QPointF &point);
  void extend(const QPointF &point);
  /** Commits the active stroke; a stroke that never moved is dropped. */
  void finish(qint64 now);
  void cancel();

  /** Turning hold off restarts every stroke's fade clock at `now`. */
  void setHold(bool hold, qint64 now);
  /** Drops every stroke and the selection. */
  void clear();

  /** Index into strokes() of the selected stroke, if any. A selected stroke
   * never fades; its fade clock restarts on deselect. */
  [[nodiscard]] std::optional<int> selected() const { return selected_; }
  void select(int index);
  void deselect(qint64 now);
  /** Topmost stroke under `point` (rect-local), with the per-tool slack
   * described in the plan; empty when nothing is close enough. */
  [[nodiscard]] std::optional<int> hitTest(const QPointF &point) const;
  /** Translates the selected stroke and restarts its fade clock. */
  void moveSelected(const QPointF &delta, qint64 now);
  void removeSelected();
  /** Single-level undo: drops the most recent stroke. */
  void removeLast();

  /** 1.0 until the fade starts, linear to 0.0 across the fade duration. */
  [[nodiscard]] qreal opacity(const Stroke &stroke, qint64 now) const;
  /** Drops strokes whose fade has completed. */
  void prune(qint64 now);
  /** True while at least one stroke is mid-fade and needs periodic repaints. */
  [[nodiscard]] bool fading(qint64 now) const;
  /** Ms until the next stroke starts fading, or -1 when nothing is waiting. */
  [[nodiscard]] qint64 msUntilNextFade(qint64 now) const;

private:
  FadeSettings settings_;
  QList<Stroke> strokes_;
  std::optional<Stroke> active_;
  std::optional<int> selected_;
  bool hold_ = false;

  [[nodiscard]] bool isSelected(const Stroke &stroke) const;
};
