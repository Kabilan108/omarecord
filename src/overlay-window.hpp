/** @fileoverview Layer-shell window that paints the border and toolbar for
 * one recorded rect and turns toolbar clicks into state requests. */
#pragma once

#include "overlay-state.hpp"

#include <QList>
#include <QRect>
#include <QString>
#include <QWidget>

class QPainter;

class OverlayWindow final : public QWidget {
  Q_OBJECT
public:
  /** `rect` and `output` are Niri global logical coordinates. */
  OverlayWindow(OverlayState &state, const QRect &rect, const QRect &output);

  [[nodiscard]] ToolbarLayout toolbarLayout() const { return layout_; }
  /** Region that should receive pointer input; everything else passes through. */
  [[nodiscard]] QRect inputRect() const;

signals:
  void surfaceReady();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  enum class Button { None, Draw, Pause, Stop };
  struct ButtonRect {
    Button button;
    QRect rect;
  };

  void applyMask();
  [[nodiscard]] bool collapsed() const;
  [[nodiscard]] QList<ButtonRect> buttonRects() const;
  [[nodiscard]] Button buttonAt(const QPoint &point) const;
  [[nodiscard]] QColor borderColor() const;
  void paintToolbar(QPainter &painter);

  OverlayState &state_;
  QRect localRect_;
  ToolbarLayout layout_;
  bool expanded_ = false;
  bool readyEmitted_ = false;
  Button hovered_ = Button::None;
};
