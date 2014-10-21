#include <sched.h>
#include <platform/sched.h>

#include <assert.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>


#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id   _sigev_un._tid
#endif


#define RESCHED_SIG SIGRTMIN


static const clockid_t TIMESLICE_CLOCK = CLOCK_REALTIME;
static const int TIMESLICE_AMOUNT = 50;


static const struct itimerspec TIMESLICE = {
    .it_interval = {
        .tv_sec = 0,
        .tv_nsec = 50000000
    },
    .it_value = {
        .tv_sec=  0,
        .tv_nsec = 50000000
    }
};


static void resched_handler(int signo, siginfo_t* info, void* context) {
    __sched_reschedule_current();
}


void platform_sched_block(platform_sched_t* platform_sched) {
    // TODO: adjusting the sigmask might be slow - alternative solution?
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, RESCHED_SIG);

    int result = pthread_sigmask(SIG_BLOCK, &set, NULL);
    assert(result == 0);
}


void platform_sched_unblock(platform_sched_t* platform_sched) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, RESCHED_SIG);

    int result = pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    assert(result == 0);
}


int platform_sched_init(platform_sched_t* platform_sched) {
    struct sigevent event;

    // TODO: Linux only
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_notify_thread_id = syscall(SYS_gettid);
    event.sigev_signo = RESCHED_SIG;
    event.sigev_value.sival_ptr = (void*)platform_sched;

    int result = timer_create(TIMESLICE_CLOCK, &event, &platform_sched->timer);
    if (result != 0) {
        return -1;
    }

    struct sigaction action;
    action.sa_sigaction = resched_handler;
    action.sa_flags = SA_SIGINFO;

    struct sigaction old_action;

    result = sigaction(RESCHED_SIG, &action, &old_action);
    if (result != 0) {
        result = timer_delete(&platform_sched->timer);
        assert(result == 0);
        return -1;
    }

    result = timer_settime(&platform_sched->timer, 0, &TIMESLICE, NULL);
    if (result != 0) {
        result = timer_delete(&platform_sched->timer);
        assert(result == 0);
        result = sigaction(RESCHED_SIG, &old_action, NULL);
        assert(result == 0);
        return -1;
    }

    return 0;
}
