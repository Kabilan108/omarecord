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

  /** Set once `finished` fires with a confirmed region. */
  [[nodiscard]] std::optional<RegionRect> result() const { return result_; }
  [[nodiscard]] bool cancelled() const { return cancelled_; }

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
  void drawReadout(QPainter &painter, const QPointF &anchor,
                   const QString &text) const;

  OutputInfo output_;
  QPointF cursor_;
  QPointF dragStart_;
  QRectF selection_;
  bool dragging_ = false;
  bool done_ = false;
  bool cancelled_ = false;
  std::optional<RegionRect> result_;
};
