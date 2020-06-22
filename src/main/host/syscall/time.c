/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/time.h"

#include <errno.h>
#include <stddef.h>

#include "main/core/worker.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

/* make sure we return the 'emulated' time, and not the actual simulation clock
 */
static EmulatedTime _syscallhandler_getEmulatedTime() {
    return worker_getEmulatedTime();
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[0].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Grab the arg from the syscall register. */
    const struct timespec* req =
        thread_getReadablePtr(sys->thread, args->args[0].as_ptr, sizeof(*req));

    /* Bounds checking. */
    if (!(req->tv_nsec >= 0 && req->tv_nsec <= 999999999)) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Does the timeout request require us to block? */
    int requestToBlock = req->tv_sec > 0 || req->tv_nsec > 0;

    /* Did we already block? */
    int wasBlocked = _syscallhandler_wasBlocked(sys);

    if (requestToBlock && !wasBlocked) {
        /* We need to block for a while following the requested timeout. */
        _syscallhandler_setListenTimeout(sys, req);

        /* tell the thread we blocked it */
        return (SysCallReturn){
            .state = SYSCALL_BLOCK,
            .cond = syscallcondition_new(sys->timer, NULL, DS_NONE)};
    }

    /* If needed, verify that the timer expired correctly. */
    if (requestToBlock && wasBlocked) {
        /* Make sure we don't have a pending timer. */
        if (_syscallhandler_isListenTimeoutPending(sys)) {
            error("nanosleep unblocked but a timer is still pending.");
        }

        /* The timer must have expired. */
        if (!_syscallhandler_didListenTimeoutExpire(sys)) {
            error("nanosleep unblocked but the timer did not expire.");
        }
    }

    /* The syscall is now complete. */
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys,
                                           const SysCallArgs* args) {
    clockid_t clk_id = args->args[0].as_u64;
    debug("syscallhandler_clock_gettime with %d %p", clk_id,
          GUINT_TO_POINTER(args->args[1].as_ptr.val));

    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[1].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    struct timespec* res_timespec = thread_getWriteablePtr(
        sys->thread, args->args[1].as_ptr, sizeof(*res_timespec));

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    res_timespec->tv_sec = now / SIMTIME_ONE_SECOND;
    res_timespec->tv_nsec = now % SIMTIME_ONE_SECOND;

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}