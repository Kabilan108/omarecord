/** @fileoverview Niri JSON IPC queries for outputs and the focused window,
 * plus pure parsers over their JSON so tests can run without a compositor. */
#pragma once

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
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

/** Identity and layout of one window as Niri reports it; tiled windows carry
 * no position, only a size, and must be located by image match. */
struct NiriWindow {
  qint64 id = -1;
  qint64 workspaceId = -1;
  QString title;
  QString appId;
  QSize size; // Logical pixels.
  std::optional<QPoint> positionOnOutput; // Floating windows only.
};

struct WorkspaceInfo {
  qint64 id = -1;
  QString output;
};

[[nodiscard]] std::optional<NiriWindow> parseWindow(const QByteArray &json,
                                                    QString &error);
[[nodiscard]] QList<WorkspaceInfo> parseWorkspaces(const QByteArray &json,
                                                   QString &error);
/** Name of the output showing `workspaceId`, or nullopt with `error` set. */
[[nodiscard]] std::optional<QString>
outputForWorkspace(const QList<WorkspaceInfo> &workspaces, qint64 workspaceId,
                   QString &error);

/** Live queries; each shells out to `niri msg --json`. */
[[nodiscard]] QList<OutputInfo> queryOutputs(QString &error);
[[nodiscard]] std::optional<QString> queryFocusedOutputName(QString &error);
[[nodiscard]] std::optional<FocusedWindow> queryFocusedWindow(QString &error);
[[nodiscard]] std::optional<NiriWindow> queryFocusedNiriWindow(QString &error);
[[nodiscard]] QList<WorkspaceInfo> queryWorkspaces(QString &error);
