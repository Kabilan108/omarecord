/** @fileoverview Full-output region picker widget: dims the output, follows
 * the pointer with a crosshair, and turns a drag into a RegionRect in Niri
 * global coordinates. Layer-shell attachment is separate so tests can drive
 * it offscreen. */
#pragma once

#include "niri.hpp"
#include "rect.hpp"

#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <functional>
#include <optional>

/** Smallest region worth recording; smaller drags keep the picker open. */
inline constexpr int kMinimumRegionSide = 8;

class SelectorWindow final : public QWidget {
  Q_OBJECT
public:
  explicit SelectorWindow(OutputInfo output, QWidget *parent = nullptr);

  /** Binds the widget to the output as an exclusive overlay layer surface.
   * Returns false when the platform has no layer shell. */
  [[nodiscard]] bool attachLayerShell();

  /** Space asks the picker for the focused window's rect; nullopt means the
   * pick failed and the selector finishes with `failed()` set so the caller
   * can report why. Without a picker, Space is ignored. */
  using WindowPicker = std::function<std::optional<RegionRect>()>;
  void setWindowPicker(WindowPicker picker) {
    windowPicker_ = std::move(picker);
  }

  /** Set once `finished` fires with a confirmed region. */
  [[nodiscard]] std::optional<RegionRect> result() const { return result_; }
  [[nodiscard]] bool cancelled() const { return cancelled_; }
  [[nodiscard]] bool failed() const { return failed_; }

signals:
  /** Emitted once, on confirm or cancel. */
  void finished();

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  [[nodiscard]] QRect clampedSelection() const;
  [[nodiscard]] QPoint toGlobal(const QPointF &local) const;
  void confirm(const QRect &localRect);
  void cancel();
  void pickWindow();
  void drawReadout(QPainter &painter, const QPointF &anchor,
                   const QString &text) const;

  OutputInfo output_;
  QPointF cursor_;
  QPointF dragStart_;
  QRectF selection_;
  bool dragging_ = false;
  bool done_ = false;
  bool cancelled_ = false;
  bool failed_ = false;
  std::optional<RegionRect> result_;
  WindowPicker windowPicker_;
};
