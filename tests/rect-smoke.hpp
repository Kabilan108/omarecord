#pragma once
#include <QObject>

class RectSmoke final : public QObject {
  Q_OBJECT
private slots:
  void parsesGeometryWithNegativeOffsets();
  void rejectsEmptyOrMalformedGeometry();
  void roundTripsGeometryAndJson();
};
