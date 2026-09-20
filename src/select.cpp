#include "select.hpp"

#include "niri.hpp"
#include "rect.hpp"

#include <QCommandLineParser>
#include <QJsonDocument>
#include <QTextStream>

int runSelect(const QStringList &arguments) {
  QCommandLineParser parser;
  parser.addOptions({
      {QStringLiteral("mode"), QStringLiteral("region|window|monitor"),
       QStringLiteral("mode"), QStringLiteral("region")},
      {QStringLiteral("output"), QStringLiteral("Output name"), QStringLiteral("name")},
  });
  parser.process(arguments);
  QTextStream out(stdout);
  QTextStream err(stderr);
  QString error;
  const QString mode = parser.value(QStringLiteral("mode"));
  if (mode == QStringLiteral("monitor")) {
    const QList<OutputInfo> outputs = queryOutputs(error);
    QString name = parser.value(QStringLiteral("output"));
    if (name.isEmpty()) {
      name = queryFocusedOutputName(error).value_or(QString());
    }
    for (const OutputInfo &output : outputs) {
      if (output.name == name) {
        out << QJsonDocument(RegionRect{output.name, output.logical}.toJson())
                   .toJson(QJsonDocument::Compact)
            << '\n';
        return 0;
      }
    }
    err << (error.isEmpty() ? QStringLiteral("Unknown output: %1").arg(name) : error)
        << '\n';
    return 2;
  }
  err << "omarecord select: mode '" << mode << "' is not implemented yet\n";
  return 2;
}
