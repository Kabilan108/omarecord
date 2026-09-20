#pragma once
#include <QObject>

class SelectSmoke final : public QObject {
  Q_OBJECT
private slots:
  void dragPrintsGlobalRect();
  void escapeCancels();
  void tinyDragKeepsWaiting();
  void selectAllTakesWholeOutput();
};
