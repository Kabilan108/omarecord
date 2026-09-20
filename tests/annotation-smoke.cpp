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

void AnnotationSmoke::hitTestsStrokesByTool() {
  AnnotationModel model;
  model.begin(Tool::Pen, Qt::red, 4.0, QPointF(0, 0));
  model.extend(QPointF(100, 0));
  model.extend(QPointF(100, 100));
  model.finish(0);
  QCOMPARE(model.hitTest(QPointF(50, 5)), std::optional<int>(0));
  QCOMPARE(model.hitTest(QPointF(103, 60)), std::optional<int>(0));
  QVERIFY(!model.hitTest(QPointF(50, 20)));
  QVERIFY(!model.hitTest(QPointF(80, 60)));

  model.begin(Tool::Arrow, Qt::blue, 4.0, QPointF(200, 200));
  model.extend(QPointF(300, 200));
  model.finish(0);
  QCOMPARE(model.hitTest(QPointF(250, 206)), std::optional<int>(1));
  QVERIFY(!model.hitTest(QPointF(250, 220)));

  model.begin(Tool::Rectangle, Qt::green, 4.0, QPointF(400, 400));
  model.extend(QPointF(500, 480));
  model.finish(0);
  QCOMPARE(model.hitTest(QPointF(450, 403)), std::optional<int>(2));
  QCOMPARE(model.hitTest(QPointF(497, 440)), std::optional<int>(2));
  QVERIFY2(!model.hitTest(QPointF(450, 440)), "rectangle interior is not a hit");

  model.begin(Tool::Highlighter, Qt::yellow, 24.0, QPointF(600, 600));
  model.extend(QPointF(700, 600));
  model.finish(0);
  QCOMPARE(model.hitTest(QPointF(650, 615)), std::optional<int>(3));
  QVERIFY(!model.hitTest(QPointF(650, 620)));
}

void AnnotationSmoke::hitTestPrefersLatestStroke() {
  AnnotationModel model;
  addStroke(model, 0);
  addStroke(model, 1);
  QCOMPARE(model.hitTest(QPointF(5, 5)), std::optional<int>(1));
  model.removeLast();
  QCOMPARE(model.hitTest(QPointF(5, 5)), std::optional<int>(0));
}

void AnnotationSmoke::movesAndRemovesSelectedStroke() {
  AnnotationModel model;
  addStroke(model, 0);
  addStroke(model, 0);
  model.select(0);
  QCOMPARE(model.selected(), std::optional<int>(0));
  model.moveSelected(QPointF(30, 20), 100);
  QCOMPARE(model.strokes().at(0).points.first(), QPointF(30, 20));
  QCOMPARE(model.strokes().at(0).points.last(), QPointF(40, 30));
  QCOMPARE(model.strokes().at(0).finishedAt, 100);
  QCOMPARE(model.strokes().at(1).points.first(), QPointF(0, 0));

  model.removeSelected();
  QCOMPARE(model.strokes().size(), 1);
  QVERIFY(!model.selected());
  model.removeSelected();
  QCOMPARE(model.strokes().size(), 1);

  model.select(0);
  model.removeLast();
  QVERIFY(model.strokes().isEmpty());
  QVERIFY(!model.selected());
  model.removeLast();
}

void AnnotationSmoke::selectedStrokeDoesNotFade() {
  AnnotationModel model;
  addStroke(model, 0);
  model.select(0);
  QCOMPARE(model.opacity(model.strokes().first(), 60000), 1.0);
  QVERIFY(!model.fading(60000));
  QCOMPARE(model.msUntilNextFade(60000), -1);
  model.prune(60000);
  QCOMPARE(model.strokes().size(), 1);
  QCOMPARE(model.selected(), std::optional<int>(0));

  model.deselect(60000);
  QVERIFY(!model.selected());
  QCOMPARE(model.opacity(model.strokes().first(), 64999), 1.0);
  QVERIFY(model.opacity(model.strokes().first(), 65350) < 1.0);
  model.prune(65700);
  QVERIFY(model.strokes().isEmpty());
}

void AnnotationSmoke::selectToolDrivesSelectionFromInput() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  state.setDrawing(true);
  drag(window, inRect(window, 50, 60), inRect(window, 250, 60));
  QCOMPARE(window.model().strokes().size(), 1);

  QTest::keyClick(&window, Qt::Key_S);
  QCOMPARE(window.tool(), Tool::Select);
  QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, inRect(window, 150, 62));
  QCOMPARE(window.model().selected(), std::optional<int>(0));
  QCOMPARE(window.model().strokes().size(), 1);

  drag(window, inRect(window, 150, 62), inRect(window, 180, 82));
  QVERIFY(!window.draggingSelection());
  QCOMPARE(window.model().strokes().first().points.first(), QPointF(80, 80));
  QCOMPARE(window.model().strokes().first().points.last(), QPointF(280, 80));
  QCOMPARE(window.model().selected(), std::optional<int>(0));

  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, inRect(window, 150, 300));
  QVERIFY2(!window.model().selected(), "click on empty space deselects");
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, inRect(window, 150, 82));
  QCOMPARE(window.model().selected(), std::optional<int>(0));

  QTest::keyClick(&window, Qt::Key_Escape);
  QVERIFY(state.drawing());
  QVERIFY(!window.model().selected());
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, inRect(window, 150, 82));
  QCOMPARE(window.model().selected(), std::optional<int>(0));
  QTest::keyClick(&window, Qt::Key_P);
  QVERIFY2(!window.model().selected(), "switching tool clears the selection");

  QTest::keyClick(&window, Qt::Key_S);
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, inRect(window, 150, 82));
  QTest::keyClick(&window, Qt::Key_Delete);
  QVERIFY(window.model().strokes().isEmpty());

  QTest::keyClick(&window, Qt::Key_P);
  drag(window, inRect(window, 10, 10), inRect(window, 20, 20));
  drag(window, inRect(window, 30, 30), inRect(window, 40, 40));
  QCOMPARE(window.model().strokes().size(), 2);
  QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(window.model().strokes().size(), 1);
  QCOMPARE(window.model().strokes().first().points.first(), QPointF(10, 10));

  QTest::keyClick(&window, Qt::Key_Escape);
  QVERIFY(!state.drawing());
}

void AnnotationSmoke::toolbarShowsSelectButton() {
  OverlayState state;
  OverlayWindow window(state, kRect, kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(window.toolbarItemRect(QStringLiteral("Select (S)")).isNull());
  state.setDrawing(true);
  const QRect button = window.toolbarItemRect(QStringLiteral("Select (S)"));
  QVERIFY(!button.isNull());
  QVERIFY(window.toolbarLayout().rect.contains(button));
  QTest::mouseMove(&window, button.center());
  QTRY_COMPARE(window.visibleTooltip(), QStringLiteral("Select (S)"));
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, button.center());
  QCOMPARE(window.tool(), Tool::Select);
}
