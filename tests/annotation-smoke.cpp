#include "annotation-smoke.hpp"
#include "annotation-model.hpp"
#include "overlay-state.hpp"
#include "overlay-window.hpp"

#include <QTest>
#include <QWindow>

namespace {

const QRect kOutput(0, -1504, 3440, 1440);
const QRect kRect(100, -1400, 800, 400);

void addStroke(AnnotationModel &model, qint64 finishedAt) {
  model.begin(Tool::Pen, Qt::red, 4.0, QPointF(0, 0));
  model.extend(QPointF(10, 10));
  model.finish(finishedAt);
}

QPoint inRect(const OverlayWindow &window, int x, int y) {
  return window.recordedRect().topLeft() + QPoint(x, y);
}

void drag(OverlayWindow &window, const QPoint &from, const QPoint &to) {
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, from);
  QTest::mouseMove(&window, (from + to) / 2);
  QTest::mouseMove(&window, to);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, to);
}

} // namespace

void AnnotationSmoke::fadesAndRemovesStrokes() {
  AnnotationModel model;
  addStroke(model, 0);
  QCOMPARE(model.strokes().size(), 1);
  const Stroke &stroke = model.strokes().first();
  QCOMPARE(model.opacity(stroke, 4999), 1.0);
  QVERIFY(!model.fading(4999));
  QCOMPARE(model.msUntilNextFade(4999), 1);
  QVERIFY(qAbs(model.opacity(stroke, 5350) - 0.5) < 0.01);
  QVERIFY(model.fading(5350));
  model.prune(5350);
  QCOMPARE(model.strokes().size(), 1);
  model.prune(5700);
  QVERIFY(model.strokes().isEmpty());
  QVERIFY(!model.fading(5700));
  QCOMPARE(model.msUntilNextFade(5700), -1);
}

void AnnotationSmoke::holdSuspendsFadeAndRestartsClock() {
  AnnotationModel model;
  addStroke(model, 0);
  model.setHold(true, 10);
  QCOMPARE(model.opacity(model.strokes().first(), 60000), 1.0);
  model.prune(60000);
  QCOMPARE(model.strokes().size(), 1);
  QVERIFY(!model.fading(60000));
  QCOMPARE(model.msUntilNextFade(60000), -1);

  model.setHold(false, 60000);
  QCOMPARE(model.opacity(model.strokes().first(), 64999), 1.0);
  QVERIFY(model.opacity(model.strokes().first(), 65350) < 1.0);
  model.prune(65700);
  QVERIFY(model.strokes().isEmpty());
}

void AnnotationSmoke::clearRemovesEverything() {
  AnnotationModel model;
  addStroke(model, 0);
  addStroke(model, 100);
  model.begin(Tool::Arrow, Qt::blue, 4.0, QPointF(1, 1));
  QCOMPARE(model.strokes().size(), 2);
  QVERIFY(model.active());
  model.clear();
  QVERIFY(model.strokes().isEmpty());
  QVERIFY(!model.active());

  model.begin(Tool::Rectangle, Qt::blue, 4.0, QPointF(1, 1));
  model.finish(5);
  QVERIFY2(model.strokes().isEmpty(), "a stroke that never moved is dropped");
}

void AnnotationSmoke::readsFadeSettingsFromEnvironment() {
  qputenv("OMARECORD_FADE_SECONDS", "2.5");
  qputenv("OMARECORD_FADE_DURATION", "bogus");
  const FadeSettings settings = fadeSettingsFromEnvironment();
  qunsetenv("OMARECORD_FADE_SECONDS");
  qunsetenv("OMARECORD_FADE_DURATION");
  QCOMPARE(settings.fadeAfterMs, 2500);
  QCOMPARE(settings.fadeDurationMs, 700);
}

void AnnotationSmoke::drawsStrokeInsideRect() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  state.setDrawing(true);

  drag(window, inRect(window, 50, 60), inRect(window, 250, 160));
  QCOMPARE(window.model().strokes().size(), 1);
  const Stroke &pen = window.model().strokes().first();
  QCOMPARE(pen.tool, Tool::Pen);
  QCOMPARE(pen.points.first(), QPointF(50, 60));
  QCOMPARE(pen.points.last(), QPointF(250, 160));
  QCOMPARE(pen.color, window.colour());

  QTest::keyClick(&window, Qt::Key_A);
  QCOMPARE(window.tool(), Tool::Arrow);
  drag(window, inRect(window, 10, 10), inRect(window, 300, 300));
  QCOMPARE(window.model().strokes().size(), 2);
  const Stroke &arrow = window.model().strokes().last();
  QCOMPARE(arrow.tool, Tool::Arrow);
  QCOMPARE(arrow.points.size(), 2);
  QCOMPARE(arrow.points.last(), QPointF(300, 300));

  QTest::keyClick(&window, Qt::Key_H);
  drag(window, inRect(window, 10, 10), inRect(window, 100, 10));
  const Stroke &highlight = window.model().strokes().last();
  QCOMPARE(highlight.tool, Tool::Highlighter);
  QCOMPARE(highlight.color.alpha(), kHighlighterAlpha);
  QVERIFY(highlight.width > kStrokeWidths.back());
}

void AnnotationSmoke::ignoresPressOutsideRect() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  drag(window, inRect(window, 50, 60), inRect(window, 250, 160));
  QVERIFY2(window.model().strokes().isEmpty(), "no strokes while draw mode is off");

  state.setDrawing(true);
  const QPoint outside = window.recordedRect().bottomRight() + QPoint(40, 40);
  drag(window, outside, outside + QPoint(100, 100));
  QVERIFY(window.model().strokes().isEmpty());
}

void AnnotationSmoke::escapeLeavesDrawMode() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  state.setDrawing(true);
  QTest::keyClick(&window, Qt::Key_Escape);
  QVERIFY(!state.drawing());
  QTest::keyClick(&window, Qt::Key_Escape);
  QVERIFY(!state.drawing());
}

void AnnotationSmoke::keysChangeToolColourAndHold() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QTest::keyClick(&window, Qt::Key_3);
  QCOMPARE(window.colourIndex(), 0);

  state.setDrawing(true);
  for (int key = Qt::Key_1; key <= Qt::Key_4; ++key) {
    QTest::keyClick(&window, static_cast<Qt::Key>(key));
    QCOMPARE(window.colourIndex(), key - Qt::Key_1);
    QCOMPARE(window.colour(), QColor(QLatin1String(kStrokeColours[key - Qt::Key_1])));
  }
  QTest::keyClick(&window, Qt::Key_R);
  QCOMPARE(window.tool(), Tool::Rectangle);
  QTest::keyClick(&window, Qt::Key_P);
  QCOMPARE(window.tool(), Tool::Pen);

  QTest::keyClick(&window, Qt::Key_L);
  QVERIFY(window.model().hold());
  drag(window, inRect(window, 10, 10), inRect(window, 20, 20));
  QCOMPARE(window.model().strokes().size(), 1);
  QVERIFY(!window.fadeTimerActive());
  QTest::keyClick(&window, Qt::Key_C);
  QVERIFY(window.model().strokes().isEmpty());
  QTest::keyClick(&window, Qt::Key_L);
  QVERIFY(!window.model().hold());
}

void AnnotationSmoke::masksToolbarAndRectWhileDrawing() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  const QRect idleBar = window.toolbarLayout().rect;
  QCOMPARE(window.windowHandle()->mask(), QRegion(idleBar));

  state.setDrawing(true);
  const QRect drawingBar = window.toolbarLayout().rect;
  QVERIFY(drawingBar.width() > idleBar.width());
  QCOMPARE(window.windowHandle()->mask(), QRegion(drawingBar) + window.recordedRect());
  QVERIFY(window.windowHandle()->mask().contains(window.recordedRect().center()));
  QVERIFY(window.mask().isEmpty());

  state.setDrawing(false);
  QCOMPARE(window.windowHandle()->mask(), QRegion(idleBar));
  QVERIFY(!window.windowHandle()->mask().contains(window.recordedRect().center()));
}

void AnnotationSmoke::ticksOnlyWhileFading() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput, FadeSettings{40, 100});
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(!window.fadeTimerActive());
  state.setDrawing(true);
  drag(window, inRect(window, 10, 10), inRect(window, 20, 20));
  QVERIFY2(!window.fadeTimerActive(), "no periodic tick before the fade starts");
  QTRY_VERIFY(window.fadeTimerActive());
  QTRY_VERIFY(window.model().strokes().isEmpty());
  QTRY_VERIFY(!window.fadeTimerActive());
}

void AnnotationSmoke::showsTooltipAfterHover() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  const QRect bar = window.toolbarLayout().rect;
  QTest::mouseMove(&window, QPoint(bar.right() - 12, bar.center().y()));
  QVERIFY(window.visibleTooltip().isEmpty());
  QTRY_COMPARE(window.visibleTooltip(), QStringLiteral("Stop"));
  QTest::mouseMove(&window, window.recordedRect().center());
  QVERIFY(window.visibleTooltip().isEmpty());
}
