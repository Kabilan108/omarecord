#pragma once
#include <QObject>

class WindowLocateSmoke final : public QObject {
  Q_OBJECT
private slots:
  void findsExactNeedle();
  void findsPerturbedNeedle();
  void findsTranslucentNeedle();
  void rejectsAbsentNeedle();
  void geometryInsideShadowedBuffer();
  void geometryOfBareBuffer();
  void parsesWindowAndWorkspaces();
};
