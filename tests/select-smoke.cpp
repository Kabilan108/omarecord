#include "select-smoke.hpp"
#include "selector-window.hpp"

#include <QSignalSpy>
#include <QTest>

namespace {
OutputInfo fakeOutput() {
  return OutputInfo{QStringLiteral("DP-4"), QRect(0, -1504, 3440, 1440), 1.0};
}
} // namespace

void SelectSmoke::dragPrintsGlobalRect() {
  SelectorWindow selector(fakeOutput());
  QSignalSpy finished(&selector, &SelectorWindow::finished);
  QTest::mousePress(&selector, Qt::LeftButton, {}, QPoint(100, 104));
  QTest::mouseMove(&selector, QPoint(500, 300));
  QVERIFY(!selector.result().has_value());
  QTest::mouseRelease(&selector, Qt::LeftButton, {}, QPoint(900, 504));
  QCOMPARE(finished.count(), 1);
  QVERIFY(!selector.cancelled());
  const auto region = selector.result();
  QVERIFY(region.has_value());
  QCOMPARE(region->output, QStringLiteral("DP-4"));
  QCOMPARE(region->rect, QRect(100, -1400, 800, 400));
}

void SelectSmoke::escapeCancels() {
  SelectorWindow selector(fakeOutput());
  QSignalSpy finished(&selector, &SelectorWindow::finished);
  QTest::keyClick(&selector, Qt::Key_Escape);
  QCOMPARE(finished.count(), 1);
  QVERIFY(selector.cancelled());
  QVERIFY(!selector.result().has_value());
}

void SelectSmoke::tinyDragKeepsWaiting() {
  SelectorWindow selector(fakeOutput());
  QSignalSpy finished(&selector, &SelectorWindow::finished);
  QTest::mousePress(&selector, Qt::LeftButton, {}, QPoint(10, 10));
  QTest::mouseRelease(&selector, Qt::LeftButton, {}, QPoint(15, 15));
  QCOMPARE(finished.count(), 0);
  QVERIFY(!selector.result().has_value());
  QTest::keyClick(&selector, Qt::Key_Return);
  QCOMPARE(finished.count(), 0);
}

void SelectSmoke::selectAllTakesWholeOutput() {
  SelectorWindow selector(fakeOutput());
  QTest::keyClick(&selector, Qt::Key_A, Qt::ControlModifier);
  const auto region = selector.result();
  QVERIFY(region.has_value());
  QCOMPARE(region->rect, QRect(0, -1504, 3440, 1440));
}
