/** @fileoverview CLI entry: `select` prints a region as JSON, `overlay` runs
 * the recording overlay. Both are driven by an orchestrator, never by hand. */
#include "overlay.hpp"
#include "select.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>

namespace {

int printUsage(QTextStream &out) {
  out << "usage: omarecord select [--mode region|window|monitor] [--output NAME]\n"
         "       omarecord overlay --output NAME --rect WxH+X+Y --socket PATH\n"
         "       omarecord --version\n";
  return 2;
}

} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("omarecord"));
  QApplication::setApplicationVersion(QStringLiteral(OMARECORD_VERSION));
  const QStringList arguments = QApplication::arguments();
  QTextStream out(stdout);
  if (arguments.size() < 2) {
    return printUsage(out);
  }
  const QString command = arguments.at(1);
  if (command == QStringLiteral("--version")) {
    out << "omarecord " << OMARECORD_VERSION << '\n';
    return 0;
  }
  QStringList rest = arguments;
  rest.removeAt(1);
  if (command == QStringLiteral("select")) {
    return runSelect(rest);
  }
  if (command == QStringLiteral("overlay")) {
    return runOverlay(rest);
  }
  return printUsage(out);
}
