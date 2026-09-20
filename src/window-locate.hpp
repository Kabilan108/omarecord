/** @fileoverview Pure sub-image search: finds where a window screenshot sits
 * inside an output screenshot without depending on compositor coordinates. */
#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <optional>

/** Returns the top-left offset in `haystack` where `needle` appears.
 *
 * Needle alpha is honoured as slack rather than blended: a pixel stored as
 * (rgb, a) may show up in the haystack anywhere in [rgb, rgb + 255 - a] per
 * channel, because whatever the compositor drew behind the window adds to it.
 * `tolerance` is the fraction of verified pixels allowed to differ outright
 * (cursor, tooltip, popup). Returns nullopt when nothing passes. */
[[nodiscard]] std::optional<QPoint>
locateSubImage(const QImage &haystack, const QImage &needle, double tolerance);

/** Finds the window geometry inside a `screenshot-window` buffer, which Niri
 * pads with CSD shadows. The geometry is the bounding box of the buffer's
 * most opaque pixels; it must match `expected` (physical pixels, +-1) or the
 * result is nullopt. A buffer already of the expected size is the geometry. */
[[nodiscard]] std::optional<QRect>
windowGeometryInBuffer(const QImage &buffer, const QSize &expected);
