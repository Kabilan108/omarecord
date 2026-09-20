#pragma once
#include <QObject>

class NiriSmoke final : public QObject {
  Q_OBJECT
private slots:
  void parsesOutputsWithNegativeOrigin();
  void skipsDisabledOutputs();
  void findsOutputContainingPoint();
  void focusedWindowWithoutTilePositionHasSizeOnly();
  void focusedWindowNullIsAnError();
};
