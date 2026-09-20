/** @fileoverview Region geometry shared by select and overlay: parsing and
 * formatting of gpu-screen-recorder style WxH+X+Y rects in Niri global
 * coordinates. */
#pragma once

#include <QJsonObject>
#include <QRect>
#include <QString>
#include <optional>

/** A recordable rect on one output, in Niri logical (global) pixels. */
struct RegionRect {
  QString output;
  QRect rect;

  [[nodiscard]] QJsonObject toJson() const;
  /** Formats as WxH+X+Y, the form gpu-screen-recorder takes on -w. */
  [[nodiscard]] QString toGeometry() const;
};

/** Parses WxH+X+Y; X and Y may be negative (outputs above/left of origin). */
[[nodiscard]] std::optional<QRect> parseGeometry(const QString &text);
