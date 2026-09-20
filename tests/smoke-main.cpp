#include "niri-smoke.hpp"
#include "rect-smoke.hpp"
#include "select-smoke.hpp"

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
    SelectSmoke test;
    status |= QTest::qExec(&test, argc, argv);
  }
  return status;
}
