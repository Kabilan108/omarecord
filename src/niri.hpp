/** @fileoverview Niri JSON IPC queries for outputs and the focused window,
 * plus pure parsers over their JSON so tests can run without a compositor. */
#pragma once

#include <QByteArray>
#include <QList>
#include <QRect>
#include <QString>
#include <optional>

struct OutputInfo {
  QString name;
  QRect logical; // Global logical geometry, may have negative origin.
  double scale = 1.0;
};

struct FocusedWindow {
  QString title;
  QString appId;
  QRect rect; // Global logical geometry.
  QString output;
};

[[nodiscard]] QList<OutputInfo> parseOutputs(const QByteArray &json, QString &error);
/** Window JSON from `niri msg --json focused-window`; window layout comes with
 * workspace-relative coordinates, so the caller passes the outputs to resolve
 * to global space. */
[[nodiscard]] std::optional<FocusedWindow>
parseFocusedWindow(const QByteArray &json, const QList<OutputInfo> &outputs,
                   QString &error);
[[nodiscard]] std::optional<OutputInfo>
outputContaining(const QList<OutputInfo> &outputs, const QPoint &point);

/** Live queries; each shells out to `niri msg --json`. */
[[nodiscard]] QList<OutputInfo> queryOutputs(QString &error);
[[nodiscard]] std::optional<QString> queryFocusedOutputName(QString &error);
[[nodiscard]] std::optional<FocusedWindow> queryFocusedWindow(QString &error);
