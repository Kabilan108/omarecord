#include "overlay.hpp"

#include "niri.hpp"
#include "overlay-protocol.hpp"
#include "overlay-state.hpp"
#include "overlay-window.hpp"
#include "rect.hpp"

#include <LayerShellQt/Window>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTextStream>

int runOverlay(const QStringList &arguments) {
  QCommandLineParser parser;
  parser.addOptions({
      {QStringLiteral("output"), QStringLiteral("Output name"), QStringLiteral("name")},
      {QStringLiteral("rect"), QStringLiteral("WxH+X+Y in Niri global coordinates"),
       QStringLiteral("geometry")},
      {QStringLiteral("socket"), QStringLiteral("Unix socket path to listen on"),
       QStringLiteral("path")},
  });
  parser.process(arguments);
  QTextStream err(stderr);

  const QString outputName = parser.value(QStringLiteral("output"));
  const QString socketPath = parser.value(QStringLiteral("socket"));
  const auto rect = parseGeometry(parser.value(QStringLiteral("rect")));
  if (outputName.isEmpty() || socketPath.isEmpty() || !rect) {
    err << "omarecord overlay: --output, --rect WxH+X+Y and --socket are required\n";
    return 2;
  }

  QString error;
  std::optional<OutputInfo> output;
  for (const OutputInfo &candidate : queryOutputs(error)) {
    if (candidate.name == outputName) {
      output = candidate;
    }
  }
  if (!output) {
    err << (error.isEmpty() ? QStringLiteral("Unknown output: %1").arg(outputName) : error)
        << '\n';
    return 2;
  }
  QScreen *screen = nullptr;
  for (QScreen *candidate : QGuiApplication::screens()) {
    if (candidate->name() == outputName) {
      screen = candidate;
    }
  }
  if (!screen) {
    err << "omarecord overlay: Qt has no screen named " << outputName << '\n';
    return 2;
  }

  OverlayState state;
  OverlayProtocol protocol;
  if (!protocol.listen(socketPath, error)) {
    err << "omarecord overlay: cannot listen on " << socketPath << ": " << error << '\n';
    return 2;
  }
  QObject::connect(&protocol, &OverlayProtocol::commandReceived, &state,
                   &OverlayState::applyCommand);
  QObject::connect(&state, &OverlayState::eventRequested, &protocol,
                   &OverlayProtocol::sendEvent);
  QObject::connect(&state, &OverlayState::quitRequestedChanged, qApp,
                   [] { QCoreApplication::exit(0); });

  OverlayWindow window(state, *rect, output->logical);
  window.setScreen(screen);
  static_cast<void>(window.winId());
  QWindow *handle = window.windowHandle();
  LayerShellQt::Window *layer = handle ? LayerShellQt::Window::get(handle) : nullptr;
  if (!handle || !layer) {
    err << "omarecord overlay: cannot create layer surface\n";
    return 2;
  }
  handle->setScreen(screen);
  layer->setScope(QStringLiteral("omarecord-overlay"));
  layer->setScreen(screen);
  layer->setLayer(LayerShellQt::Window::LayerOverlay);
  LayerShellQt::Window::Anchors anchors;
  anchors.setFlag(LayerShellQt::Window::AnchorTop);
  anchors.setFlag(LayerShellQt::Window::AnchorBottom);
  anchors.setFlag(LayerShellQt::Window::AnchorLeft);
  anchors.setFlag(LayerShellQt::Window::AnchorRight);
  layer->setAnchors(anchors);
  layer->setExclusiveZone(-1);
  layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
  layer->setActivateOnShow(false);

  bool ready = false;
  QObject::connect(&window, &OverlayWindow::surfaceReady, &protocol, [&] {
    ready = true;
    protocol.sendEvent(QStringLiteral("ready"));
  });
  QObject::connect(&protocol, &OverlayProtocol::clientConnected, &protocol, [&] {
    if (ready) {
      protocol.sendEvent(QStringLiteral("ready"));
    }
  });
  window.show();
  return QCoreApplication::exec();
}
