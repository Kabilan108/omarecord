#include "niri.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <climits>

namespace {

struct ProcessResult {
  bool finished = false;
  int exitCode = -1;
  QByteArray output;
  QByteArray errorOutput;
};

ProcessResult runNiri(const QStringList &arguments) {
  QProcess process;
  process.start(QStringLiteral("niri"), arguments);
  ProcessResult result;
  if (!process.waitForStarted(3000) || !process.waitForFinished(5000)) {
    result.errorOutput = process.errorString().toUtf8();
    return result;
  }
  result.finished = process.exitStatus() == QProcess::NormalExit;
  result.exitCode = process.exitCode();
  result.output = process.readAllStandardOutput();
  result.errorOutput = process.readAllStandardError();
  return result;
}

QString failure(const ProcessResult &result) {
  const QString stderrText = QString::fromUtf8(result.errorOutput).trimmed();
  if (!stderrText.isEmpty()) {
    return stderrText;
  }
  return result.finished ? QStringLiteral("exit code %1").arg(result.exitCode)
                         : QStringLiteral("niri did not finish");
}

std::optional<QJsonDocument> parseDocument(const QByteArray &json,
                                           const QString &what,
                                           QString &error) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    error = QStringLiteral("Could not parse Niri %1: %2")
                .arg(what, parseError.errorString());
    return std::nullopt;
  }
  return document;
}

} // namespace

QList<OutputInfo> parseOutputs(const QByteArray &json, QString &error) {
  const auto document = parseDocument(json, QStringLiteral("outputs"), error);
  if (!document || !document->isObject()) {
    if (error.isEmpty()) {
      error = QStringLiteral("Niri outputs were not an object");
    }
    return {};
  }
  QList<OutputInfo> outputs;
  const QJsonObject byName = document->object();
  for (auto it = byName.begin(); it != byName.end(); ++it) {
    const QJsonObject logical =
        it.value().toObject().value(QStringLiteral("logical")).toObject();
    if (logical.isEmpty()) {
      continue; // Disabled output: connected but not mapped anywhere.
    }
    OutputInfo info;
    info.name = it.key();
    info.logical = QRect(logical.value(QStringLiteral("x")).toInt(),
                         logical.value(QStringLiteral("y")).toInt(),
                         logical.value(QStringLiteral("width")).toInt(),
                         logical.value(QStringLiteral("height")).toInt());
    info.scale = logical.value(QStringLiteral("scale")).toDouble(1.0);
    outputs.append(info);
  }
  if (outputs.isEmpty()) {
    error = QStringLiteral("Niri reported no enabled outputs");
  }
  return outputs;
}

std::optional<OutputInfo> outputContaining(const QList<OutputInfo> &outputs,
                                           const QPoint &point) {
  for (const OutputInfo &output : outputs) {
    if (output.logical.contains(point)) {
      return output;
    }
  }
  return std::nullopt;
}

std::optional<FocusedWindow>
parseFocusedWindow(const QByteArray &json, const QList<OutputInfo> &outputs,
                   QString &error) {
  if (json.trimmed() == QByteArrayLiteral("null") || json.trimmed().isEmpty()) {
    error = QStringLiteral("Niri did not report a focused window");
    return std::nullopt;
  }
  const auto document =
      parseDocument(json, QStringLiteral("focused window"), error);
  if (!document || !document->isObject()) {
    return std::nullopt;
  }
  const QJsonObject object = document->object();
  const QJsonObject layout = object.value(QStringLiteral("layout")).toObject();
  const QJsonArray size = layout.value(QStringLiteral("window_size")).toArray();
  const QJsonValue tilePos = layout.value(QStringLiteral("tile_pos_in_workspace_view"));
  if (size.size() != 2) {
    error = QStringLiteral("Niri window layout had no size");
    return std::nullopt;
  }
  FocusedWindow window;
  window.title = object.value(QStringLiteral("title")).toString();
  window.appId = object.value(QStringLiteral("app_id")).toString();
  window.rect = QRect(QPoint(), QSize(size.at(0).toInt(), size.at(1).toInt()));
  Q_UNUSED(outputs);
  if (!tilePos.isArray()) {
    // Tiled windows: Niri reports no position over IPC. Callers must locate
    // the window another way; only the size is trustworthy here.
    window.rect.moveTopLeft(QPoint(INT_MIN, INT_MIN));
  } else {
    const QJsonArray pos = tilePos.toArray();
    window.rect.moveTopLeft(QPoint(static_cast<int>(pos.at(0).toDouble()),
                                   static_cast<int>(pos.at(1).toDouble())));
  }
  return window;
}

std::optional<NiriWindow> parseWindow(const QByteArray &json, QString &error) {
  if (json.trimmed() == QByteArrayLiteral("null") || json.trimmed().isEmpty()) {
    error = QStringLiteral("no focused window");
    return std::nullopt;
  }
  const auto document =
      parseDocument(json, QStringLiteral("focused window"), error);
  if (!document || !document->isObject()) {
    if (error.isEmpty())
      error = QStringLiteral("Niri window was not an object");
    return std::nullopt;
  }
  const QJsonObject object = document->object();
  const QJsonObject layout = object.value(QStringLiteral("layout")).toObject();
  const QJsonArray size = layout.value(QStringLiteral("window_size")).toArray();
  if (size.size() != 2 || size.at(0).toInt() <= 0 || size.at(1).toInt() <= 0) {
    error = QStringLiteral("Niri window layout had no size");
    return std::nullopt;
  }
  NiriWindow window;
  window.id = object.value(QStringLiteral("id")).toInteger(-1);
  window.workspaceId =
      object.value(QStringLiteral("workspace_id")).toInteger(-1);
  window.title = object.value(QStringLiteral("title")).toString();
  window.appId = object.value(QStringLiteral("app_id")).toString();
  window.size = QSize(size.at(0).toInt(), size.at(1).toInt());
  const QJsonValue tilePos =
      layout.value(QStringLiteral("tile_pos_in_workspace_view"));
  if (tilePos.isArray() && tilePos.toArray().size() == 2) {
    const QJsonArray pos = tilePos.toArray();
    window.positionOnOutput = QPoint(qRound(pos.at(0).toDouble()),
                                     qRound(pos.at(1).toDouble()));
  }
  if (window.id < 0) {
    error = QStringLiteral("Niri window had no id");
    return std::nullopt;
  }
  return window;
}

QList<WorkspaceInfo> parseWorkspaces(const QByteArray &json, QString &error) {
  const auto document =
      parseDocument(json, QStringLiteral("workspaces"), error);
  if (!document || !document->isArray()) {
    if (error.isEmpty())
      error = QStringLiteral("Niri workspaces were not an array");
    return {};
  }
  QList<WorkspaceInfo> workspaces;
  for (const QJsonValue &value : document->array()) {
    const QJsonObject object = value.toObject();
    WorkspaceInfo info;
    info.id = object.value(QStringLiteral("id")).toInteger(-1);
    info.output = object.value(QStringLiteral("output")).toString();
    if (info.id >= 0)
      workspaces.append(info);
  }
  return workspaces;
}

std::optional<QString>
outputForWorkspace(const QList<WorkspaceInfo> &workspaces, qint64 workspaceId,
                   QString &error) {
  for (const WorkspaceInfo &workspace : workspaces) {
    if (workspace.id != workspaceId)
      continue;
    if (workspace.output.isEmpty()) {
      error = QStringLiteral("Niri workspace %1 is not on any output")
                  .arg(workspaceId);
      return std::nullopt;
    }
    return workspace.output;
  }
  error = QStringLiteral("Niri reported no workspace with id %1")
              .arg(workspaceId);
  return std::nullopt;
}

QList<OutputInfo> queryOutputs(QString &error) {
  const ProcessResult result = runNiri(
      {QStringLiteral("msg"), QStringLiteral("--json"), QStringLiteral("outputs")});
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("Could not query Niri outputs: %1").arg(failure(result));
    return {};
  }
  return parseOutputs(result.output, error);
}

std::optional<QString> queryFocusedOutputName(QString &error) {
  const ProcessResult result =
      runNiri({QStringLiteral("msg"), QStringLiteral("--json"),
               QStringLiteral("focused-output")});
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("Could not query the focused Niri output: %1")
                .arg(failure(result));
    return std::nullopt;
  }
  const auto document =
      parseDocument(result.output, QStringLiteral("focused output"), error);
  if (!document || !document->isObject()) {
    return std::nullopt;
  }
  const QString name = document->object().value(QStringLiteral("name")).toString();
  if (name.isEmpty()) {
    error = QStringLiteral("Niri reported no focused output");
    return std::nullopt;
  }
  return name;
}

std::optional<FocusedWindow> queryFocusedWindow(QString &error) {
  const ProcessResult result =
      runNiri({QStringLiteral("msg"), QStringLiteral("--json"),
               QStringLiteral("focused-window")});
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("Could not query the focused Niri window: %1")
                .arg(failure(result));
    return std::nullopt;
  }
  return parseFocusedWindow(result.output, queryOutputs(error), error);
}

std::optional<NiriWindow> queryFocusedNiriWindow(QString &error) {
  const ProcessResult result =
      runNiri({QStringLiteral("msg"), QStringLiteral("--json"),
               QStringLiteral("focused-window")});
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("Could not query the focused Niri window: %1")
                .arg(failure(result));
    return std::nullopt;
  }
  return parseWindow(result.output, error);
}

QList<WorkspaceInfo> queryWorkspaces(QString &error) {
  const ProcessResult result =
      runNiri({QStringLiteral("msg"), QStringLiteral("--json"),
               QStringLiteral("workspaces")});
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("Could not query Niri workspaces: %1")
                .arg(failure(result));
    return {};
  }
  return parseWorkspaces(result.output, error);
}
