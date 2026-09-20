/** @fileoverview Layer-shell window that paints the border, toolbar and
 * annotation strokes for one recorded rect and turns input into state
 * requests and strokes. */
#pragma once

#include "annotation-model.hpp"
#include "overlay-state.hpp"

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QRect>
#include <QRegion>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <array>

class QPainter;

/** Colour slots reachable with keys 1–4. */
inline constexpr std::array<const char *, 4> kStrokeColours = {"#ff453a", "#ffd60a",
                                                               "#30d158", "#0a84ff"};
/** Pen widths reachable with the wheel; the highlighter scales these up. */
inline constexpr std::array<qreal, 3> kStrokeWidths = {3.0, 6.0, 10.0};
inline constexpr qreal kHighlighterScale = 4.0;
inline constexpr int kHighlighterAlpha = 102;

class OverlayWindow final : public QWidget {
  Q_OBJECT
public:
  /** `rect` and `output` are Niri global logical coordinates. */
  OverlayWindow(OverlayState &state, const QRect &rect, const QRect &output,
                FadeSettings fade = fadeSettingsFromEnvironment());

  [[nodiscard]] ToolbarLayout toolbarLayout() const;
  /** Region that should receive pointer input; everything else passes through. */
  [[nodiscard]] QRegion inputRegion() const;
  /** The toolbar's current rect (collapsed pill or full bar). */
  [[nodiscard]] QRect inputRect() const;
  [[nodiscard]] QRect recordedRect() const { return localRect_; }

  [[nodiscard]] Tool tool() const { return tool_; }
  [[nodiscard]] int colourIndex() const { return colour_; }
  [[nodiscard]] QColor colour() const { return QColor(QLatin1String(kStrokeColours[colour_])); }
  [[nodiscard]] int widthStep() const { return width_; }
  [[nodiscard]] const AnnotationModel &model() const { return model_; }
  [[nodiscard]] bool fadeTimerActive() const { return fadeTimer_.isActive(); }
  /** Tooltip currently shown under the hovered toolbar item, empty if none. */
  [[nodiscard]] QString visibleTooltip() const {
    return tooltipShown_ ? tooltipFor(hovered_) : QString();
  }

signals:
  void surfaceReady();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  enum class Button {
    None,
    Draw,
    Hold,
    Pause,
    Stop,
    ToolPen,
    ToolArrow,
    ToolRectangle,
    ToolHighlighter,
    Colour0,
    Colour1,
    Colour2,
    Colour3,
    Width,
  };
  struct ButtonRect {
    Button button;
    QRect rect;
  };

  [[nodiscard]] qint64 now() const { return clock_.elapsed(); }
  void applyMask();
  void onDrawingChanged(bool drawing);
  void setTool(Tool tool);
  void setColour(int index);
  void stepWidth(int delta);
  [[nodiscard]] qreal strokeWidth() const;
  [[nodiscard]] QColor strokeColour() const;
  void scheduleFade();
  void tickFade();
  [[nodiscard]] bool collapsed() const;
  [[nodiscard]] QList<ButtonRect> buttonRects() const;
  [[nodiscard]] Button buttonAt(const QPoint &point) const;
  [[nodiscard]] QColor borderColor() const;
  [[nodiscard]] QString tooltipFor(Button button) const;
  void setHovered(Button button);
  void paintToolbar(QPainter &painter);
  void paintTooltip(QPainter &painter);
  void paintStrokes(QPainter &painter);
  void paintStroke(QPainter &painter, const Stroke &stroke, qreal opacity) const;

  OverlayState &state_;
  QRect localRect_;
  ToolbarLayout idleLayout_;
  ToolbarLayout drawingLayout_;
  AnnotationModel model_;
  QElapsedTimer clock_;
  QTimer fadeTimer_;
  QTimer fadeStartTimer_;
  QTimer tooltipTimer_;
  bool tooltipShown_ = false;
  Tool tool_ = Tool::Pen;
  int colour_ = 0;
  int width_ = 1;
  bool expanded_ = false;
  bool readyEmitted_ = false;
  Button hovered_ = Button::None;
};
