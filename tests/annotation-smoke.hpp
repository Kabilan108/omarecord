#pragma once
#include <QObject>

class AnnotationSmoke final : public QObject {
  Q_OBJECT
private slots:
  void fadesAndRemovesStrokes();
  void holdSuspendsFadeAndRestartsClock();
  void clearRemovesEverything();
  void readsFadeSettingsFromEnvironment();
  void drawsStrokeInsideRect();
  void ignoresPressOutsideRect();
  void escapeLeavesDrawMode();
  void keysChangeToolColourAndHold();
  void masksToolbarAndRectWhileDrawing();
  void ticksOnlyWhileFading();
  void showsTooltipAfterHover();
};
