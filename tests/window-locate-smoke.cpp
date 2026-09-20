#include "window-locate-smoke.hpp"
#include "niri.hpp"
#include "window-locate.hpp"

#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QTest>
#include <QtLogging>

namespace {

constexpr int kOutputWidth = 3440;
constexpr int kOutputHeight = 1440;
constexpr QSize kWindowSize(1711, 1400);
constexpr QPoint kWindowOffset(6, 6);
constexpr int kTimeBudgetMs = 300;

QImage noise(int width, int height, quint32 seed) {
  QRandomGenerator random(seed);
  QImage image(width, height, QImage::Format_RGB32);
  for (int y = 0; y < height; ++y) {
    auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < width; ++x)
      row[x] = 0xff000000u | (random.generate() & 0x00ffffffu);
  }
  return image;
}

/** A window-like image: flat panels with text-ish speckle, so the sample
 * chooser has real edges to find rather than pure noise. */
QImage windowContent(quint32 seed) {
  QImage image = noise(kWindowSize.width(), kWindowSize.height(), seed);
  QPainter painter(&image);
  painter.fillRect(QRect(0, 0, kWindowSize.width(), 40), QColor(40, 40, 60));
  painter.fillRect(QRect(0, 40, 300, kWindowSize.height() - 40),
                   QColor(28, 28, 40));
  painter.fillRect(QRect(300, 40, kWindowSize.width() - 300, 900),
                   QColor(245, 245, 250));
  return image;
}

QImage haystackWith(const QImage &window, const QPoint &at) {
  QImage output = noise(kOutputWidth, kOutputHeight, 7);
  QPainter painter(&output);
  painter.drawImage(at, window);
  return output;
}

} // namespace

void WindowLocateSmoke::findsExactNeedle() {
  const QImage window = windowContent(1);
  const QImage output = haystackWith(window, kWindowOffset);
  QElapsedTimer timer;
  timer.start();
  const auto found = locateSubImage(output, window, 0.02);
  const qint64 ms = timer.elapsed();
  QVERIFY(found.has_value());
  QCOMPARE(*found, kWindowOffset);
  qInfo("locate took %lld ms (budget %d)", static_cast<long long>(ms),
        kTimeBudgetMs);
  QVERIFY2(ms < kTimeBudgetMs,
           qPrintable(QStringLiteral("locate took %1 ms").arg(ms)));
}

void WindowLocateSmoke::findsPerturbedNeedle() {
  const QImage window = windowContent(2);
  const QImage output = haystackWith(window, QPoint(1723, 34));
  QImage perturbed = window;
  QRandomGenerator random(99);
  const int count = perturbed.width() * perturbed.height() / 100;
  for (int i = 0; i < count; ++i) {
    const int x = random.bounded(perturbed.width());
    const int y = random.bounded(perturbed.height());
    perturbed.setPixel(x, y, 0xff000000u | (random.generate() & 0xffffffu));
  }
  // A cursor-sized opaque blob on top of the content as well.
  QPainter painter(&perturbed);
  painter.fillRect(QRect(800, 700, 24, 32), Qt::red);
  QElapsedTimer timer;
  timer.start();
  const auto found = locateSubImage(output, perturbed, 0.02);
  const qint64 ms = timer.elapsed();
  QVERIFY(found.has_value());
  QCOMPARE(*found, QPoint(1723, 34));
  qInfo("locate took %lld ms (budget %d)", static_cast<long long>(ms),
        kTimeBudgetMs);
  QVERIFY2(ms < kTimeBudgetMs,
           qPrintable(QStringLiteral("locate took %1 ms").arg(ms)));
}

void WindowLocateSmoke::findsTranslucentNeedle() {
  // Niri's PNG of an opacity-0.85 window holds the premultiplied render
  // (checked against a real capture); the compositor then adds whatever lies
  // behind it, scaled by 1 - alpha. The haystack therefore differs from the
  // needle by an unknown per-pixel addition bounded by 255 - alpha.
  QImage window = windowContent(3).convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < window.height(); ++y) {
    auto *row = reinterpret_cast<QRgb *>(window.scanLine(y));
    for (int x = 0; x < window.width(); ++x)
      row[x] = qRgba(qRed(row[x]) * 217 / 255, qGreen(row[x]) * 217 / 255,
                     qBlue(row[x]) * 217 / 255, 217);
  }
  QImage output = noise(kOutputWidth, kOutputHeight, 11);
  const QPoint at(1723, 34);
  for (int y = 0; y < window.height(); ++y) {
    const auto *src = reinterpret_cast<const QRgb *>(window.scanLine(y));
    auto *dst = reinterpret_cast<QRgb *>(output.scanLine(at.y() + y)) + at.x();
    for (int x = 0; x < window.width(); ++x) {
      const int rest = 255 - qAlpha(src[x]);
      dst[x] = qRgb(qRed(src[x]) + qRed(dst[x]) * rest / 255,
                    qGreen(src[x]) + qGreen(dst[x]) * rest / 255,
                    qBlue(src[x]) + qBlue(dst[x]) * rest / 255);
    }
  }
  const auto found = locateSubImage(output, window, 0.02);
  QVERIFY(found.has_value());
  QCOMPARE(*found, QPoint(1723, 34));
}

void WindowLocateSmoke::rejectsAbsentNeedle() {
  const QImage window = windowContent(4);
  const QImage output = noise(kOutputWidth, kOutputHeight, 5);
  QElapsedTimer timer;
  timer.start();
  const auto found = locateSubImage(output, window, 0.02);
  const qint64 ms = timer.elapsed();
  QVERIFY(!found.has_value());
  qInfo("locate took %lld ms (budget %d)", static_cast<long long>(ms),
        kTimeBudgetMs);
  QVERIFY2(ms < kTimeBudgetMs,
           qPrintable(QStringLiteral("locate took %1 ms").arg(ms)));
}

void WindowLocateSmoke::geometryInsideShadowedBuffer() {
  QImage buffer(1743, 1442, QImage::Format_ARGB32);
  buffer.fill(Qt::transparent);
  QPainter painter(&buffer);
  painter.fillRect(QRect(10, 4, 1723, 1420), QColor(0, 0, 0, 60));
  painter.fillRect(QRect(16, 10, 1711, 1400), QColor(31, 20, 57, 255));
  painter.end();
  const auto geometry = windowGeometryInBuffer(buffer, QSize(1711, 1400));
  QVERIFY(geometry.has_value());
  QCOMPARE(*geometry, QRect(16, 10, 1711, 1400));
  QVERIFY(!windowGeometryInBuffer(buffer, QSize(1000, 1000)).has_value());
}

void WindowLocateSmoke::geometryOfBareBuffer() {
  QImage buffer(1693, 1400, QImage::Format_ARGB32);
  buffer.fill(QColor(25, 25, 39, 217));
  const auto geometry = windowGeometryInBuffer(buffer, QSize(1693, 1400));
  QVERIFY(geometry.has_value());
  QCOMPARE(*geometry, QRect(0, 0, 1693, 1400));
}

void WindowLocateSmoke::parsesWindowAndWorkspaces() {
  QString error;
  const QByteArray tiled =
      R"({"id":4,"title":"T3","app_id":"t3","workspace_id":2,"is_floating":false,)"
      R"("layout":{"window_size":[1711,1400],"tile_pos_in_workspace_view":null}})";
  const auto window = parseWindow(tiled, error);
  QVERIFY2(window.has_value(), qPrintable(error));
  QCOMPARE(window->id, 4);
  QCOMPARE(window->workspaceId, 2);
  QCOMPARE(window->size, QSize(1711, 1400));
  QVERIFY(!window->positionOnOutput.has_value());

  const QByteArray floating =
      R"({"id":12,"title":"Agent","app_id":"ghostty","workspace_id":2,)"
      R"("layout":{"window_size":[1032,1129],"tile_pos_in_workspace_view":[2344.0,247.0]}})";
  const auto floater = parseWindow(floating, error);
  QVERIFY2(floater.has_value(), qPrintable(error));
  QCOMPARE(floater->positionOnOutput.value(), QPoint(2344, 247));

  QVERIFY(!parseWindow("null", error).has_value());

  const QByteArray workspaces =
      R"([{"id":1,"idx":1,"output":"eDP-1"},{"id":2,"idx":1,"output":"DP-4"},)"
      R"({"id":9,"idx":3,"output":null}])";
  const QList<WorkspaceInfo> list = parseWorkspaces(workspaces, error);
  QCOMPARE(list.size(), 3);
  QCOMPARE(outputForWorkspace(list, 2, error).value(), QStringLiteral("DP-4"));
  QVERIFY(!outputForWorkspace(list, 9, error).has_value());
  QVERIFY(!error.isEmpty());
  error.clear();
  QVERIFY(!outputForWorkspace(list, 42, error).has_value());
  QVERIFY(error.contains(QStringLiteral("42")));
}
