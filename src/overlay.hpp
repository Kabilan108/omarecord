/** @fileoverview `omarecord overlay`: long-running recording overlay (border,
 * toolbar, annotations) controlled over a local socket. */
#pragma once

#include <QStringList>

/** Runs the overlay subcommand until told to quit. Returns 0 on quit, 2 on
 * bad arguments or if the surface cannot be created. */
int runOverlay(const QStringList &arguments);
