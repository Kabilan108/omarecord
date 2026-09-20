#include "overlay.hpp"

#include <QTextStream>

int runOverlay(const QStringList &arguments) {
  Q_UNUSED(arguments);
  QTextStream err(stderr);
  err << "omarecord overlay: not implemented yet\n";
  return 2;
}
