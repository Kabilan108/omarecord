/** @fileoverview Resolves the focused Niri window to a global rect. Floating
 * windows come straight from IPC; tiled windows are found by matching a
 * `screenshot-window` capture inside a `grim` capture of the output. */
#pragma once

#include "niri.hpp"
#include "rect.hpp"

#include <QImage>
#include <QString>
#include <optional>

/** Everything needed to resolve the window later, gathered while no selector
 * overlay is mapped: Niri reports no focused window once an exclusive layer
 * surface takes the keyboard, and the output capture must not show the dim. */
struct WindowTarget {
  NiriWindow window;
  OutputInfo output;
  QImage outputImage; // Physical pixels; empty for floating windows.
};

/** Queries the focused window and its output, checks it lives on `wanted`
 * (any output when empty), and captures the output for later matching. */
[[nodiscard]] std::optional<WindowTarget>
prepareWindowTarget(const QString &wanted, QString &error);

/** Re-captures the output image, for when the cached one may be stale. */
[[nodiscard]] bool refreshOutputImage(WindowTarget &target, QString &error);

/** Resolves the window rect in Niri global logical coordinates. */
[[nodiscard]] std::optional<RegionRect>
resolveWindowRect(const WindowTarget &target, QString &error);
