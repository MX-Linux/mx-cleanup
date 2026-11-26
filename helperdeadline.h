#pragma once

#include <QDeadlineTimer>
#include <functional>

constexpr int kQuickTimeoutMs = 30000;
constexpr int kDiskScanTimeoutMs = 600000;
constexpr int kNoTimeoutMs = 0;
constexpr int kStopGraceMs = 2000;
constexpr int kTimeoutExit = 124;
constexpr int kUnconfirmedExit = 125;

// Called by the single-threaded helper before initializing Qt. Only the
// worker runs the action. The supervisor exits after collecting its status.
int runWithDeadline(QDeadlineTimer deadline, const std::function<int()> &work);
