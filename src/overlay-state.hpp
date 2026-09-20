/** @fileoverview Overlay state and toolbar placement, free of any window so
 * the smoke suite can drive it headless. */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>

/** Where the toolbar sits relative to the recorded rect. */
enum class ToolbarPlacement { Above, Below, CornerPill };

struct ToolbarLayout {
  ToolbarPlacement placement = ToolbarPlacement::Above;
  /** Toolbar rect in window-local pixels (window covers the whole output). */
  QRect rect;
  /** Collapsed pill rect for CornerPill; equals rect otherwise. */
  QRect collapsed;
};

/** Gap between the recorded rect's border and the toolbar. */
inline constexpr int kToolbarGap = 8;
inline constexpr int kBorderWidth = 2;
inline constexpr int kCornerPillSize = 24;
inline constexpr int kCornerMargin = 8;

/** Places a toolbar of `toolbarSize` outside `rect`, both in global
 * coordinates, returning window-local rects for a window covering `output`. */
[[nodiscard]] ToolbarLayout computeToolbarLayout(const QRect &rect, const QRect &output,
                                                 const QSize &toolbarSize);

[[nodiscard]] QString formatElapsed(int seconds);

class OverlayState final : public QObject {
  Q_OBJECT
public:
  explicit OverlayState(QObject *parent = nullptr);

  [[nodiscard]] bool paused() const { return paused_; }
  [[nodiscard]] bool drawing() const { return drawing_; }
  [[nodiscard]] int elapsedSeconds() const { return elapsed_; }
  [[nodiscard]] bool quitRequested() const { return quit_; }

  /** Applies one inbound command; unknown or malformed commands are ignored. */
  void applyCommand(const QJsonObject &command);
  void toggleDrawing();
  /** User clicked Pause/Resume; the orchestrator owns the actual state, so
   * this only emits the outbound event. */
  void requestPause();
  void requestStop();

signals:
  void changed();
  void quitRequestedChanged();
  /** An outbound event to relay to the orchestrator. */
  void eventRequested(const QString &event);

private:
  bool paused_ = false;
  bool drawing_ = false;
  bool quit_ = false;
  int elapsed_ = 0;
};
