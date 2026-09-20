#include "window-resolve.hpp"

#include "window-locate.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <cmath>

namespace {

/** Fraction of sampled pixels allowed to differ between the two captures.
 * They are taken tens of milliseconds apart, so spinners, caret blink and
 * clocks legitimately differ; a wrong offset differs in far more. */
constexpr double kMatchTolerance = 0.3;

struct ProcessResult {
  bool finished = false;
  int exitCode = -1;
  QByteArray output;
  QByteArray errorOutput;
};

ProcessResult run(const QString &program, const QStringList &arguments,
                  const QByteArray &input = {}, int timeoutMs = 5000) {
  QProcess process;
  process.start(program, arguments);
  ProcessResult result;
  if (!process.waitForStarted(3000)) {
    result.errorOutput = process.errorString().toUtf8();
    return result;
  }
  if (!input.isEmpty())
    process.write(input);
  process.closeWriteChannel();
  if (!process.waitForFinished(timeoutMs)) {
    result.errorOutput = process.errorString().toUtf8();
    process.kill();
    return result;
  }
  result.finished = process.exitStatus() == QProcess::NormalExit;
  result.exitCode = process.exitCode();
  result.output = process.readAllStandardOutput();
  result.errorOutput = process.readAllStandardError();
  return result;
}

QString describe(const ProcessResult &result) {
  const QString text = QString::fromUtf8(result.errorOutput).trimmed();
  if (!text.isEmpty())
    return text;
  return result.finished ? QStringLiteral("exit code %1").arg(result.exitCode)
                         : QStringLiteral("did not finish");
}

QString scratchPath(const QString &suffix) {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (dir.isEmpty())
    dir = QDir::tempPath();
  return QDir(dir).filePath(QStringLiteral("omarecord-%1-%2")
                                .arg(QCoreApplication::applicationPid())
                                .arg(suffix));
}

struct ScratchFile {
  QString path;
  explicit ScratchFile(const QString &suffix) : path(scratchPath(suffix)) {}
  ~ScratchFile() { QFile::remove(path); }
  ScratchFile(const ScratchFile &) = delete;
  ScratchFile &operator=(const ScratchFile &) = delete;
};

/** `screenshot-window` always publishes the capture on the clipboard, even
 * with --path and write-to-disk=false (checked against niri 2026-08-02), so
 * the previous selection is stashed and put back afterwards. */
struct ClipboardSnapshot {
  bool valid = false;
  bool wasEmpty = false;
  QString mimeType;
  QByteArray payload;
};

ClipboardSnapshot snapshotClipboard() {
  ClipboardSnapshot snapshot;
  const ProcessResult listed =
      run(QStringLiteral("wl-paste"), {QStringLiteral("--list-types")}, {}, 2000);
  if (!listed.finished)
    return snapshot;
  const QStringList types =
      QString::fromUtf8(listed.output).split('\n', Qt::SkipEmptyParts);
  if (listed.exitCode != 0 || types.isEmpty()) {
    snapshot.valid = true;
    snapshot.wasEmpty = true;
    return snapshot;
  }
  static const QStringList preferred{
      QStringLiteral("image/png"),
      QStringLiteral("text/plain;charset=utf-8"),
      QStringLiteral("text/plain"),
      QStringLiteral("UTF8_STRING"),
  };
  snapshot.mimeType = types.first();
  for (const QString &type : preferred) {
    if (types.contains(type)) {
      snapshot.mimeType = type;
      break;
    }
  }
  const ProcessResult pasted =
      run(QStringLiteral("wl-paste"),
          {QStringLiteral("--no-newline"), QStringLiteral("--type"),
           snapshot.mimeType},
          {}, 2000);
  if (!pasted.finished || pasted.exitCode != 0)
    return snapshot;
  snapshot.payload = pasted.output;
  snapshot.valid = true;
  return snapshot;
}

bool clipboardHoldsPng() {
  const ProcessResult listed =
      run(QStringLiteral("wl-paste"), {QStringLiteral("--list-types")}, {}, 1000);
  return listed.finished && listed.exitCode == 0 &&
         listed.output.contains("image/png");
}

void restoreClipboard(const ClipboardSnapshot &snapshot) {
  if (!snapshot.valid)
    return;
  // Niri publishes the copy asynchronously; restoring before it lands would
  // just be overwritten.
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 1000 && !clipboardHoldsPng())
    QThread::msleep(15);
  if (snapshot.wasEmpty)
    run(QStringLiteral("wl-copy"), {QStringLiteral("--clear")});
  else
    run(QStringLiteral("wl-copy"),
        {QStringLiteral("--type"), snapshot.mimeType}, snapshot.payload);
}

QImage captureOutput(const OutputInfo &output, QString &error) {
  // PPM skips PNG encode/decode: ~50 ms for a 3440x1440 output either way
  // on grim's side, but the decode is a memcpy.
  ScratchFile file(QStringLiteral("output.ppm"));
  const ProcessResult result =
      run(QStringLiteral("grim"),
          {QStringLiteral("-t"), QStringLiteral("ppm"), QStringLiteral("-o"),
           output.name, file.path});
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("Could not capture %1 with grim: %2")
                .arg(output.name, describe(result));
    return {};
  }
  QImage image(file.path);
  if (image.isNull()) {
    error = QStringLiteral("grim wrote an unreadable capture of %1")
                .arg(output.name);
    return {};
  }
  return image;
}

QImage captureWindow(const NiriWindow &window, QString &error) {
  ScratchFile file(QStringLiteral("window.png"));
  const ClipboardSnapshot clipboard = snapshotClipboard();
  const ProcessResult result =
      run(QStringLiteral("niri"),
          {QStringLiteral("msg"), QStringLiteral("action"),
           QStringLiteral("screenshot-window"), QStringLiteral("--id"),
           QString::number(window.id), QStringLiteral("--path"), file.path},
          {}, 10000);
  if (!result.finished || result.exitCode != 0) {
    restoreClipboard(clipboard);
    error = QStringLiteral("Niri could not screenshot window %1: %2")
                .arg(window.id)
                .arg(describe(result));
    return {};
  }
  // The action returns before the file exists.
  QImage image;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000 && !image.load(file.path))
    QThread::msleep(10);
  restoreClipboard(clipboard);
  if (image.isNull())
    error = QStringLiteral("Timed out waiting for Niri's capture of window %1")
                .arg(window.id);
  return image;
}

QSize physicalSize(const QSize &logical, double scale) {
  return QSize(static_cast<int>(std::lround(logical.width() * scale)),
               static_cast<int>(std::lround(logical.height() * scale)));
}

QString windowLabel(const NiriWindow &window) {
  return QStringLiteral("%1 (%2, id %3)")
      .arg(window.title.isEmpty() ? QStringLiteral("untitled") : window.title,
           window.appId.isEmpty() ? QStringLiteral("no app id") : window.appId)
      .arg(window.id);
}

} // namespace

std::optional<WindowTarget> prepareWindowTarget(const QString &wanted,
                                                QString &error) {
  const std::optional<NiriWindow> window = queryFocusedNiriWindow(error);
  if (!window)
    return std::nullopt;
  const QList<WorkspaceInfo> workspaces = queryWorkspaces(error);
  if (workspaces.isEmpty())
    return std::nullopt;
  const std::optional<QString> outputName =
      outputForWorkspace(workspaces, window->workspaceId, error);
  if (!outputName)
    return std::nullopt;
  if (!wanted.isEmpty() && *outputName != wanted) {
    error = QStringLiteral("The focused window %1 is on %2, not %3")
                .arg(windowLabel(*window), *outputName, wanted);
    return std::nullopt;
  }
  WindowTarget target;
  target.window = *window;
  bool found = false;
  for (const OutputInfo &output : queryOutputs(error)) {
    if (output.name == *outputName) {
      target.output = output;
      found = true;
    }
  }
  if (!found) {
    if (error.isEmpty())
      error = QStringLiteral("Niri did not list output %1").arg(*outputName);
    return std::nullopt;
  }
  if (!window->positionOnOutput && !refreshOutputImage(target, error))
    return std::nullopt;
  return target;
}

bool refreshOutputImage(WindowTarget &target, QString &error) {
  target.outputImage = captureOutput(target.output, error);
  return !target.outputImage.isNull();
}

std::optional<RegionRect> resolveWindowRect(const WindowTarget &target,
                                            QString &error) {
  const NiriWindow &window = target.window;
  const OutputInfo &output = target.output;
  const QRect bounds(QPoint(0, 0), output.logical.size());
  if (window.positionOnOutput) {
    const QRect local(*window.positionOnOutput, window.size);
    const QRect clamped = local.intersected(bounds);
    if (clamped.isEmpty()) {
      error = QStringLiteral("The focused window %1 lies outside %2")
                  .arg(windowLabel(window), output.name);
      return std::nullopt;
    }
    return RegionRect{output.name,
                      clamped.translated(output.logical.topLeft())};
  }
  if (target.outputImage.isNull()) {
    error = QStringLiteral("No capture of %1 to locate the window in")
                .arg(output.name);
    return std::nullopt;
  }
  const QImage buffer = captureWindow(window, error);
  if (buffer.isNull())
    return std::nullopt;
  const QSize expected = physicalSize(window.size, output.scale);
  const std::optional<QRect> geometry =
      windowGeometryInBuffer(buffer, expected);
  if (!geometry) {
    error = QStringLiteral("Niri's capture of %1 is %2x%3 and holds no %4x%5 "
                           "window in it")
                .arg(windowLabel(window))
                .arg(buffer.width())
                .arg(buffer.height())
                .arg(expected.width())
                .arg(expected.height());
    return std::nullopt;
  }
  const std::optional<QPoint> found = locateSubImage(
      target.outputImage, buffer.copy(*geometry), kMatchTolerance);
  if (!found) {
    error = QStringLiteral("could not locate the focused window %1 on %2")
                .arg(windowLabel(window), output.name);
    return std::nullopt;
  }
  const QPoint logical(
      static_cast<int>(std::lround(found->x() / output.scale)),
      static_cast<int>(std::lround(found->y() / output.scale)));
  const QRect local = QRect(logical, window.size).intersected(bounds);
  return RegionRect{output.name, local.translated(output.logical.topLeft())};
}
