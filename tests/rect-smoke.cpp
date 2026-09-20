#include "rect-smoke.hpp"
#include "rect.hpp"

#include <QJsonObject>
#include <QTest>

void RectSmoke::parsesGeometryWithNegativeOffsets() {
  const auto rect = parseGeometry(QStringLiteral("800x400+100+-1400"));
  QVERIFY(rect.has_value());
  QCOMPARE(*rect, QRect(100, -1400, 800, 400));
  const auto both = parseGeometry(QStringLiteral("10x20-5-6"));
  QVERIFY(both.has_value());
  QCOMPARE(*both, QRect(-5, -6, 10, 20));
}

void RectSmoke::rejectsEmptyOrMalformedGeometry() {
  QVERIFY(!parseGeometry(QStringLiteral("0x400+1+1")).has_value());
  QVERIFY(!parseGeometry(QStringLiteral("800x400")).has_value());
  QVERIFY(!parseGeometry(QStringLiteral("800x400+a+b")).has_value());
  QVERIFY(!parseGeometry(QString()).has_value());
}

void RectSmoke::roundTripsGeometryAndJson() {
  const RegionRect region{QStringLiteral("DP-4"), QRect(100, -1400, 800, 400)};
  QCOMPARE(region.toGeometry(), QStringLiteral("800x400+100-1400"));
  QCOMPARE(parseGeometry(region.toGeometry()).value(), region.rect);
  const QJsonObject json = region.toJson();
  QCOMPARE(json.value(QStringLiteral("output")).toString(), QStringLiteral("DP-4"));
  QCOMPARE(json.value(QStringLiteral("y")).toInt(), -1400);
  QCOMPARE(json.value(QStringLiteral("w")).toInt(), 800);
}
