#include "overlay-smoke.hpp"
#include "overlay-protocol.hpp"
#include "overlay-state.hpp"
#include "overlay-window.hpp"

#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

namespace {
const QRect kOutput(0, -1504, 3440, 1440);
const QSize kToolbar(200, 36);
} // namespace

void OverlaySmoke::placesToolbarAboveWhenThereIsRoom() {
  const ToolbarLayout layout =
      computeToolbarLayout(QRect(100, -1400, 800, 400), kOutput, kToolbar);
  QCOMPARE(layout.placement, ToolbarPlacement::Above);
  QCOMPARE(layout.rect.bottom() + 1, 104 - kBorderWidth - kToolbarGap);
  QCOMPARE(layout.rect.left(), 400);
  QCOMPARE(layout.collapsed, layout.rect);
}

void OverlaySmoke::placesToolbarBelowWhenRectTouchesTop() {
  const ToolbarLayout layout =
      computeToolbarLayout(QRect(100, -1504, 800, 400), kOutput, kToolbar);
  QCOMPARE(layout.placement, ToolbarPlacement::Below);
  QCOMPARE(layout.rect.top(), 400 + kBorderWidth + kToolbarGap);
}

void OverlaySmoke::collapsesToCornerPillWhenRectCoversOutput() {
  const ToolbarLayout layout = computeToolbarLayout(kOutput, kOutput, kToolbar);
  QCOMPARE(layout.placement, ToolbarPlacement::CornerPill);
  QCOMPARE(layout.collapsed.size(), QSize(kCornerPillSize, kCornerPillSize));
  QCOMPARE(layout.collapsed.right() + 1, kOutput.width() - kCornerMargin);
  QCOMPARE(layout.rect.right(), layout.collapsed.right());
}

void OverlaySmoke::appliesInboundCommands() {
  OverlayState state;
  QSignalSpy changed(&state, &OverlayState::changed);
  state.applyCommand(*parseCommandLine("{\"cmd\":\"paused\",\"value\":true}"));
  QVERIFY(state.paused());
  state.applyCommand(*parseCommandLine("{\"cmd\":\"elapsed\",\"seconds\":65}"));
  QCOMPARE(state.elapsedSeconds(), 65);
  QCOMPARE(formatElapsed(state.elapsedSeconds()), QStringLiteral("01:05"));
  QCOMPARE(changed.count(), 2);
  QVERIFY(!state.quitRequested());
  state.applyCommand(*parseCommandLine("{\"cmd\":\"quit\"}\n"));
  QVERIFY(state.quitRequested());
}

void OverlaySmoke::ignoresMalformedLines() {
  QVERIFY(!parseCommandLine("not json").has_value());
  QVERIFY(!parseCommandLine("[1,2]").has_value());
  QVERIFY(!parseCommandLine("").has_value());
  OverlayState state;
  state.applyCommand(*parseCommandLine("{\"cmd\":\"paused\",\"value\":\"yes\"}"));
  QVERIFY(!state.paused());
  state.applyCommand(*parseCommandLine("{\"cmd\":\"elapsed\"}"));
  QCOMPARE(state.elapsedSeconds(), 0);
  state.applyCommand(*parseCommandLine("{\"cmd\":\"bogus\"}"));
  QVERIFY(!state.quitRequested());
}

void OverlaySmoke::roundTripsOverSocket() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("overlay.sock"));
  OverlayState state;
  OverlayProtocol protocol;
  QString error;
  QVERIFY2(protocol.listen(path, error), qUtf8Printable(error));
  connect(&protocol, &OverlayProtocol::commandReceived, &state, &OverlayState::applyCommand);
  connect(&state, &OverlayState::eventRequested, &protocol, &OverlayProtocol::sendEvent);

  QLocalSocket client;
  client.connectToServer(path);
  QVERIFY(client.waitForConnected(2000));
  QTRY_VERIFY(protocol.hasClient());
  client.write("{\"cmd\":\"elapsed\",\"seconds\":65}\nnot json\n{\"cmd\":\"paused\",\"value\":true}\n");
  client.flush();
  QTRY_COMPARE(state.elapsedSeconds(), 65);
  QTRY_VERIFY(state.paused());

  state.requestPause();
  QVERIFY(client.waitForReadyRead(2000));
  QCOMPARE(client.readLine(), QByteArray("{\"event\":\"pause\"}\n"));
  state.requestStop();
  QVERIFY(client.waitForReadyRead(2000));
  QCOMPARE(client.readLine(), QByteArray("{\"event\":\"stop\"}\n"));

  client.disconnectFromServer();
  QTRY_VERIFY(!protocol.hasClient());
}

void OverlaySmoke::windowMasksToolbarOnly() {
  OverlayState state;
  OverlayWindow window(state, QRect(100, -1400, 800, 400), kOutput);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  const ToolbarLayout layout = window.toolbarLayout();
  QCOMPARE(layout.placement, ToolbarPlacement::Above);
  QCOMPARE(window.windowHandle()->mask(), QRegion(layout.rect));
  QVERIFY(window.mask().isEmpty());
  QCOMPARE(window.inputRect(), layout.rect);
  QVERIFY(!window.windowHandle()->mask().contains(QPoint(500, 200)));
}
