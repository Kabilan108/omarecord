#include "select.hpp"

#include "instance-lock.hpp"
#include "niri.hpp"
#include "rect.hpp"
#include "selector-window.hpp"
#include "window-resolve.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLockFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <cerrno>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {

/** Turns SIGTERM/SIGINT (a second `select` dismissing this one, or an
 * orchestrator giving up) into an orderly event-loop exit so the lock file is
 * released and the exit code stays within the contract. */
class PosixSignalBridge final : public QObject {
public:
  explicit PosixSignalBridge(QObject *parent = nullptr) : QObject(parent) {
    if (::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                     fds_) != 0)
      return;
    writeFd_ = fds_[0];
    struct sigaction action{};
    action.sa_handler = [](int signal) {
      const int savedErrno = errno;
      const char byte = static_cast<char>(signal);
      if (writeFd_ >= 0 && ::write(writeFd_, &byte, sizeof(byte)) < 0) {
        // Only a wake-up hint; a dropped datagram is harmless.
      }
      errno = savedErrno;
    };
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &action, &previousTerm_);
    ::sigaction(SIGINT, &action, &previousInt_);
    installed_ = true;
    auto *notifier = new QSocketNotifier(fds_[1], QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, notifier] {
      notifier->setEnabled(false);
      char received = 0;
      while (::read(fds_[1], &received, sizeof(received)) > 0)
        signal_ = received;
      QCoreApplication::exit(0);
    });
  }

  /** The signal that ended the event loop, or 0 if none arrived. */
  [[nodiscard]] int receivedSignal() const { return signal_; }

  ~PosixSignalBridge() override {
    if (installed_) {
      ::sigaction(SIGTERM, &previousTerm_, nullptr);
      ::sigaction(SIGINT, &previousInt_, nullptr);
    }
    writeFd_ = -1;
    for (int fd : fds_) {
      if (fd >= 0)
        ::close(fd);
    }
  }

private:
  static inline volatile int writeFd_ = -1;
  int fds_[2] = {-1, -1};
  int signal_ = 0;
  bool installed_ = false;
  struct sigaction previousTerm_{};
  struct sigaction previousInt_{};
};

std::optional<OutputInfo> resolveOutput(const QString &requested,
                                        QString &error) {
  const QList<OutputInfo> outputs = queryOutputs(error);
  QString name = requested;
  if (name.isEmpty())
    name = queryFocusedOutputName(error).value_or(QString());
  for (const OutputInfo &output : outputs) {
    if (output.name == name)
      return output;
  }
  if (error.isEmpty())
    error = QStringLiteral("Unknown output: %1").arg(name);
  return std::nullopt;
}

void printRegion(QTextStream &out, const RegionRect &region) {
  out << QJsonDocument(region.toJson()).toJson(QJsonDocument::Compact) << '\n';
  out.flush();
}

QString instanceLockPath() {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (dir.isEmpty())
    dir = QDir::tempPath();
  return QDir(dir).filePath(QStringLiteral("omarecord-select.lock"));
}

int runRegion(const OutputInfo &output, QTextStream &out, QTextStream &err) {
  QLockFile lock(instanceLockPath());
  const InstanceLockResult lockResult =
      acquireInstanceLock(lock, InstanceMode::Capture);
  if (!lockResult.proceed) {
    if (!lockResult.error.isEmpty()) {
      err << lockResult.error << '\n';
      return 2;
    }
    // Dismissing a running selector is a cancel, not a failure.
    err << "omarecord select: dismissed the running selector (pid "
        << lockResult.signalledPid << "); nothing selected\n";
    return 1;
  }
  SelectorWindow selector(output);
  if (QGuiApplication::platformName() == QStringLiteral("wayland") &&
      !selector.attachLayerShell()) {
    err << "omarecord select: could not create a layer surface on "
        << output.name << '\n';
    return 2;
  }
  // Space needs the focused window and a clean capture of the output, and
  // neither is available once the selector is up: Niri reports no focused
  // window while an exclusive layer surface holds the keyboard, and grim
  // would see the dim. Both are gathered now (well under 150 ms) so Space is
  // instant; a failure here is only reported if Space is actually pressed.
  QString windowError;
  std::optional<WindowTarget> windowTarget =
      prepareWindowTarget(output.name, windowError);
  selector.setWindowPicker([&]() -> std::optional<RegionRect> {
    if (!windowTarget)
      return std::nullopt;
    if (const auto region = resolveWindowRect(*windowTarget, windowError))
      return region;
    // The window may have repainted since the pre-capture (video, spinner).
    // Re-capture without the dim: unmap, give the compositor two frames.
    selector.hide();
    for (int i = 0; i < 2; ++i) {
      QCoreApplication::processEvents();
      QThread::msleep(20);
    }
    QString refreshError;
    if (!refreshOutputImage(*windowTarget, refreshError))
      return std::nullopt;
    return resolveWindowRect(*windowTarget, windowError);
  });
  PosixSignalBridge signalBridge;
  QObject::connect(&selector, &SelectorWindow::finished, &selector,
                   [] { QCoreApplication::exit(0); });
  selector.show();
  selector.setFocus(Qt::ActiveWindowFocusReason);
  QCoreApplication::exec();
  // The loop ends on confirm, Esc, or a signal; anything without a region is
  // a cancel from the orchestrator's point of view.
  if (const std::optional<RegionRect> region = selector.result()) {
    printRegion(out, *region);
    return 0;
  }
  if (selector.failed()) {
    err << "omarecord select: " << windowError << '\n';
    return 2;
  }
  if (selector.cancelled())
    err << "omarecord select: cancelled (Esc)\n";
  else if (const int signal = signalBridge.receivedSignal(); signal != 0)
    err << "omarecord select: cancelled by signal "
        << (signal == SIGTERM ? "SIGTERM" : signal == SIGINT ? "SIGINT" : "?")
        << " before a region was chosen\n";
  else
    err << "omarecord select: event loop ended without a region\n";
  return 1;
}

} // namespace

int runSelect(const QStringList &arguments) {
  QCommandLineParser parser;
  parser.addOptions({
      {QStringLiteral("mode"), QStringLiteral("region|window|monitor"),
       QStringLiteral("mode"), QStringLiteral("region")},
      {QStringLiteral("output"), QStringLiteral("Output name"),
       QStringLiteral("name")},
  });
  parser.process(arguments);
  QTextStream out(stdout);
  QTextStream err(stderr);
  const QString mode = parser.value(QStringLiteral("mode"));
  QString error;
  if (mode == QStringLiteral("window")) {
    const std::optional<WindowTarget> target =
        prepareWindowTarget(parser.value(QStringLiteral("output")), error);
    const std::optional<RegionRect> region =
        target ? resolveWindowRect(*target, error) : std::nullopt;
    if (!region) {
      err << "omarecord select: " << error << '\n';
      return 2;
    }
    printRegion(out, *region);
    return 0;
  }
  if (mode != QStringLiteral("monitor") && mode != QStringLiteral("region")) {
    err << "omarecord select: unknown mode '" << mode << "'\n";
    return 2;
  }
  const std::optional<OutputInfo> output =
      resolveOutput(parser.value(QStringLiteral("output")), error);
  if (!output) {
    err << error << '\n';
    return 2;
  }
  if (mode == QStringLiteral("monitor")) {
    printRegion(out, RegionRect{output->name, output->logical});
    return 0;
  }
  return runRegion(*output, out, err);
}
