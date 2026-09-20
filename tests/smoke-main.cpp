#include "niri-smoke.hpp"
#include "overlay-smoke.hpp"
#include "rect-smoke.hpp"

#include <QApplication>
#include <QTest>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  int status = 0;
  {
    RectSmoke test;
    status |= QTest::qExec(&test, argc, argv);
  }
  {
    NiriSmoke test;
    status |= QTest::qExec(&test, argc, argv);
  }
  {
    OverlaySmoke test;
    status |= QTest::qExec(&test, argc, argv);
  }
  return status;
}
