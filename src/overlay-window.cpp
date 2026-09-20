#include "overlay-window.hpp"

#include "icons.hpp"

#include <QEnterEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QShowEvent>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>

namespace {

constexpr int kToolbarHeight = 36;
constexpr int kToolbarPadding = 10;
constexpr int kButtonSize = 32;
constexpr int kButtonGap = 4;
constexpr int kElapsedWidth = 72;
constexpr int kDotSize = 10;
constexpr int kToolButtonSize = 26;
constexpr int kSwatchSize = 18;
constexpr int kSwatchGap = 6;
constexpr int kSeparator = 8;
constexpr int kFadeTickMs = 33;
constexpr int kTooltipDelayMs = 400;
constexpr int kTooltipHeight = 22;
constexpr int kTooltipGap = 6;
constexpr int kHoldWidth = 44;

constexpr int idleToolbarWidth = kToolbarPadding * 2 + kElapsedWidth + kButtonGap * 4 +
                                 kButtonSize * 3 + kHoldWidth;
constexpr int drawingExtras = kSeparator + kToolButtonSize * 4 + kButtonGap * 3 + kSeparator +
                              kSwatchSize * 4 + kSwatchGap * 3 + kSeparator +
                              kToolButtonSize + kSeparator;

constexpr QSize toolbarSize(bool drawing) {
  return QSize(idleToolbarWidth + (drawing ? drawingExtras : 0), kToolbarHeight);
}

QFont neuchaFont(int pointSize) {
  static const int fontId =
      QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Neucha.ttf"));
  const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
  QFont font(families.isEmpty() ? QStringLiteral("Neucha") : families.first());
  font.setPointSize(pointSize);
  return font;
}

QPainterPath smoothPolyline(const QList<QPointF> &points) {
  QPainterPath path(points.first());
  for (qsizetype index = 1; index + 1 < points.size(); ++index) {
    const QPointF midpoint = (points.at(index) + points.at(index + 1)) / 2.0;
    path.quadTo(points.at(index), midpoint);
  }
  path.lineTo(points.last());
  return path;
}

QString toolIcon(Tool tool) {
  switch (tool) {
  case Tool::Pen:
    return QStringLiteral("tool-freehand");
  case Tool::Arrow:
    return QStringLiteral("tool-arrow");
  case Tool::Rectangle:
    return QStringLiteral("tool-rectangle");
  case Tool::Highlighter:
    return QStringLiteral("tool-highlighter");
  }
  return QString();
}

} // namespace

OverlayWindow::OverlayWindow(OverlayState &state, const QRect &rect, const QRect &output,
                             FadeSettings fade)
    : state_(state), localRect_(rect.translated(-output.topLeft())),
      idleLayout_(computeToolbarLayout(rect, output, toolbarSize(false))),
      drawingLayout_(computeToolbarLayout(rect, output, toolbarSize(true))), model_(fade) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_NoSystemBackground);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setWindowTitle(QStringLiteral("omarecord overlay"));
  setGeometry(0, 0, output.width(), output.height());
  clock_.start();
  fadeTimer_.setInterval(kFadeTickMs);
  fadeStartTimer_.setSingleShot(true);
  connect(&fadeTimer_, &QTimer::timeout, this, &OverlayWindow::tickFade);
  connect(&fadeStartTimer_, &QTimer::timeout, this, &OverlayWindow::tickFade);
  tooltipTimer_.setSingleShot(true);
  tooltipTimer_.setInterval(kTooltipDelayMs);
  connect(&tooltipTimer_, &QTimer::timeout, this, [this] {
    tooltipShown_ = hovered_ != Button::None;
    update();
  });
  applyMask();
  connect(&state_, &OverlayState::changed, this, qOverload<>(&QWidget::update));
  connect(&state_, &OverlayState::drawingChanged, this, &OverlayWindow::onDrawingChanged);
}

ToolbarLayout OverlayWindow::toolbarLayout() const {
  return state_.drawing() ? drawingLayout_ : idleLayout_;
}

bool OverlayWindow::collapsed() const {
  return toolbarLayout().placement == ToolbarPlacement::CornerPill && !expanded_;
}

QRect OverlayWindow::inputRect() const {
  const ToolbarLayout layout = toolbarLayout();
  return collapsed() ? layout.collapsed : layout.rect;
}

QRegion OverlayWindow::inputRegion() const {
  QRegion region(inputRect());
  if (state_.drawing()) {
    region += localRect_;
  }
  return region;
}

// QWidget::setMask would also clip painting to the toolbar; the QWindow mask
// only sets the Wayland input region, leaving the border paintable.
void OverlayWindow::applyMask() {
  if (QWindow *handle = windowHandle()) {
    handle->setMask(inputRegion());
  }
}

void OverlayWindow::onDrawingChanged(bool drawing) {
  if (!drawing) {
    model_.cancel();
    setHovered(Button::None);
  } else {
    setFocus();
  }
  applyMask();
  update();
}

void OverlayWindow::setTool(Tool tool) {
  tool_ = tool;
  update();
}

void OverlayWindow::setColour(int index) {
  colour_ = std::clamp<int>(index, 0, kStrokeColours.size() - 1);
  update();
}

void OverlayWindow::stepWidth(int delta) {
  width_ = std::clamp<int>(width_ + delta, 0, kStrokeWidths.size() - 1);
  update();
}

qreal OverlayWindow::strokeWidth() const {
  const qreal base = kStrokeWidths[width_];
  return tool_ == Tool::Highlighter ? base * kHighlighterScale : base;
}

QColor OverlayWindow::strokeColour() const {
  QColor ink = colour();
  if (tool_ == Tool::Highlighter) {
    ink.setAlpha(kHighlighterAlpha);
  }
  return ink;
}

// Two timers keep the window idle between input: a single shot wakes us when
// the oldest stroke starts fading, and only then does the ~30 Hz tick run.
void OverlayWindow::scheduleFade() {
  const qint64 current = now();
  if (model_.fading(current)) {
    fadeStartTimer_.stop();
    if (!fadeTimer_.isActive()) {
      fadeTimer_.start();
    }
    return;
  }
  fadeTimer_.stop();
  const qint64 wait = model_.msUntilNextFade(current);
  if (wait < 0) {
    fadeStartTimer_.stop();
  } else {
    fadeStartTimer_.start(static_cast<int>(std::min<qint64>(wait, INT_MAX)));
  }
}

void OverlayWindow::tickFade() {
  model_.prune(now());
  scheduleFade();
  update();
}

QList<OverlayWindow::ButtonRect> OverlayWindow::buttonRects() const {
  const QRect bar = toolbarLayout().rect;
  const int y = bar.top() + (bar.height() - kButtonSize) / 2;
  const int toolY = bar.top() + (bar.height() - kToolButtonSize) / 2;
  const int swatchY = bar.top() + (bar.height() - kSwatchSize) / 2;
  int x = bar.left() + kToolbarPadding + kElapsedWidth + kButtonGap;
  QList<ButtonRect> rects;
  rects.append({Button::Draw, QRect(x, y, kButtonSize, kButtonSize)});
  x += kButtonSize + kButtonGap;
  if (state_.drawing()) {
    x += kSeparator;
    for (Button button :
         {Button::ToolPen, Button::ToolArrow, Button::ToolRectangle, Button::ToolHighlighter}) {
      rects.append({button, QRect(x, toolY, kToolButtonSize, kToolButtonSize)});
      x += kToolButtonSize + kButtonGap;
    }
    x += kSeparator - kButtonGap;
    for (Button button : {Button::Colour0, Button::Colour1, Button::Colour2, Button::Colour3}) {
      rects.append({button, QRect(x, swatchY, kSwatchSize, kSwatchSize)});
      x += kSwatchSize + kSwatchGap;
    }
    x += kSeparator - kSwatchGap;
    rects.append({Button::Width, QRect(x, toolY, kToolButtonSize, kToolButtonSize)});
    x += kToolButtonSize + kSeparator;
  }
  rects.append({Button::Hold, QRect(x, y, kHoldWidth, kButtonSize)});
  x += kHoldWidth + kButtonGap;
  for (Button button : {Button::Pause, Button::Stop}) {
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

QString OverlayWindow::tooltipFor(Button button) const {
  switch (button) {
  case Button::Draw:
    return QStringLiteral("Draw (Esc exits)");
  case Button::Hold:
    return QStringLiteral("Hold strokes (L)");
  case Button::Pause:
    return state_.paused() ? QStringLiteral("Resume") : QStringLiteral("Pause");
  case Button::Stop:
    return QStringLiteral("Stop");
  case Button::ToolPen:
    return QStringLiteral("Pen (P)");
  case Button::ToolArrow:
    return QStringLiteral("Arrow (A)");
  case Button::ToolRectangle:
    return QStringLiteral("Rectangle (R)");
  case Button::ToolHighlighter:
    return QStringLiteral("Highlighter (H)");
  case Button::Colour0:
    return QStringLiteral("Red (1)");
  case Button::Colour1:
    return QStringLiteral("Yellow (2)");
  case Button::Colour2:
    return QStringLiteral("Green (3)");
  case Button::Colour3:
    return QStringLiteral("Blue (4)");
  case Button::Width:
    return QStringLiteral("Width %1/%2 (wheel; C clears)")
        .arg(width_ + 1)
        .arg(kStrokeWidths.size());
  case Button::None:
    break;
  }
  return QString();
}

void OverlayWindow::setHovered(Button button) {
  if (button == hovered_) {
    return;
  }
  hovered_ = button;
  tooltipShown_ = false;
  tooltipTimer_.stop();
  if (hovered_ != Button::None) {
    tooltipTimer_.start();
  }
  update();
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
  paintStrokes(painter);
  paintToolbar(painter);
  paintTooltip(painter);
}

// The tooltip sits on the toolbar's far side from the rect so it never lands
// inside the captured region.
void OverlayWindow::paintTooltip(QPainter &painter) {
  const QString text = visibleTooltip();
  if (text.isEmpty() || collapsed()) {
    return;
  }
  const QRect bar = inputRect();
  QRect anchor = bar;
  for (const ButtonRect &entry : buttonRects()) {
    if (entry.button == hovered_) {
      anchor = entry.rect;
    }
  }
  painter.setFont(neuchaFont(12));
  const int width = painter.fontMetrics().horizontalAdvance(text) + 16;
  const int x = std::clamp(anchor.center().x() - width / 2, 0, this->width() - width);
  const int y = toolbarLayout().placement == ToolbarPlacement::Above
                    ? bar.top() - kTooltipGap - kTooltipHeight
                    : bar.bottom() + 1 + kTooltipGap;
  const QRectF box(x, y, width, kTooltipHeight);
  painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
  painter.setBrush(QColor(18, 18, 22, 235));
  painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
  painter.setPen(QColor(QStringLiteral("#f5f5f7")));
  painter.drawText(box, Qt::AlignCenter, text);
}

void OverlayWindow::paintStrokes(QPainter &painter) {
  const qint64 current = now();
  painter.save();
  painter.setClipRect(localRect_);
  painter.translate(localRect_.topLeft());
  for (const Stroke &stroke : model_.strokes()) {
    paintStroke(painter, stroke, model_.opacity(stroke, current));
  }
  if (const Stroke *active = model_.activeStroke()) {
    paintStroke(painter, *active, 1.0);
  }
  painter.restore();
}

void OverlayWindow::paintStroke(QPainter &painter, const Stroke &stroke, qreal opacity) const {
  if (stroke.points.size() < 2 || opacity <= 0.0) {
    return;
  }
  painter.setOpacity(opacity);
  painter.setPen(QPen(stroke.color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  switch (stroke.tool) {
  case Tool::Pen:
  case Tool::Highlighter:
    painter.drawPath(smoothPolyline(stroke.points));
    break;
  case Tool::Rectangle:
    painter.drawRect(QRectF(stroke.points.first(), stroke.points.last()).normalized());
    break;
  case Tool::Arrow: {
    const QPointF start = stroke.points.first();
    const QPointF end = stroke.points.last();
    const QLineF line(start, end);
    if (line.length() < 1.0) {
      break;
    }
    const qreal angle = std::atan2(line.dy(), line.dx());
    const qreal headLength = std::max<qreal>(12.0, stroke.width * 4.0);
    const qreal halfWidth = headLength * 0.46;
    const QPointF direction(std::cos(angle), std::sin(angle));
    const QPointF perpendicular(-direction.y(), direction.x());
    const QPointF base = end - direction * headLength;
    painter.drawLine(start, end - direction * (headLength * 0.5));
    QPolygonF head;
    head << end << base + perpendicular * halfWidth << base - perpendicular * halfWidth;
    painter.setPen(Qt::NoPen);
    painter.setBrush(stroke.color);
    painter.drawPolygon(head);
    break;
  }
  }
  painter.setOpacity(1.0);
}

void OverlayWindow::paintToolbar(QPainter &painter) {
  const QColor text(QStringLiteral("#f5f5f7"));
  const QColor amber(QStringLiteral("#ff9f0a"));
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
    const Button button = entry.button;
    const bool active = (button == Button::Draw && state_.drawing()) ||
                        (button == Button::Hold && model_.hold()) ||
                        (button == Button::ToolPen && tool_ == Tool::Pen) ||
                        (button == Button::ToolArrow && tool_ == Tool::Arrow) ||
                        (button == Button::ToolRectangle && tool_ == Tool::Rectangle) ||
                        (button == Button::ToolHighlighter && tool_ == Tool::Highlighter);
    const bool swatch = button >= Button::Colour0 && button <= Button::Colour3;
    if (button == Button::Hold) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(active                   ? amber
                       : button == hovered_ ? QColor(255, 255, 255, 28)
                                            : QColor(255, 255, 255, 12));
      const QRectF pill = QRectF(entry.rect).adjusted(0, 5, 0, -5);
      painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
      painter.setPen(active ? QColor(18, 18, 22) : text);
      painter.setFont(neuchaFont(13));
      painter.drawText(pill, Qt::AlignCenter, QStringLiteral("Hold"));
      continue;
    }
    if (!swatch && (active || button == hovered_)) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(active ? QColor(255, 159, 10, 56) : QColor(255, 255, 255, 28));
      painter.drawRoundedRect(entry.rect, 8, 8);
    }
    const QColor color = active ? amber : text;
    const QRectF bounds(entry.rect);
    const QPointF c = bounds.center();
    switch (button) {
    case Button::Draw:
      drawToolbarIcon(painter, bounds, QStringLiteral("tool-freehand"), QString(), color);
      break;
    case Button::Hold:
      break;
    case Button::ToolPen:
    case Button::ToolArrow:
    case Button::ToolRectangle:
    case Button::ToolHighlighter: {
      const Tool tool = button == Button::ToolPen         ? Tool::Pen
                        : button == Button::ToolArrow     ? Tool::Arrow
                        : button == Button::ToolRectangle ? Tool::Rectangle
                                                          : Tool::Highlighter;
      drawToolbarIcon(painter, bounds, toolIcon(tool), QString(), color);
      break;
    }
    case Button::Colour0:
    case Button::Colour1:
    case Button::Colour2:
    case Button::Colour3: {
      const int index = static_cast<int>(button) - static_cast<int>(Button::Colour0);
      const bool selected = index == colour_;
      painter.setPen(QPen(selected ? text : QColor(255, 255, 255, 70), selected ? 2.0 : 1.0));
      painter.setBrush(QColor(QLatin1String(kStrokeColours[index])));
      const qreal inset = selected ? 1.0 : 2.5;
      painter.drawEllipse(bounds.adjusted(inset, inset, -inset, -inset));
      break;
    }
    case Button::Width: {
      const qreal diameter = std::min<qreal>(kStrokeWidths[width_] + 4.0, bounds.height() - 6.0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(text);
      painter.drawEllipse(c, diameter / 2.0, diameter / 2.0);
      break;
    }
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
  const Button button = buttonAt(event->pos());
  switch (button) {
  case Button::Draw:
    state_.toggleDrawing();
    break;
  case Button::Hold:
    model_.setHold(!model_.hold(), now());
    scheduleFade();
    update();
    break;
  case Button::Pause:
    state_.requestPause();
    break;
  case Button::Stop:
    state_.requestStop();
    break;
  case Button::ToolPen:
    setTool(Tool::Pen);
    break;
  case Button::ToolArrow:
    setTool(Tool::Arrow);
    break;
  case Button::ToolRectangle:
    setTool(Tool::Rectangle);
    break;
  case Button::ToolHighlighter:
    setTool(Tool::Highlighter);
    break;
  case Button::Colour0:
  case Button::Colour1:
  case Button::Colour2:
  case Button::Colour3:
    setColour(static_cast<int>(button) - static_cast<int>(Button::Colour0));
    break;
  case Button::Width:
    stepWidth(width_ + 1 < static_cast<int>(kStrokeWidths.size()) ? 1 : -width_);
    break;
  case Button::None:
    if (state_.drawing() && localRect_.contains(event->pos())) {
      setFocus();
      model_.begin(tool_, strokeColour(), strokeWidth(),
                   QPointF(event->pos() - localRect_.topLeft()));
      update();
    }
    break;
  }
  event->accept();
}

void OverlayWindow::mouseMoveEvent(QMouseEvent *event) {
  if (model_.active()) {
    model_.extend(QPointF(event->pos() - localRect_.topLeft()));
    update();
    return;
  }
  setHovered(buttonAt(event->pos()));
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || !model_.active()) {
    return;
  }
  model_.extend(QPointF(event->pos() - localRect_.topLeft()));
  model_.finish(now());
  scheduleFade();
  update();
  event->accept();
}

void OverlayWindow::wheelEvent(QWheelEvent *event) {
  if (!state_.drawing()) {
    return;
  }
  const int delta = event->angleDelta().y();
  if (delta != 0) {
    stepWidth(delta > 0 ? 1 : -1);
  }
  event->accept();
}

void OverlayWindow::keyPressEvent(QKeyEvent *event) {
  if (!state_.drawing()) {
    QWidget::keyPressEvent(event);
    return;
  }
  switch (event->key()) {
  case Qt::Key_Escape:
    state_.setDrawing(false);
    break;
  case Qt::Key_P:
    setTool(Tool::Pen);
    break;
  case Qt::Key_A:
    setTool(Tool::Arrow);
    break;
  case Qt::Key_R:
    setTool(Tool::Rectangle);
    break;
  case Qt::Key_H:
    setTool(Tool::Highlighter);
    break;
  case Qt::Key_1:
  case Qt::Key_2:
  case Qt::Key_3:
  case Qt::Key_4:
    setColour(event->key() - Qt::Key_1);
    break;
  case Qt::Key_C:
    model_.clear();
    scheduleFade();
    update();
    break;
  case Qt::Key_L:
    model_.setHold(!model_.hold(), now());
    scheduleFade();
    update();
    break;
  default:
    QWidget::keyPressEvent(event);
    return;
  }
  event->accept();
}

void OverlayWindow::enterEvent(QEnterEvent *) {
  if (collapsed()) {
    expanded_ = true;
    applyMask();
    update();
  }
}

void OverlayWindow::leaveEvent(QEvent *) {
  setHovered(Button::None);
  if (toolbarLayout().placement == ToolbarPlacement::CornerPill && expanded_) {
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
