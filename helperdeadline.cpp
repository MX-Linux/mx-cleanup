#include "helperdeadline.h"

#include <QFile>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
void waitForChildSignal(const sigset_t &childSignals, QDeadlineTimer deadline)
{
    const qint64 ms = deadline.remainingTime();
    const timespec wait {static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000};
    ::sigtimedwait(&childSignals, nullptr, &wait);
}

// After a parent exits, Linux reparents its remaining descendants to this
// supervisor. This also covers runuser children in a different session.
bool stopChildren(pid_t worker, const sigset_t &childSignals)
{
    ::kill(worker, SIGKILL);
    const QDeadlineTimer grace(kStopGraceMs, Qt::PreciseTimer);
    for (;;) {
        const pid_t reaped = ::waitpid(-1, nullptr, WNOHANG);
        if (reaped > 0 || (reaped < 0 && errno == EINTR)) {
            continue;
        }
        if (reaped < 0 || grace.hasExpired()) {
            return reaped < 0 && errno == ECHILD;
        }
        QFile children("/proc/thread-self/children");
        if (children.open(QIODevice::ReadOnly)) {
            for (const auto &word : children.readAll().simplified().split(' ')) {
                const pid_t pid = word.toInt();
                if (pid > 0) {
                    ::kill(pid, SIGKILL);
                }
            }
        }
        waitForChildSignal(childSignals, grace);
    }
}
}

int runWithDeadline(QDeadlineTimer deadline, const std::function<int()> &work)
{
    if (deadline.hasExpired()) {
        std::fputs("Operation timed out before starting\n", stderr);
        return kTimeoutExit;
    }
    sigset_t childSignals, previous;
    ::sigemptyset(&childSignals);
    ::sigaddset(&childSignals, SIGCHLD);
    if (::signal(SIGCHLD, SIG_DFL) == SIG_ERR || ::prctl(PR_SET_CHILD_SUBREAPER, 1) < 0
        || ::sigprocmask(SIG_BLOCK, &childSignals, &previous) < 0) {
        std::perror("Cannot supervise cleanup");
        return 1;
    }

    const pid_t worker = ::fork();
    if (worker < 0) {
        std::perror("Cannot start cleanup");
        return 1;
    }
    if (worker == 0) {
        ::sigprocmask(SIG_SETMASK, &previous, nullptr);
        ::_exit(work());
    }
    for (;;) {
        int status = 0;
        const pid_t result = ::waitpid(worker, &status, WNOHANG);
        if (result == worker) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        if (result < 0 && errno != EINTR) {
            std::fputs("Cannot confirm termination. Cleanup may still be running.\n", stderr);
            return kUnconfirmedExit;
        }
        if (deadline.hasExpired()) {
            break;
        }
        waitForChildSignal(childSignals, deadline);
    }
    if (stopChildren(worker, childSignals)) {
        std::fputs("Operation timed out\n", stderr);
        return kTimeoutExit;
    }
    std::fputs("Operation timed out. Cleanup may still be running.\n", stderr);
    return kUnconfirmedExit;
}
