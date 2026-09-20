#include "window-locate.hpp"

#include <QVector>
#include <algorithm>
#include <cstdlib>

namespace {

/** Per-channel slack for rounding between the window render and the final
 * composite (dithering, blend rounding). */
constexpr int kChannelSlack = 3;
constexpr int kSampleColumns = 8;
constexpr int kSampleRows = 6;
constexpr int kSampleScanStride = 4;
constexpr int kEdgeStride = 4;
constexpr int kVerifyStride = 3;
/** Samples a cursor or tooltip may cover without rejecting the offset. */
constexpr double kSampleSlack = 0.15;

struct Sample {
  int x = 0;
  int y = 0;
  QRgb pixel = 0;
  int score = 0;
};

inline bool channelMatches(int needle, int hay, int slackHigh) {
  return hay >= needle - kChannelSlack && hay <= needle + slackHigh;
}

inline bool pixelMatches(QRgb needle, QRgb hay) {
  const int slackHigh = 255 - qAlpha(needle) + kChannelSlack;
  return channelMatches(qRed(needle), qRed(hay), slackHigh) &&
         channelMatches(qGreen(needle), qGreen(hay), slackHigh) &&
         channelMatches(qBlue(needle), qBlue(hay), slackHigh);
}

inline int contrast(QRgb a, QRgb b) {
  return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
         std::abs(qBlue(a) - qBlue(b));
}

/** One high-contrast, high-alpha pixel per grid cell, most distinctive
 * first, so the offset scan rejects almost every position on its first
 * comparison. Contrast is measured between the sample's neighbours, never
 * against the sample itself, so a stray pixel (cursor edge, subpixel text
 * fringe, a perturbed capture) is not the one that gets picked. Transparent
 * cells (pure shadow) carry no information. */
QVector<Sample> chooseSamples(const QImage &needle) {
  QVector<Sample> samples;
  const int w = needle.width();
  const int h = needle.height();
  if (w < 3 || h < 3)
    return samples;
  const int cellW = std::max(1, w / kSampleColumns);
  const int cellH = std::max(1, h / kSampleRows);
  for (int cy = 0; cy < kSampleRows; ++cy) {
    for (int cx = 0; cx < kSampleColumns; ++cx) {
      Sample best;
      best.score = -1;
      const int y0 = std::max(1, cy * cellH);
      const int x0 = std::max(1, cx * cellW);
      const int y1 = cy == kSampleRows - 1 ? h - 1 : y0 + cellH;
      const int x1 = cx == kSampleColumns - 1 ? w - 1 : x0 + cellW;
      for (int y = y0; y < y1; y += kSampleScanStride) {
        const auto *row = reinterpret_cast<const QRgb *>(needle.scanLine(y));
        const auto *above =
            reinterpret_cast<const QRgb *>(needle.scanLine(y - 1));
        const auto *below =
            reinterpret_cast<const QRgb *>(needle.scanLine(y + 1));
        for (int x = x0; x < x1; x += kSampleScanStride) {
          const QRgb pixel = row[x];
          const int alpha = qAlpha(pixel);
          if (alpha == 0)
            continue;
          const int gradient =
              contrast(row[x - 1], row[x + 1]) + contrast(above[x], below[x]);
          const int score = gradient * alpha;
          if (score > best.score)
            best = Sample{x, y, pixel, score};
        }
      }
      if (best.score >= 0)
        samples.append(best);
    }
  }
  std::sort(samples.begin(), samples.end(),
            [](const Sample &a, const Sample &b) { return a.score > b.score; });
  return samples;
}

struct Verifier {
  const QImage &haystack;
  const QImage &needle;
  double tolerance;

  /** Mismatch fraction over a sparse grid, or >1 once the budget is blown.
   * Skips fully transparent needle pixels: they say nothing. */
  double mismatch(int ox, int oy, int stride) const {
    const int w = needle.width();
    const int h = needle.height();
    int compared = 0;
    int bad = 0;
    const int budget =
        static_cast<int>(tolerance * ((w / stride + 1) * (h / stride + 1))) + 1;
    for (int y = 0; y < h; y += stride) {
      const auto *n = reinterpret_cast<const QRgb *>(needle.scanLine(y));
      const auto *hay =
          reinterpret_cast<const QRgb *>(haystack.scanLine(oy + y)) + ox;
      for (int x = 0; x < w; x += stride) {
        if (qAlpha(n[x]) == 0)
          continue;
        ++compared;
        if (!pixelMatches(n[x], hay[x]) && ++bad > budget)
          return 2.0;
      }
    }
    return compared == 0 ? 2.0 : static_cast<double>(bad) / compared;
  }

  /** Three rows and three columns at a small stride: cheap enough to run on
   * every candidate the sample filter lets through. */
  bool edgesPlausible(int ox, int oy) const {
    const int w = needle.width();
    const int h = needle.height();
    int compared = 0;
    int bad = 0;
    for (int y : {h / 4, h / 2, (3 * h) / 4}) {
      const auto *n = reinterpret_cast<const QRgb *>(needle.scanLine(y));
      const auto *hay =
          reinterpret_cast<const QRgb *>(haystack.scanLine(oy + y)) + ox;
      for (int x = 0; x < w; x += kEdgeStride) {
        if (qAlpha(n[x]) == 0)
          continue;
        ++compared;
        bad += !pixelMatches(n[x], hay[x]);
      }
    }
    for (int x : {w / 4, w / 2, (3 * w) / 4}) {
      for (int y = 0; y < h; y += kEdgeStride) {
        const QRgb n = reinterpret_cast<const QRgb *>(needle.scanLine(y))[x];
        if (qAlpha(n) == 0)
          continue;
        const QRgb hay =
            reinterpret_cast<const QRgb *>(haystack.scanLine(oy + y))[ox + x];
        ++compared;
        bad += !pixelMatches(n, hay);
      }
    }
    return compared > 0 && bad <= tolerance * compared;
  }
};

} // namespace

std::optional<QPoint> locateSubImage(const QImage &haystackIn,
                                     const QImage &needleIn, double tolerance) {
  if (haystackIn.isNull() || needleIn.isNull() ||
      needleIn.width() > haystackIn.width() ||
      needleIn.height() > haystackIn.height())
    return std::nullopt;
  const QImage haystack = haystackIn.convertToFormat(QImage::Format_RGB32);
  const QImage needle = needleIn.convertToFormat(QImage::Format_ARGB32);
  const QVector<Sample> samples = chooseSamples(needle);
  if (samples.isEmpty())
    return std::nullopt;

  const int maxX = haystack.width() - needle.width();
  const int maxY = haystack.height() - needle.height();
  QVector<const QRgb *> rows(haystack.height());
  for (int y = 0; y < haystack.height(); ++y)
    rows[y] = reinterpret_cast<const QRgb *>(haystack.scanLine(y));

  const Verifier verifier{haystack, needle, tolerance};
  const int allowedSampleMisses =
      static_cast<int>(kSampleSlack * samples.size());
  std::optional<QPoint> best;
  double bestMismatch = 2.0;
  for (int oy = 0; oy <= maxY; ++oy) {
    for (int ox = 0; ox <= maxX; ++ox) {
      int misses = 0;
      for (const Sample &sample : samples) {
        if (!pixelMatches(sample.pixel, rows[oy + sample.y][ox + sample.x]) &&
            ++misses > allowedSampleMisses)
          break;
      }
      if (misses > allowedSampleMisses || !verifier.edgesPlausible(ox, oy))
        continue;
      const double mismatch = verifier.mismatch(ox, oy, kVerifyStride);
      if (mismatch > tolerance || mismatch >= bestMismatch)
        continue;
      best = QPoint(ox, oy);
      bestMismatch = mismatch;
      if (mismatch == 0.0)
        return best;
    }
  }
  return best;
}

std::optional<QRect> windowGeometryInBuffer(const QImage &bufferIn,
                                            const QSize &expected) {
  if (bufferIn.isNull() || expected.isEmpty())
    return std::nullopt;
  auto closeEnough = [](const QSize &a, const QSize &b) {
    return std::abs(a.width() - b.width()) <= 1 &&
           std::abs(a.height() - b.height()) <= 1;
  };
  if (closeEnough(bufferIn.size(), expected))
    return QRect(QPoint(0, 0), bufferIn.size());
  const QImage buffer = bufferIn.convertToFormat(QImage::Format_ARGB32);
  int maxAlpha = 0;
  for (int y = 0; y < buffer.height(); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(buffer.scanLine(y));
    for (int x = 0; x < buffer.width(); ++x)
      maxAlpha = std::max(maxAlpha, qAlpha(row[x]));
  }
  if (maxAlpha == 0)
    return std::nullopt;
  int left = buffer.width();
  int top = buffer.height();
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < buffer.height(); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(buffer.scanLine(y));
    for (int x = 0; x < buffer.width(); ++x) {
      if (qAlpha(row[x]) != maxAlpha)
        continue;
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  const QRect box(QPoint(left, top), QPoint(right, bottom));
  if (!closeEnough(box.size(), expected))
    return std::nullopt;
  return box;
}
