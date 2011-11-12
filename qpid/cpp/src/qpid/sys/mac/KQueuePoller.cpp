/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */
#include "qpid/sys/Poller.h"
#include "qpid/sys/IOHandle.h"
#include "qpid/sys/Mutex.h"
#include "qpid/sys/AtomicCount.h"
#include "qpid/sys/DeletionManager.h"
#include "qpid/sys/posix/check.h"
#include "qpid/sys/posix/PrivatePosix.h"
#include "qpid/log/Statement.h"

#include <sys/event.h>
#include <errno.h>
#include <signal.h>

#include <assert.h>
#include <queue>
#include <set>
#include <exception>

/** THROW QPID_POSIX_ERROR(errno) if TODO */
#define QPID_KQUEUE_CHECK(RESULT, EVENT) \
    if ((RESULT) != 1 && ((EVENT).flags != EV_ERROR) && ((EVENT).data != 0)) throw QPID_POSIX_ERROR((errno))


namespace qpid {
namespace sys {
    
// Deletion manager to handle deferring deletion of PollerHandles to when they definitely aren't being used
DeletionManager<PollerHandlePrivate> PollerHandleDeletionManager;

//  Instantiate (and define) class static for DeletionManager
template <>
DeletionManager<PollerHandlePrivate>::AllThreadsStatuses DeletionManager<PollerHandlePrivate>::allThreadsStatuses(0);

class PollerHandlePrivate {
    friend class Poller;
    friend class PollerPrivate;
    friend class PollerHandle;

    enum FDStat {
        ABSENT,
        MONITORED,
        INACTIVE,
        HUNGUP,
        MONITORED_HUNGUP,
        INTERRUPTED,
        INTERRUPTED_HUNGUP,
        DELETED
    };

    bool readFilter, writeFilter;
    const IOHandlePrivate* ioHandle;
    PollerHandle* pollerHandle;
    FDStat stat;
    Mutex lock;

    PollerHandlePrivate(const IOHandlePrivate* h, PollerHandle* p) :
      readFilter(false),
      writeFilter(false),
      ioHandle(h),
      pollerHandle(p),
      stat(ABSENT) {
    }

    int fd() const {
        return toFd(ioHandle);
    }

    bool isActive() const {
        return stat == MONITORED || stat == MONITORED_HUNGUP;
    }

    void setActive() {
        stat = (stat == HUNGUP || stat == INTERRUPTED_HUNGUP) 
            ? MONITORED_HUNGUP
            : MONITORED;
    }

    bool isInactive() const {
        return stat == INACTIVE || stat == HUNGUP;
    }

    void setInactive() {
        stat = INACTIVE;
    }

    bool isIdle() const {
        return stat == ABSENT;
    }

    void setIdle() {
        stat = ABSENT;
    }

    bool isHungup() const {
        return
            stat == MONITORED_HUNGUP ||
            stat == HUNGUP ||
            stat == INTERRUPTED_HUNGUP;
    }

    void setHungup() {
        assert(stat == MONITORED);
        stat = HUNGUP;
    }

    bool isInterrupted() const {
        return stat == INTERRUPTED || stat == INTERRUPTED_HUNGUP;
    }

    void setInterrupted() {
        stat = (stat == MONITORED_HUNGUP || stat == HUNGUP)
            ? INTERRUPTED_HUNGUP
            : INTERRUPTED;
    }

    bool isDeleted() const {
        return stat == DELETED;
    }

    void setDeleted() {
        stat = DELETED;
    }
};

PollerHandle::PollerHandle(const IOHandle& h) :
    impl(new PollerHandlePrivate(h.impl, this))
{}

PollerHandle::~PollerHandle() {
    {
        ScopedLock<Mutex> l(impl->lock);
        if (impl->isDeleted()) {
            return;
        }
        impl->pollerHandle = 0;
        if (impl->isInterrupted()) {
            impl->setDeleted();
            return;
        }
        assert(impl->isIdle());
        impl->setDeleted();
    }
    PollerHandleDeletionManager.markForDeletion(impl);
}

class HandleSet
{
    Mutex lock;
    std::set<PollerHandle*> handles;
  public:
    void add(PollerHandle*);
    void remove(PollerHandle*);
    void cleanup();
};

void HandleSet::add(PollerHandle* h)
{
    ScopedLock<Mutex> l(lock);
    handles.insert(h);
}
void HandleSet::remove(PollerHandle* h)
{
    ScopedLock<Mutex> l(lock);
    handles.erase(h);
}
void HandleSet::cleanup()
{
    // Inform all registered handles of disconnection
    std::set<PollerHandle*> copy;
    handles.swap(copy);
    for (std::set<PollerHandle*>::const_iterator i = copy.begin(); i != copy.end(); ++i) {
        Poller::Event event(*i, Poller::DISCONNECTED);
        event.process();
    }
}

/**
 * Concrete implementation of Poller to use the BSD specific kqueue
 * interface
 */
class PollerPrivate {
    friend class Poller;

    struct ReadablePipe {
        int fds[2];

        /**
         * This encapsulates an always readable pipe which we can add
         * to the kqueue set to force kevent to return
         */
        ReadablePipe() {
            QPID_POSIX_CHECK(::pipe(fds));
            // Just write the pipe's fds to the pipe
            QPID_POSIX_CHECK(::write(fds[1], fds, 2));
        }

        ~ReadablePipe() {
            ::close(fds[0]);
            ::close(fds[1]);
        }

        int getFD() {
            return fds[0];
        }
    };

    static ReadablePipe alwaysReadable;
    static int alwaysReadableFd;

    class InterruptHandle: public PollerHandle {
        std::queue<PollerHandle*> handles;

        void processEvent(Poller::EventType) {
            PollerHandle* handle = handles.front();
            handles.pop();
            assert(handle);

            // Synthesise event
            Poller::Event event(handle, Poller::INTERRUPTED);

            // Process synthesised event
            event.process();
        }

    public:
        InterruptHandle() :
            PollerHandle(DummyIOHandle)
        {}

        void addHandle(PollerHandle& h) {
            handles.push(&h);
        }

        PollerHandle* getHandle() {
            PollerHandle* handle = handles.front();
            handles.pop();
            return handle;
        }

        bool queuedHandles() {
            return handles.size() > 0;
        }
    };

    const int kq;
    bool isShutdown;
    InterruptHandle interruptHandle;
    HandleSet registeredHandles;
    AtomicCount threadCount;

    static void directionToKqueueEvent(Poller::Direction dir, bool& readFilter, bool& writeFilter) {
        switch (dir) {
            case Poller::INPUT:
                readFilter = true;
                break;
            case Poller::OUTPUT:
                writeFilter = true;
                break;
            case Poller::INOUT:
                readFilter = true;
                writeFilter = true;
                break;
            default:
                break;
        }
    }

    static Poller::EventType kqueueToDirection(struct kevent& event) {
        switch (event.filter) {
            case EVFILT_READ: return Poller::READABLE;
            case EVFILT_WRITE: return Poller::WRITABLE;
            default:
              return (event.flags & (EV_EOF | EV_ERROR)) ?
                    Poller::DISCONNECTED : Poller::INVALID;
        }
    }

    PollerPrivate() :
        kq(::kqueue()),
        isShutdown(false) {
        QPID_LOG(trace, "PollerPrivate CTOR - created KQ");
        QPID_POSIX_CHECK(kq);        
    }

    ~PollerPrivate() {
        // It's probably okay to ignore any errors here as there can't be data loss
        QPID_LOG(trace, "CLOSING KQ");
        ::close(kq);

        // Need to put the interruptHandle in idle state to delete it
        static_cast<PollerHandle&>(interruptHandle).impl->setIdle();
    }

    void resetMode(PollerHandlePrivate& handle);

    void interrupt() {
        struct kevent change, event;
        EV_SET(&change, alwaysReadableFd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_RECEIPT | EV_ONESHOT, 0, 0, &static_cast<PollerHandle&>(interruptHandle));
        int rc = kevent(kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }

    void interruptAll() {
        struct kevent change, event;
        QPID_LOG(trace, "PollerPrivate interruptAll - setting up kevent");
        EV_SET(&change, alwaysReadableFd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_RECEIPT, 0, 0, 0);
        int rc = kevent(kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }
};

PollerPrivate::ReadablePipe PollerPrivate::alwaysReadable;
int PollerPrivate::alwaysReadableFd = alwaysReadable.getFD();

void Poller::registerHandle(PollerHandle& handle) {
    PollerHandlePrivate& eh = *handle.impl;
    ScopedLock<Mutex> l(eh.lock);
    assert(eh.isIdle());

    impl->registeredHandles.add(&handle);

    eh.setActive();
}

void Poller::unregisterHandle(PollerHandle& handle) {
    PollerHandlePrivate& eh = *handle.impl;
    ScopedLock<Mutex> l(eh.lock);
    assert(!eh.isIdle());

    impl->registeredHandles.remove(&handle);
    
    struct kevent change, event;
    if(eh.readFilter) {
        eh.readFilter = false;
        EV_SET(&change, eh.fd(), EVFILT_READ, EV_DELETE | EV_RECEIPT, 0, 0, &eh);
        int rc = kevent(impl->kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }
    
    if(eh.writeFilter) {
        eh.writeFilter = false;
        EV_SET(&change, eh.fd(), EVFILT_WRITE, EV_DELETE | EV_RECEIPT, 0, 0, &eh);
        int rc = kevent(impl->kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }

    eh.setIdle();
}

void PollerPrivate::resetMode(PollerHandlePrivate& eh) {
    PollerHandle* ph;
    {
        ScopedLock<Mutex> l(eh.lock);
        assert(!eh.isActive());

        if (eh.isIdle() || eh.isDeleted()) {
            return;
        }

        if (!eh.readFilter && !eh.writeFilter) {
            eh.setActive();
            return;
        }

        if (!eh.isInterrupted()) {
            struct kevent change, event;
            if(eh.readFilter) {
                EV_SET(&change, eh.fd(), EVFILT_READ, EV_RECEIPT | EV_ADD | EV_CLEAR, 0, 0, &eh);
                int rc = kevent(kq, &change, 1, &event, 1, NULL);
                QPID_KQUEUE_CHECK(rc, event);
            }
            
            if(eh.writeFilter) {
                EV_SET(&change, eh.fd(), EVFILT_WRITE, EV_RECEIPT | EV_ADD | EV_CLEAR, 0, 0, &eh);
                int rc = kevent(kq, &change, 1, &event, 1, NULL);
                QPID_KQUEUE_CHECK(rc, event);
            }

            eh.setActive();
            return;
        }
        ph = eh.pollerHandle;
    }

    PollerHandlePrivate& ihp = *static_cast<PollerHandle&>(interruptHandle).impl;
    ScopedLock<Mutex> l(ihp.lock);
    interruptHandle.addHandle(*ph);
    ihp.setActive();
    interrupt();
}

void Poller::monitorHandle(PollerHandle& handle, Direction dir) {
    PollerHandlePrivate& eh = *handle.impl;
    ScopedLock<Mutex> l(eh.lock);
    assert(!eh.isIdle());

    bool readFilter = eh.readFilter;
    bool writeFilter = eh.writeFilter;
    
    PollerPrivate::directionToKqueueEvent(dir, eh.readFilter, eh.writeFilter);
    
    // If no change nothing more to do - avoid unnecessary system call
    if (eh.readFilter == readFilter && eh.writeFilter == writeFilter) {
        return;
    }

    // If we're not actually listening wait till we are to perform change
    if (!eh.isActive()) {
        return;
    }
    
    struct kevent change, event;
    if(eh.readFilter) {
        EV_SET(&change, eh.fd(), EVFILT_READ, EV_ADD | EV_RECEIPT | EV_CLEAR, 0, 0, &eh);
        int rc = kevent(impl->kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);        
    }
    
    if(eh.writeFilter) {
        struct kevent change, event;
        EV_SET(&change, eh.fd(), EVFILT_WRITE, EV_ADD | EV_RECEIPT | EV_CLEAR, 0, 0, &eh);
        int rc = kevent(impl->kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }
}

void Poller::unmonitorHandle(PollerHandle& handle, Direction dir) {
    PollerHandlePrivate& eh = *handle.impl;
    ScopedLock<Mutex> l(eh.lock);
    assert(!eh.isIdle());

    bool needToUnmonitorRead = false;
    bool needToUnmonitorWrite = false;
    PollerPrivate::directionToKqueueEvent(dir, needToUnmonitorRead, needToUnmonitorWrite);
    eh.readFilter = !needToUnmonitorRead;
    eh.writeFilter = !needToUnmonitorWrite;

    // If we're not actually listening wait till we are to perform change
    if (!eh.isActive()) {
        return;
    }

    struct kevent change, event;
    if(needToUnmonitorRead) {
        EV_SET(&change, eh.fd(), EVFILT_READ, EV_DELETE | EV_RECEIPT, 0, 0, &eh);
        int rc = kevent(impl->kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }
    
    if(needToUnmonitorWrite) {
        EV_SET(&change, eh.fd(), EVFILT_WRITE, EV_DELETE | EV_RECEIPT, 0, 0, &eh);
        int rc = kevent(impl->kq, &change, 1, &event, 1, NULL);
        QPID_KQUEUE_CHECK(rc, event);
    }
}

void Poller::shutdown() {
    // NB: this function must be async-signal safe, it must not
    // call any function that is not async-signal safe.

    // Allow sloppy code to shut us down more than once
    if (impl->isShutdown)
        return;
    
    // Don't use any locking here - isShutdown will be visible to all
    // after the epoll_ctl() anyway (it's a memory barrier)
    impl->isShutdown = true;

    impl->interruptAll();
}

bool Poller::interrupt(PollerHandle& handle) {
    {
        PollerHandlePrivate& eh = *handle.impl;
        ScopedLock<Mutex> l(eh.lock);
        if (eh.isIdle() || eh.isDeleted()) {
            return false;
        }

        if (eh.isInterrupted()) {
            return true;
        }

        // Stop monitoring handle for read or write
        struct kevent change, event;
        if (eh.readFilter) {
            EV_SET(&change, eh.fd(), EVFILT_READ, EV_DELETE | EV_RECEIPT, 0, 0, &eh);
            int rc = kevent(impl->kq, &change, 1, NULL, 0, NULL);
            QPID_KQUEUE_CHECK(rc, event);
        }
        
        if (eh.readFilter) {
            EV_SET(&change, eh.fd(), EVFILT_WRITE, EV_DELETE | EV_RECEIPT, 0, 0, &eh);
            int rc = kevent(impl->kq, &change, 1, NULL, 0, NULL);
            QPID_KQUEUE_CHECK(rc, event);
        }

        if (eh.isInactive()) {
            eh.setInterrupted();
            return true;
        }
        eh.setInterrupted();
    }

    PollerPrivate::InterruptHandle& ih = impl->interruptHandle;
    PollerHandlePrivate& eh = *static_cast<PollerHandle&>(ih).impl;
    ScopedLock<Mutex> l(eh.lock);
    ih.addHandle(handle);

    impl->interrupt();
    eh.setActive();
    return true;
}

void Poller::run() {
    // Ensure that we exit thread responsibly under all circumstances
    try {
        // Make sure we can't be interrupted by signals at a bad time
        ::sigset_t ss;
        sigfillset(&ss);
        ::pthread_sigmask(SIG_SETMASK, &ss, 0);

        ++(impl->threadCount);
        do {
            Event event = wait();

            // If can read/write then dispatch appropriate callbacks
            if (event.handle) {
                event.process();
            } else {
                // Handle shutdown
                switch (event.type) {
                case SHUTDOWN:
                    PollerHandleDeletionManager.destroyThreadState();
                    //last thread to respond to shutdown cleans up:
                    if (--(impl->threadCount) == 0) impl->registeredHandles.cleanup();
                    return;
                default:
                    // This should be impossible
                    assert(false);
                }
            }
        } while (true);
    } catch (const std::exception& e) {
        QPID_LOG(error, "IO worker thread exiting with unhandled exception: " << e.what());
    }
    PollerHandleDeletionManager.destroyThreadState();
    --(impl->threadCount);
}

bool Poller::hasShutdown()
{
    return impl->isShutdown;
}

Poller::Event Poller::wait(Duration timeout) {
    QPID_TSS(PollerHandlePrivate*, lastReturnedHandle, 0);
    int timeoutSec = (timeout == TIME_INFINITE) ? -1 : timeout / TIME_SEC;
    // long timeoutNs = (timeout == TIME_INFINITE) ? -1 : timeout % TIME_SEC;
    AbsTime targetTimeout = 
        (timeout == TIME_INFINITE) ?
            FAR_FUTURE :
            AbsTime(now(), timeout); 

    if (lastReturnedHandle) {
        impl->resetMode(*lastReturnedHandle);
        lastReturnedHandle = 0;
    }

    // Repeat until we weren't interrupted by signal
    do {
        PollerHandleDeletionManager.markAllUnusedInThisThread();
        struct kevent event;
        int rc = 0;
        if (timeout == TIME_INFINITE) {
            rc = kevent(impl->kq, 0, 0, &event, 1, 0);
        } else {
            struct timespec timeoutSpec;
            timeoutSpec.tv_sec = 0;//timeoutSec;
            timeoutSpec.tv_nsec = 100000000;//timeoutNs;
            rc = kevent(impl->kq, 0, 0, &event, 1, &timeoutSpec);
        }
        
        if (rc ==-1 && errno != EINTR) {
            QPID_POSIX_CHECK(rc);
        } else if (rc > 0) {
            assert(rc == 1);
            
            void* dataPtr = event.udata;

            // Check if this is an interrupt
            PollerPrivate::InterruptHandle& interruptHandle = impl->interruptHandle;
            if (dataPtr == &interruptHandle) {
                QPID_LOG(trace, "dataPtr is interruptHandle");
                // If we are shutting down we need to rearm the shutdown interrupt to
                // ensure everyone still sees it. It's okay that this might be overridden
                // below as we will be back here if it is.
                if (impl->isShutdown) {
                    impl->interruptAll();
                }
                PollerHandle* wrappedHandle = 0;
                {
                ScopedLock<Mutex> l(interruptHandle.impl->lock);
                if (interruptHandle.impl->isActive()) {
                    wrappedHandle = interruptHandle.getHandle();
                    // If there is an interrupt queued behind this one we need to arm it
                    // We do it this way so that another thread can pick it up
                    if (interruptHandle.queuedHandles()) {
                        impl->interrupt();
                        interruptHandle.impl->setActive();
                    } else {
                        interruptHandle.impl->setInactive();
                    }
                }
                }
                if (wrappedHandle) {
                    PollerHandlePrivate& eh = *wrappedHandle->impl;
                    {
                    ScopedLock<Mutex> l(eh.lock);
                    if (!eh.isDeleted()) {
                        if (!eh.isIdle()) {
                            eh.setInactive();
                        }
                        lastReturnedHandle = &eh;
                        assert(eh.pollerHandle == wrappedHandle);
                        return Event(wrappedHandle, INTERRUPTED);
                    }
                    }
                    PollerHandleDeletionManager.markForDeletion(&eh);
                }
                continue;
            }

            // Check for shutdown
            if (impl->isShutdown) {
                PollerHandleDeletionManager.markAllUnusedInThisThread();
                return Event(0, SHUTDOWN);
            }

            PollerHandlePrivate& eh = *static_cast<PollerHandlePrivate*>(dataPtr);
            ScopedLock<Mutex> l(eh.lock);

            // the handle could have gone inactive since we left the epoll_wait
            if (eh.isActive()) {
                PollerHandle* handle = eh.pollerHandle;
                assert(handle);

                // If the connection has been hungup we could still be readable
                // (just not writable), allow us to readable until we get here again
                if (event.flags & EV_EOF) {
                    if (eh.isHungup()) {
                        eh.setInactive();
                        // Don't set up last Handle so that we don't reset this handle
                        // on re-entering Poller::wait. This means that we will never
                        // be set active again once we've returned disconnected, and so
                        // can never be returned again.
                        return Event(handle, DISCONNECTED);
                    }
                    eh.setHungup();
                } else {
                    eh.setInactive();
                }
                lastReturnedHandle = &eh;
                return Event(handle, PollerPrivate::kqueueToDirection(event));
            }
        }
        // We only get here if one of the following:
        // * epoll_wait was interrupted by a signal
        // * epoll_wait timed out
        // * the state of the handle changed after being returned by epoll_wait
        //
        // The only things we can do here are return a timeout or wait more.
        // Obviously if we timed out we return timeout; if the wait was meant to
        // be indefinite then we should never return with a time out so we go again.
        // If the wait wasn't indefinite, we check whether we are after the target wait
        // time or not
        if (timeoutSec == -1) {
            continue;
        }
        if (rc == 0 && now() > targetTimeout) {
            PollerHandleDeletionManager.markAllUnusedInThisThread();
            return Event(0, TIMEOUT);
        }
    } while (true);
}

// Concrete constructors
Poller::Poller() :
    impl(new PollerPrivate())
{}

Poller::~Poller() {
    delete impl;
}

}}