/** @fileoverview `omarecord select`: interactive region/window/monitor picker
 * that prints one RegionRect as JSON on stdout. */
#pragma once

#include <QStringList>

/** Runs the select subcommand. Returns 0 with JSON on stdout, 1 on cancel,
 * 2 on error (message on stderr). */
int runSelect(const QStringList &arguments);
