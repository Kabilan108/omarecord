#include "niri-smoke.hpp"
#include "niri.hpp"

#include <QTest>
#include <climits>

namespace {
const QByteArray kOutputs = R"({
  "eDP-1": {"name": "eDP-1", "logical": {"x": 0, "y": 0, "width": 2256, "height": 1504, "scale": 1.0}},
  "DP-4": {"name": "DP-4", "logical": {"x": 0, "y": -1504, "width": 3440, "height": 1440, "scale": 1.0}},
  "HDMI-A-1": {"name": "HDMI-A-1", "logical": null}
})";
}

void NiriSmoke::parsesOutputsWithNegativeOrigin() {
  QString error;
  const QList<OutputInfo> outputs = parseOutputs(kOutputs, error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(outputs.size(), 2);
  bool sawDp4 = false;
  for (const OutputInfo &output : outputs) {
    if (output.name == QStringLiteral("DP-4")) {
      sawDp4 = true;
      QCOMPARE(output.logical, QRect(0, -1504, 3440, 1440));
    }
  }
  QVERIFY(sawDp4);
}

void NiriSmoke::skipsDisabledOutputs() {
  QString error;
  for (const OutputInfo &output : parseOutputs(kOutputs, error)) {
    QVERIFY(output.name != QStringLiteral("HDMI-A-1"));
  }
}

void NiriSmoke::findsOutputContainingPoint() {
  QString error;
  const QList<OutputInfo> outputs = parseOutputs(kOutputs, error);
  QCOMPARE(outputContaining(outputs, QPoint(100, -1400)).value().name, QStringLiteral("DP-4"));
  QCOMPARE(outputContaining(outputs, QPoint(100, 100)).value().name, QStringLiteral("eDP-1"));
  QVERIFY(!outputContaining(outputs, QPoint(5000, 5000)).has_value());
}

void NiriSmoke::focusedWindowWithoutTilePositionHasSizeOnly() {
  QString error;
  const QByteArray json = R"({"id":4,"title":"T3","app_id":"t3","layout":{"window_size":[1711,1400],"tile_pos_in_workspace_view":null}})";
  const auto window = parseFocusedWindow(json, parseOutputs(kOutputs, error), error);
  QVERIFY2(window.has_value(), qPrintable(error));
  QCOMPARE(window->rect.size(), QSize(1711, 1400));
  QCOMPARE(window->rect.topLeft(), QPoint(INT_MIN, INT_MIN));
}

void NiriSmoke::focusedWindowNullIsAnError() {
  QString error;
  QVERIFY(!parseFocusedWindow("null", {}, error).has_value());
  QVERIFY(!error.isEmpty());
}
