/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define ST_UTIME_MILLISECONDS 1000
#define SELECT_TEST_TIMEOUT (1000 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the select event system, the portable one: the only one on Cygwin, and the one an OS thread gets when
// it calls st_init without choosing. It watches at most FD_SETSIZE descriptors, so its st_init lowers the descriptor
// limit of the whole process to FD_SETSIZE, for good. The test process keeps epoll or kqueue, so each test runs a new
// OS thread with its own select-based ST in a forked child, and the child reports what it saw through shared memory.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A result shared with the forked child; T must be plain data, since the child's heap is its own.
template <typename T>
struct SelectTestShared {
    T* p_;
    SelectTestShared() : p_(NULL) {
        void* m = mmap(NULL, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
        if (m != MAP_FAILED) p_ = new (m) T();
    }
    ~SelectTestShared() {
        if (p_) munmap(p_, sizeof(T));
    }
};

struct SelectTestChild {
    void (*body_)(void*);
    void* arg_;
    int done_[2];
};

static void* select_test_thread(void* arg)
{
    SelectTestChild* c = (SelectTestChild*)arg;
    c->body_(c->arg_);

    char done = 1;
    if (::write(c->done_[1], &done, 1) != 1) _exit(1);

    // ST can't free an instance, so keep the thread and its instance alive until the child exits.
    for (;;) pause();
    return NULL;
}

// Runs body on a new OS thread in a forked child, and returns the child's exit status, or -1 if it didn't exit. The
// thread has no ST yet: body chooses the event system and calls st_init itself.
static int select_test_run(void (*body)(void*), void* arg)
{
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // A hang kills the child and fails the test, instead of hanging the suite.
        alarm(5);

        SelectTestChild c;
        c.body_ = body;
        c.arg_ = arg;
        if (pipe(c.done_) < 0) _exit(1);

        pthread_t trd;
        if (pthread_create(&trd, NULL, select_test_thread, &c) != 0) _exit(1);

        char done = 0;
        if (::read(c.done_[0], &done, 1) != 1) _exit(1);

        // Not _exit, so a coverage build writes the child's counters.
        exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct SelectTestChoice {
    int before_;
    char before_name_[16];
    int unknown_r0_;
    int unknown_errno_;
    int after_unknown_;
    int init_r0_;
    int chosen_;
    char chosen_name_[16];
    int alt_r0_;
    int alt_errno_;
};

static void select_test_choice(void* arg)
{
    SelectTestChoice* r = (SelectTestChoice*)arg;

    r->before_ = st_get_eventsys();
    strncpy(r->before_name_, st_get_eventsys_name(), sizeof(r->before_name_) - 1);

    errno = 0;
    r->unknown_r0_ = st_set_eventsys(100);
    r->unknown_errno_ = errno;
    r->after_unknown_ = st_get_eventsys();

    r->init_r0_ = st_init();
    r->chosen_ = st_get_eventsys();
    strncpy(r->chosen_name_, st_get_eventsys_name(), sizeof(r->chosen_name_) - 1);

    errno = 0;
    r->alt_r0_ = st_set_eventsys(ST_EVENTSYS_ALT);
    r->alt_errno_ = errno;
}

// A new OS thread has no event system until it chooses one: st_get_eventsys returns -1 and the name is empty. A number
// ST doesn't know fails with EINVAL and chooses nothing. A thread that calls st_init without choosing, such as a library
// that starts ST on its own thread, runs on select. After st_init the choice is fixed, and asking for the other one
// fails with EBUSY. Locks in current behavior.
VOID TEST(SelectTest, ThreadWithoutChoiceRunsOnSelect)
{
    SelectTestShared<SelectTestChoice> r;
    ASSERT_TRUE(r.p_ != NULL);
    ASSERT_EQ(0, select_test_run(select_test_choice, r.p_));

    EXPECT_EQ(-1, r.p_->before_);
    EXPECT_STREQ("", r.p_->before_name_);

    EXPECT_EQ(-1, r.p_->unknown_r0_);
    EXPECT_EQ(EINVAL, r.p_->unknown_errno_);
    EXPECT_EQ(-1, r.p_->after_unknown_);

    EXPECT_EQ(0, r.p_->init_r0_);
    EXPECT_EQ(ST_EVENTSYS_SELECT, r.p_->chosen_);
    EXPECT_STREQ("select", r.p_->chosen_name_);

    EXPECT_EQ(-1, r.p_->alt_r0_);
    EXPECT_EQ(EBUSY, r.p_->alt_errno_);
}

struct SelectTestLimit {
    rlim_t hard_before_;
    int set_r0_;
    int init_r0_;
    rlim_t soft_after_;
    rlim_t hard_after_;
    int fdlimit_;
};

static void select_test_limit(void* arg)
{
    SelectTestLimit* r = (SelectTestLimit*)arg;

    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return;
    r->hard_before_ = rlim.rlim_max;

    r->set_r0_ = st_set_eventsys(ST_EVENTSYS_SELECT);
    r->init_r0_ = st_init();

    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return;
    r->soft_after_ = rlim.rlim_cur;
    r->hard_after_ = rlim.rlim_max;
    r->fdlimit_ = st_getfdlimit();
}

// A server that chooses select, as SRS does on Cygwin, can't watch a descriptor numbered FD_SETSIZE or above, so
// st_init lowers the process's hard limit of open descriptors to FD_SETSIZE when it is higher, raises the soft limit to
// the hard one, and st_getfdlimit reports it. The process can't raise the hard limit again, which is why these tests
// run in a child. Locks in current behavior.
VOID TEST(SelectTest, SelectLimitsProcessDescriptors)
{
    SelectTestShared<SelectTestLimit> r;
    ASSERT_TRUE(r.p_ != NULL);
    ASSERT_EQ(0, select_test_run(select_test_limit, r.p_));

    EXPECT_EQ(0, r.p_->set_r0_);
    EXPECT_EQ(0, r.p_->init_r0_);

    rlim_t expected = r.p_->hard_before_ > (rlim_t)FD_SETSIZE ? (rlim_t)FD_SETSIZE : r.p_->hard_before_;
    EXPECT_EQ(expected, r.p_->hard_after_);
    EXPECT_EQ(expected, r.p_->soft_after_);
    EXPECT_EQ((int)expected, r.p_->fdlimit_);
}

struct SelectTestRead {
    int r0_;
    int errno_;
    char data_;
    bool done_;
};

struct SelectTestReader {
    st_netfd_t stfd_;
    st_utime_t timeout_;
    SelectTestRead* result_;
};

static void* select_test_reader_coroutine(void* arg)
{
    SelectTestReader* r = (SelectTestReader*)arg;
    errno = 0;
    r->result_->r0_ = (int)st_read(r->stfd_, &r->result_->data_, 1, r->timeout_);
    r->result_->errno_ = errno;
    r->result_->done_ = true;
    return NULL;
}

struct SelectTestJobs {
    int init_r0_;
    SelectTestRead patient_;
    SelectTestRead impatient_;
    // Whether the patient worker was still waiting when the impatient one gave up.
    bool patient_waited_;
    int busy_r0_;
    int busy_errno_;
    int close_r0_;
};

static void* select_test_feeder(void* arg)
{
    // Long enough that the scheduler is waiting in select by then, with nothing on a timer.
    usleep(20 * 1000);
    int fd = *(int*)arg;
    if (::write(fd, "a", 1) != 1) return NULL;
    return NULL;
}

static void select_test_jobs(void* arg)
{
    SelectTestJobs* r = (SelectTestJobs*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (pipe(fds) < 0) return;
    st_netfd_t jobs = st_netfd_open(fds[0]);
    if (!jobs) return;

    SelectTestReader patient = {jobs, ST_UTIME_NO_TIMEOUT, &r->patient_};
    SelectTestReader impatient = {jobs, 10 * ST_UTIME_MILLISECONDS, &r->impatient_};
    st_thread_t patient_trd = st_thread_create(select_test_reader_coroutine, &patient, 1, 0);
    st_thread_t impatient_trd = st_thread_create(select_test_reader_coroutine, &impatient, 1, 0);
    if (!patient_trd || !impatient_trd) return;

    // The impatient worker gives up, and the patient one keeps waiting on the same pipe.
    st_thread_join(impatient_trd, NULL);
    r->patient_waited_ = !r->patient_.done_;

    // The pipe can't be closed while a worker waits on it.
    errno = 0;
    r->busy_r0_ = st_netfd_close(jobs);
    r->busy_errno_ = errno;

    // A job arrives from another OS thread while nothing sleeps on a timer.
    pthread_t feeder;
    if (pthread_create(&feeder, NULL, select_test_feeder, &fds[1]) != 0) return;
    st_thread_join(patient_trd, NULL);
    pthread_join(feeder, NULL);

    r->close_r0_ = st_netfd_close(jobs);
    ::close(fds[1]);
}

// Two workers wait for jobs on one pipe, one with no timeout and one for 10 ms. Select counts the waiters of each
// descriptor, so when the impatient worker times out with ETIME and leaves, the pipe stays registered for the patient
// one. While it waits, closing the pipe fails with EBUSY. With every coroutine waiting on I/O and none on a timer,
// select waits with no timeout, and a job written by another OS thread wakes the patient worker with it. Then the pipe
// closes. Locks in current behavior.
VOID TEST(SelectTest, TimedOutWorkerLeavesOtherWaiting)
{
    SelectTestShared<SelectTestJobs> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->busy_r0_ = r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_jobs, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_TRUE(r.p_->impatient_.done_);
    EXPECT_EQ(-1, r.p_->impatient_.r0_);
    EXPECT_EQ(ETIME, r.p_->impatient_.errno_);
    EXPECT_TRUE(r.p_->patient_waited_);

    EXPECT_EQ(-1, r.p_->busy_r0_);
    EXPECT_EQ(EBUSY, r.p_->busy_errno_);

    EXPECT_TRUE(r.p_->patient_.done_);
    EXPECT_EQ(1, r.p_->patient_.r0_);
    EXPECT_EQ('a', r.p_->patient_.data_);
    EXPECT_EQ(0, r.p_->close_r0_);
}

struct SelectTestConn {
    int init_r0_;
    bool filled_;
    // Whether each side was done after 10 ms, after the peer sent a byte, and after the peer read everything.
    bool received_[3];
    bool sent_[3];
    SelectTestRead receive_;
    SelectTestRead send_;
    int close_r0_;
};

struct SelectTestSender {
    st_netfd_t stfd_;
    SelectTestRead* result_;
};

static void* select_test_sender_coroutine(void* arg)
{
    SelectTestSender* s = (SelectTestSender*)arg;
    errno = 0;
    s->result_->r0_ = (int)st_write(s->stfd_, "z", 1, SELECT_TEST_TIMEOUT);
    s->result_->errno_ = errno;
    s->result_->done_ = true;
    return NULL;
}

static void select_test_conn(void* arg)
{
    SelectTestConn* r = (SelectTestConn*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return;
    int sndbuf = 4096;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    // The peer reads nothing, so the connection's send buffer fills up.
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    while (::write(fds[0], buf, sizeof(buf)) > 0) {
    }
    r->filled_ = (errno == EAGAIN || errno == EWOULDBLOCK);

    SelectTestReader receiver = {conn, SELECT_TEST_TIMEOUT, &r->receive_};
    SelectTestSender sender = {conn, &r->send_};
    st_thread_t receive_trd = st_thread_create(select_test_reader_coroutine, &receiver, 1, 0);
    st_thread_t send_trd = st_thread_create(select_test_sender_coroutine, &sender, 1, 0);
    if (!receive_trd || !send_trd) return;

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[0] = r->receive_.done_;
    r->sent_[0] = r->send_.done_;

    // The peer sends a byte, and only the receiver wakes.
    if (::write(fds[1], "x", 1) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[1] = r->receive_.done_;
    r->sent_[1] = r->send_.done_;

    // The peer reads everything, and the sender wakes.
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
    while (::read(fds[1], buf, sizeof(buf)) > 0) {
    }
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[2] = r->receive_.done_;
    r->sent_[2] = r->send_.done_;

    st_thread_join(receive_trd, NULL);
    st_thread_join(send_trd, NULL);
    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);
}

// A connection has a receive coroutine and a send coroutine, the way SRS serves a client, and the peer reads nothing,
// so the send buffer is full. The receiver waits to read and the sender waits to write the same descriptor. When the
// peer sends a byte, only the receiver wakes, with it, and the sender keeps waiting. When the peer reads everything,
// the sender wakes and writes its byte. Then the connection closes. Locks in current behavior.
VOID TEST(SelectTest, ReceiverAndSenderShareConnection)
{
    SelectTestShared<SelectTestConn> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_conn, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);
    ASSERT_TRUE(r.p_->filled_);

    EXPECT_FALSE(r.p_->received_[0]);
    EXPECT_FALSE(r.p_->sent_[0]);

    EXPECT_TRUE(r.p_->received_[1]);
    EXPECT_FALSE(r.p_->sent_[1]);
    EXPECT_EQ(1, r.p_->receive_.r0_);
    EXPECT_EQ('x', r.p_->receive_.data_);

    EXPECT_TRUE(r.p_->sent_[2]);
    EXPECT_EQ(1, r.p_->send_.r0_);
    EXPECT_EQ(0, r.p_->close_r0_);
}

struct SelectTestRejects {
    int init_r0_;
    // st_poll with a descriptor of FD_SETSIZE, a negative one, no events, and POLLRDNORM.
    int r0_[4];
    int errno_[4];
    int close_r0_;
};

static void select_test_rejects(void* arg)
{
    SelectTestRejects* r = (SelectTestRejects*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (pipe(fds) < 0) return;
    st_netfd_t reader = st_netfd_open(fds[0]);
    if (!reader) return;

    struct pollfd bad[4];
    bad[0].fd = FD_SETSIZE;
    bad[0].events = POLLIN;
    bad[1].fd = -1;
    bad[1].events = POLLIN;
    bad[2].fd = fds[1];
    bad[2].events = 0;
    bad[3].fd = fds[1];
    bad[3].events = POLLRDNORM;

    for (int i = 0; i < 4; i++) {
        // A valid descriptor first, so a half-registered set would leave it counted.
        struct pollfd pds[2];
        pds[0].fd = fds[0];
        pds[0].events = POLLIN;
        pds[1] = bad[i];

        errno = 0;
        r->r0_[i] = st_poll(pds, 2, SELECT_TEST_TIMEOUT);
        r->errno_[i] = errno;
    }

    r->close_r0_ = st_netfd_close(reader);
    ::close(fds[1]);
}

// Select can only watch descriptors below FD_SETSIZE for reading, writing or priority data. A st_poll set with a
// descriptor of FD_SETSIZE or above, a negative one, no events, or another event such as POLLRDNORM, fails at once with
// EINVAL, and the valid descriptor before it is left unregistered, so it then closes without EBUSY. Locks in current
// behavior.
VOID TEST(SelectTest, PollRejectsWhatSelectCannotWatch)
{
    SelectTestShared<SelectTestRejects> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_rejects, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(-1, r.p_->r0_[i]) << "case " << i;
        EXPECT_EQ(EINVAL, r.p_->errno_[i]) << "case " << i;
    }
    EXPECT_EQ(0, r.p_->close_r0_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for waiting for TCP urgent data with POLLPRI on select, such as a telnet server whose user presses
// interrupt: the client sends one urgent byte with send(MSG_OOB) on the connection that also carries its commands.
// Select watches the connection in its exception set and counts those waiters apart from the reading and writing ones.
// Unlike kqueue, which rejects POLLPRI, select runs these on macOS too.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Connects a TCP pair on loopback with plain blocking calls: fds[0] is the server side, fds[1] the client side.
static bool select_test_tcp_pair(int fds[2])
{
    fds[0] = fds[1] = -1;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addrlen = sizeof(addr);
    if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, 1) < 0
        || getsockname(lfd, (sockaddr*)&addr, &addrlen) < 0) {
        ::close(lfd);
        return false;
    }

    fds[1] = socket(AF_INET, SOCK_STREAM, 0);
    if (fds[1] >= 0 && ::connect(fds[1], (sockaddr*)&addr, sizeof(addr)) == 0) {
        fds[0] = ::accept(lfd, NULL, NULL);
    }
    ::close(lfd);
    return fds[0] >= 0;
}

struct SelectTestTelnet {
    int init_r0_;
    // st_poll for a command or an abort: while the client is idle, after a command, and after an urgent byte.
    int r0_[3];
    short revents_[3];
    char command_;
    char urgent_;
    int close_r0_;
};

static void select_test_telnet(void* arg)
{
    SelectTestTelnet* r = (SelectTestTelnet*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (!select_test_tcp_pair(fds)) return;
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    struct pollfd pd;
    pd.fd = fds[0];
    pd.events = POLLIN | POLLPRI;

    // The client is idle, so the wait times out.
    pd.revents = 0;
    r->r0_[0] = st_poll(&pd, 1, 10 * ST_UTIME_MILLISECONDS);
    r->revents_[0] = pd.revents;

    // A command arrives, and the server reads it.
    if (::send(fds[1], "a", 1, 0) != 1) return;
    pd.revents = 0;
    r->r0_[1] = st_poll(&pd, 1, SELECT_TEST_TIMEOUT);
    r->revents_[1] = pd.revents;
    if (st_read(conn, &r->command_, 1, SELECT_TEST_TIMEOUT) != 1) return;

    // The client aborts, and the server reads the urgent byte.
    if (::send(fds[1], "!", 1, MSG_OOB) != 1) return;
    pd.revents = 0;
    r->r0_[2] = st_poll(&pd, 1, SELECT_TEST_TIMEOUT);
    r->revents_[2] = pd.revents;
    if (::recv(fds[0], &r->urgent_, 1, MSG_OOB) != 1) return;

    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);
}

// The server waits for a command or an abort in one st_poll, like the select loop of a telnet server with the
// connection in both the read and the exception set, and an idle timeout. While the client is idle, the wait returns 0.
// A command wakes it with only POLLIN, and it reads the command. Then the client sends an urgent byte, which wakes it
// with only POLLPRI, and recv with MSG_OOB gets the byte. The connection then closes without EBUSY, so neither the
// timeout nor the wakeups left it registered. Locks in current behavior.
VOID TEST(SelectTest, CommandOrAbortInOneWait)
{
    SelectTestShared<SelectTestTelnet> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_telnet, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_EQ(0, r.p_->r0_[0]);
    EXPECT_EQ(0, r.p_->revents_[0]);

    EXPECT_EQ(1, r.p_->r0_[1]);
    EXPECT_EQ(POLLIN, r.p_->revents_[1]);
    EXPECT_EQ('a', r.p_->command_);

    EXPECT_EQ(1, r.p_->r0_[2]);
    EXPECT_EQ(POLLPRI, r.p_->revents_[2]);
    EXPECT_EQ('!', r.p_->urgent_);

    EXPECT_EQ(0, r.p_->close_r0_);
}

struct SelectTestAbortWaiter {
    int fd_;
    int r0_;
    short revents_;
    bool done_;
};

static void* select_test_abort_waiter_coroutine(void* arg)
{
    SelectTestAbortWaiter* w = (SelectTestAbortWaiter*)arg;
    struct pollfd pd;
    pd.fd = w->fd_;
    pd.events = POLLPRI;
    pd.revents = 0;
    w->r0_ = st_poll(&pd, 1, SELECT_TEST_TIMEOUT);
    w->revents_ = pd.revents;
    w->done_ = true;
    return NULL;
}

struct SelectTestShare {
    int init_r0_;
    // Whether each one was done after 10 ms, after a command, and after an urgent byte.
    bool read_[3];
    bool aborted_[3];
    SelectTestRead command_;
    SelectTestAbortWaiter abort_;
    char urgent_;
    int close_r0_;
};

static void select_test_share(void* arg)
{
    SelectTestShare* r = (SelectTestShare*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (!select_test_tcp_pair(fds)) return;
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    SelectTestReader reader = {conn, SELECT_TEST_TIMEOUT, &r->command_};
    r->abort_.fd_ = fds[0];
    st_thread_t reader_trd = st_thread_create(select_test_reader_coroutine, &reader, 1, 0);
    st_thread_t abort_trd = st_thread_create(select_test_abort_waiter_coroutine, &r->abort_, 1, 0);
    if (!reader_trd || !abort_trd) return;

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[0] = r->command_.done_;
    r->aborted_[0] = r->abort_.done_;

    // A command arrives, and only the reader wakes.
    if (::send(fds[1], "a", 1, 0) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[1] = r->command_.done_;
    r->aborted_[1] = r->abort_.done_;

    // The client aborts, and the abort waiter wakes.
    if (::send(fds[1], "!", 1, MSG_OOB) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[2] = r->command_.done_;
    r->aborted_[2] = r->abort_.done_;

    st_thread_join(reader_trd, NULL);
    st_thread_join(abort_trd, NULL);
    if (::recv(fds[0], &r->urgent_, 1, MSG_OOB) != 1) return;

    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);
}

// One coroutine reads commands from the connection while another waits for an abort on it, so select watches the
// connection in both the read and the exception set. A command wakes only the reader, with the command, and the abort
// waiter keeps waiting. Then the client sends an urgent byte, which wakes the abort waiter with only POLLPRI, and recv
// with MSG_OOB gets it. The connection then closes without EBUSY, so both counts are back to 0. Locks in current
// behavior.
VOID TEST(SelectTest, ReaderAndAbortWaiterShareConnection)
{
    SelectTestShared<SelectTestShare> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_share, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_FALSE(r.p_->read_[0]);
    EXPECT_FALSE(r.p_->aborted_[0]);

    EXPECT_TRUE(r.p_->read_[1]);
    EXPECT_FALSE(r.p_->aborted_[1]);
    EXPECT_EQ(1, r.p_->command_.r0_);
    EXPECT_EQ('a', r.p_->command_.data_);

    EXPECT_TRUE(r.p_->aborted_[2]);
    EXPECT_EQ(1, r.p_->abort_.r0_);
    EXPECT_EQ(POLLPRI, r.p_->abort_.revents_);
    EXPECT_EQ('!', r.p_->urgent_);

    EXPECT_EQ(0, r.p_->close_r0_);
}
