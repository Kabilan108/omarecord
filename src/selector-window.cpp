#include "selector-window.hpp"

#include <LayerShellQt/Window>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTextStream>
#include <QWindow>

namespace {

constexpr int kBackdropAlpha = 90;

QFont readoutFont() {
  static const int fontId =
      QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Neucha.ttf"));
  QFont font(fontId >= 0 ? QStringLiteral("Neucha") : QStringLiteral("Sans"));
  font.setPixelSize(18);
  return font;
}

QScreen *screenNamed(const QString &name) {
  for (QScreen *screen : QGuiApplication::screens()) {
    if (screen->name() == name)
      return screen;
  }
  return nullptr;
}

} // namespace

SelectorWindow::SelectorWindow(OutputInfo output, QWidget *parent)
    : QWidget(parent), output_(std::move(output)) {
  setAttribute(Qt::WA_TranslucentBackground);
  setMouseTracking(true);
  setCursor(Qt::CrossCursor);
  setFocusPolicy(Qt::StrongFocus);
  resize(output_.logical.size());
}

bool SelectorWindow::attachLayerShell() {
  QScreen *screen = screenNamed(output_.name);
  if (!screen)
    return false;
  setScreen(screen);
  setGeometry(screen->geometry());
  winId();
  QWindow *window = windowHandle();
  LayerShellQt::Window *layer =
      window ? LayerShellQt::Window::get(window) : nullptr;
  if (!layer)
    return false;
  layer->setScope(QStringLiteral("omarecord-select"));
  layer->setScreen(screen);
  layer->setLayer(LayerShellQt::Window::LayerOverlay);
  layer->setAnchors({LayerShellQt::Window::AnchorTop,
                     LayerShellQt::Window::AnchorBottom,
                     LayerShellQt::Window::AnchorLeft,
                     LayerShellQt::Window::AnchorRight});
  layer->setExclusiveZone(-1);
  layer->setKeyboardInteractivity(
      LayerShellQt::Window::KeyboardInteractivityExclusive);
  layer->setActivateOnShow(true);
  return true;
}

QPoint SelectorWindow::toGlobal(const QPointF &local) const {
  return local.toPoint() + output_.logical.topLeft();
}

QRect SelectorWindow::clampedSelection() const {
  const QRect bounds(QPoint(0, 0), output_.logical.size());
  return selection_.toRect().intersected(bounds);
}

void SelectorWindow::confirm(const QRect &localRect) {
  if (done_)
    return;
  done_ = true;
  result_ = RegionRect{output_.name,
                       localRect.translated(output_.logical.topLeft())};
  emit finished();
}

void SelectorWindow::cancel() {
  if (done_)
    return;
  done_ = true;
  cancelled_ = true;
  emit finished();
}

void SelectorWindow::mousePressEvent(QMouseEvent *event) {
  if (done_)
    return;
  cursor_ = event->position();
  if (event->button() == Qt::RightButton) {
    dragging_ = false;
    selection_ = {};
    update();
    return;
  }
  if (event->button() != Qt::LeftButton)
    return;
  dragStart_ = cursor_;
  selection_ = {};
  dragging_ = true;
  update();
}

void SelectorWindow::mouseMoveEvent(QMouseEvent *event) {
  cursor_ = event->position();
  if (dragging_)
    selection_ = QRectF(dragStart_, cursor_).normalized();
  update();
}

void SelectorWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (done_ || event->button() != Qt::LeftButton || !dragging_)
    return;
  cursor_ = event->position();
  selection_ = QRectF(dragStart_, cursor_).normalized();
  dragging_ = false;
  const QRect local = clampedSelection();
  if (local.width() >= kMinimumRegionSide &&
      local.height() >= kMinimumRegionSide) {
    confirm(local);
    return;
  }
  selection_ = {};
  update();
}

void SelectorWindow::keyPressEvent(QKeyEvent *event) {
  if (done_)
    return;
  if (event->key() == Qt::Key_Escape) {
    cancel();
    return;
  }
  if (event->matches(QKeySequence::SelectAll)) {
    confirm(QRect(QPoint(0, 0), output_.logical.size()));
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    const QRect local = clampedSelection();
    if (!dragging_ && local.width() >= kMinimumRegionSide &&
        local.height() >= kMinimumRegionSide)
      confirm(local);
    return;
  }
  if (event->key() == Qt::Key_Space) {
    QTextStream(stderr)
        << "omarecord select: window pick (Space) is not implemented yet\n";
    return;
  }
  QWidget::keyPressEvent(event);
}

void SelectorWindow::drawReadout(QPainter &painter, const QPointF &anchor,
                                 const QString &text) const {
  painter.setFont(readoutFont());
  const QSizeF textSize =
      painter.fontMetrics().size(Qt::TextSingleLine, text);
  QRectF box(anchor + QPointF(14, 14), textSize + QSizeF(16, 8));
  // Keep the badge on the output when the pointer sits near the far edges.
  if (box.right() > width())
    box.moveRight(anchor.x() - 14);
  if (box.bottom() > height())
    box.moveBottom(anchor.y() - 14);
  painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
  painter.setBrush(QColor(18, 18, 22, 235));
  painter.drawRoundedRect(box, 6, 6);
  painter.setPen(Qt::white);
  painter.drawText(box, Qt::AlignCenter, text);
}

void SelectorWindow::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.fillRect(rect(), QColor(0, 0, 0, kBackdropAlpha));
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

  const QRect local = clampedSelection();
  if (!local.isEmpty()) {
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(local, Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(local).adjusted(0.5, 0.5, -0.5, -0.5));
  }

  if (!dragging_) {
    painter.setPen(QPen(QColor(255, 255, 255, 56), 1));
    painter.drawLine(QPointF(cursor_.x(), 0), QPointF(cursor_.x(), height()));
    painter.drawLine(QPointF(0, cursor_.y()), QPointF(width(), cursor_.y()));
  }

  const QPoint global = toGlobal(cursor_);
  const QString text =
      dragging_
          ? QStringLiteral("%1 × %2").arg(local.width()).arg(local.height())
          : QStringLiteral("%1, %2").arg(global.x()).arg(global.y());
  drawReadout(painter, cursor_, text);
}
