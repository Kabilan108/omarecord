#include "overlay-window.hpp"

#include "icons.hpp"

#include <QEnterEvent>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QWindow>

namespace {

constexpr int kToolbarHeight = 36;
constexpr int kToolbarPadding = 10;
constexpr int kButtonSize = 32;
constexpr int kButtonGap = 4;
constexpr int kElapsedWidth = 72;
constexpr int kDotSize = 10;

constexpr QSize toolbarSize() {
  return QSize(kToolbarPadding * 2 + kElapsedWidth + kButtonGap * 3 + kButtonSize * 3,
               kToolbarHeight);
}

QFont neuchaFont(int pointSize) {
  static const int fontId =
      QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Neucha.ttf"));
  const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
  QFont font(families.isEmpty() ? QStringLiteral("Neucha") : families.first());
  font.setPointSize(pointSize);
  return font;
}

} // namespace

OverlayWindow::OverlayWindow(OverlayState &state, const QRect &rect, const QRect &output)
    : state_(state), localRect_(rect.translated(-output.topLeft())),
      layout_(computeToolbarLayout(rect, output, toolbarSize())) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_NoSystemBackground);
  setMouseTracking(true);
  setWindowTitle(QStringLiteral("omarecord overlay"));
  setGeometry(0, 0, output.width(), output.height());
  applyMask();
  connect(&state_, &OverlayState::changed, this, qOverload<>(&QWidget::update));
}

bool OverlayWindow::collapsed() const {
  return layout_.placement == ToolbarPlacement::CornerPill && !expanded_;
}

QRect OverlayWindow::inputRect() const {
  return collapsed() ? layout_.collapsed : layout_.rect;
}

// QWidget::setMask would also clip painting to the toolbar; the QWindow mask
// only sets the Wayland input region, leaving the border paintable.
void OverlayWindow::applyMask() {
  if (QWindow *handle = windowHandle()) {
    handle->setMask(QRegion(inputRect()));
  }
}

QList<OverlayWindow::ButtonRect> OverlayWindow::buttonRects() const {
  const QRect bar = layout_.rect;
  const int y = bar.top() + (bar.height() - kButtonSize) / 2;
  int x = bar.left() + kToolbarPadding + kElapsedWidth + kButtonGap;
  QList<ButtonRect> rects;
  for (Button button : {Button::Draw, Button::Pause, Button::Stop}) {
    rects.append({button, QRect(x, y, kButtonSize, kButtonSize)});
    x += kButtonSize + kButtonGap;
  }
  return rects;
}

OverlayWindow::Button OverlayWindow::buttonAt(const QPoint &point) const {
  if (collapsed()) {
    return Button::None;
  }
  for (const ButtonRect &entry : buttonRects()) {
    if (entry.rect.contains(point)) {
      return entry.button;
    }
  }
  return Button::None;
}

QColor OverlayWindow::borderColor() const {
  if (state_.paused()) {
    return QColor(QStringLiteral("#9a9a9e"));
  }
  if (state_.drawing()) {
    return QColor(QStringLiteral("#ff9f0a"));
  }
  return QColor(QStringLiteral("#ff3b30"));
}

void OverlayWindow::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.fillRect(rect(), Qt::transparent);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

  // The stroke is centred on a path half a stroke outside the rect, so every
  // border pixel lies outside the captured region.
  const qreal half = kBorderWidth / 2.0;
  const QRectF outline = QRectF(localRect_).adjusted(-half, -half, half, half);
  QPen pen(borderColor(), kBorderWidth);
  pen.setJoinStyle(Qt::MiterJoin);
  if (state_.paused()) {
    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern({4.0, 3.0});
  }
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(outline);

  painter.setRenderHint(QPainter::Antialiasing);
  paintToolbar(painter);
}

void OverlayWindow::paintToolbar(QPainter &painter) {
  const QColor text(QStringLiteral("#f5f5f7"));
  const QColor accent = borderColor();
  const QRect bar = inputRect();

  painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
  painter.setBrush(QColor(18, 18, 22, 235));
  const qreal radius = bar.height() / 2.0;
  painter.drawRoundedRect(QRectF(bar).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

  if (collapsed()) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(QRectF(bar.center().x() - kDotSize / 2.0,
                               bar.center().y() - kDotSize / 2.0, kDotSize, kDotSize));
    return;
  }

  const QRect elapsedRect(bar.left() + kToolbarPadding, bar.top(), kElapsedWidth,
                          bar.height());
  painter.setPen(Qt::NoPen);
  painter.setBrush(accent);
  painter.drawEllipse(QRectF(elapsedRect.left(), elapsedRect.center().y() - kDotSize / 2.0,
                             kDotSize, kDotSize));
  painter.setPen(text);
  painter.setFont(neuchaFont(14));
  painter.drawText(elapsedRect.adjusted(kDotSize + 6, 0, 0, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, formatElapsed(state_.elapsedSeconds()));

  for (const ButtonRect &entry : buttonRects()) {
    const bool active = entry.button == Button::Draw && state_.drawing();
    if (active || entry.button == hovered_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(active ? QColor(255, 159, 10, 56) : QColor(255, 255, 255, 28));
      painter.drawRoundedRect(entry.rect, 8, 8);
    }
    const QColor color = active ? QColor(QStringLiteral("#ff9f0a")) : text;
    const QRectF bounds(entry.rect);
    const QPointF c = bounds.center();
    switch (entry.button) {
    case Button::Draw:
      drawToolbarIcon(painter, bounds, QStringLiteral("tool-freehand"), QString(), color);
      break;
    case Button::Pause:
      painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.setBrush(Qt::NoBrush);
      if (state_.paused()) {
        QPainterPath play;
        play.moveTo(c.x() - 4, c.y() - 6);
        play.lineTo(c.x() + 6, c.y());
        play.lineTo(c.x() - 4, c.y() + 6);
        play.closeSubpath();
        painter.setBrush(color);
        painter.drawPath(play);
      } else {
        painter.drawLine(QPointF(c.x() - 4, c.y() - 6), QPointF(c.x() - 4, c.y() + 6));
        painter.drawLine(QPointF(c.x() + 4, c.y() - 6), QPointF(c.x() + 4, c.y() + 6));
      }
      break;
    case Button::Stop:
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(QStringLiteral("#ff3b30")));
      painter.drawRoundedRect(QRectF(c.x() - 6, c.y() - 6, 12, 12), 2, 2);
      break;
    case Button::None:
      break;
    }
  }
}

void OverlayWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  switch (buttonAt(event->pos())) {
  case Button::Draw:
    state_.toggleDrawing();
    break;
  case Button::Pause:
    state_.requestPause();
    break;
  case Button::Stop:
    state_.requestStop();
    break;
  case Button::None:
    break;
  }
  event->accept();
}

void OverlayWindow::mouseMoveEvent(QMouseEvent *event) {
  const Button now = buttonAt(event->pos());
  if (now != hovered_) {
    hovered_ = now;
    update();
  }
}

void OverlayWindow::enterEvent(QEnterEvent *) {
  if (collapsed()) {
    expanded_ = true;
    applyMask();
    update();
  }
}

void OverlayWindow::leaveEvent(QEvent *) {
  hovered_ = Button::None;
  if (layout_.placement == ToolbarPlacement::CornerPill && expanded_) {
    expanded_ = false;
    applyMask();
  }
  update();
}

void OverlayWindow::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  applyMask();
  if (QWindow *handle = windowHandle()) {
    handle->installEventFilter(this);
  }
}

bool OverlayWindow::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::Expose && !readyEmitted_ && watched == windowHandle() &&
      windowHandle()->isExposed()) {
    readyEmitted_ = true;
    emit surfaceReady();
  }
  return QWidget::eventFilter(watched, event);
}
