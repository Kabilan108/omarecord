#pragma once
#include <QObject>

class OverlaySmoke final : public QObject {
  Q_OBJECT
private slots:
  void placesToolbarAboveWhenThereIsRoom();
  void placesToolbarBelowWhenRectTouchesTop();
  void collapsesToCornerPillWhenRectCoversOutput();
  void appliesInboundCommands();
  void ignoresMalformedLines();
  void roundTripsOverSocket();
  void windowMasksToolbarOnly();
};
