/*
    This file is part of the lvtk_core module for the JUCE Library
    Copyright (c) 2013 - Michael Fisher <mfisher31@gmail.com>.

    Permission to use, copy, modify, and/or distribute this software for any purpose with
    or without fee is hereby granted, provided that the above copyright notice and this
    permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
    TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
    NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
    IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
    CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#if JUCE_DEBUG
#define LVTK_WORKER_LOG(x) Logger::writeToLog(x)
#else
#define LVTK_WORKER_LOG(x) 
#endif

WorkThread::WorkThread (const String& name, uint32 bufsize, int32 priority)
    : Thread (name)
{
    nextWorkId = 0;
    bufferSize = (uint32) nextPowerOfTwo (bufsize);
    requests   = new RingBuffer (bufferSize);
    startThread (priority);
}

WorkThread::~WorkThread()
{
    signalThreadShouldExit();
    doExit = true;
    sem.post();
    waitForThreadToExit (100);
    requests = nullptr;
}

Worker*
WorkThread::getWorker (uint32 workerId) const
{
    if (workerId == 0)
        return nullptr;
    
    for (int i = 0; i < workers.size(); ++i)
        if (workers.getUnchecked(i)->workId == workerId)
            return workers.getUnchecked (i);
    
    return nullptr;
}


void
WorkThread::registerWorker (Worker* worker)
{
    worker->workId = ++nextWorkId;
    LVTK_WORKER_LOG (getThreadName() + " Registering worker: id = " + String (worker->workId));
    workers.addIfNotAlreadyThere (worker);
}

void
WorkThread::removeWorker (Worker* worker)
{
    LVTK_WORKER_LOG (getThreadName() + " Removing worker: id = " + String (worker->workId));
    workers.removeFirstMatchingValue (worker);
    worker->workId = 0;
}


void
WorkThread::run()
{
    HeapBlock<uint8> buffer;
    int32 bufferSize = 0;
    
    while (true)
    {
        sem.wait();
        if (doExit || threadShouldExit()) break;
        
        while (! validateMessage (*requests))
            Thread::sleep (6);
        
        if (doExit || threadShouldExit()) break;
        
        uint32 size = 0;
        if (requests->read (&size, sizeof (size)) < sizeof (size))
        {
            LVTK_WORKER_LOG ("WorkThread: error reading request: message size");
            continue;
        }
        
        uint32 workId;
        if (requests->read (&workId, sizeof (workId)) < sizeof (workId))
        {
            LVTK_WORKER_LOG ("WorkThread: error reading request: worker id");
            continue;
        }
        
        if (workId == 0)
            continue;
        
        if (size > bufferSize)
        {
            bufferSize = nextPowerOfTwo (size);
            buffer.realloc (bufferSize);
        }
        
        if (requests->read (buffer.getData(), size) < size)
        {
            LVTK_WORKER_LOG (getThreadName() + ": error reading request: message body");
            continue;
        }

        LVTK_WORKER_LOG (getThreadName() + ": Finding Worker ID " + String (workId));
        
        {
            ScopedLock lock (workers.getLock());
            if (Worker* const worker = getWorker (workId))
            {
                while (! worker->working.compareAndSetBool (1, 0)) { }
                LVTK_WORKER_LOG("Atomic Flag Set to: " + String(worker->working.get()));
                worker->processRequest (size, buffer.getData());
                while (! worker->working.compareAndSetBool (0, 1)) { }
                LVTK_WORKER_LOG("Atomic Flag Set to: " + String(worker->working.get()));
            }
        }
        
        if (threadShouldExit() || doExit)
            break;
    }
    
    buffer.free();
}


bool
WorkThread::scheduleWork (Worker* worker, uint32 size, const void* data)
{
    jassert (size > 0 && worker && worker->workId != 0);
    if (! requests->canWrite (requiredSpace (size)))
        return false;
    
    if (requests->write (&size, sizeof(size)) < sizeof (uint32))
        return false;
    
    if (requests->write (&worker->workId, sizeof (worker->workId)) < sizeof (worker->workId))
        return false;
    
    if (requests->write (data, size) < size)
        return false;
    
    sem.post();
    return true;
}

bool
WorkThread::validateMessage (RingBuffer& ring)
{
    uint32 size = 0;
    ring.peak (&size, sizeof (size));
    return ring.canRead (requiredSpace (size));
}


Worker::Worker (WorkThread& thread, uint32 bufsize)
    : owner (thread)
{
    working.set (0);
    responses = new RingBuffer (bufsize);
    response.calloc (bufsize);
    thread.registerWorker (this);
}

Worker::~Worker()
{
    LVTK_WORKER_LOG ("Worker delete: working = " + String (working.get()));
    
    while (working.get() != 0) {
        LVTK_WORKER_LOG ("Waiting for work to finish...");
        Thread::sleep (200);
    }
    
    owner.removeWorker (this);
    
    responses = nullptr;
    response.free();
}

bool
Worker::scheduleWork (uint32 size, const void* data)
{
    return owner.scheduleWork (this, size, data);
}

bool
Worker::respondToWork (uint32 size, const void* data)
{
    if (! responses->canWrite (sizeof (size) + size))
        return false;
    
    if (responses->write (&size, sizeof (size)) < sizeof (size))
        return false;
    
    if (responses->write (data, size) < size)
        return false;
    
    return true;
}

void
Worker::processWorkResponses()
{
    uint32 remaining = responses->getReadSpace();
    uint32 size      = 0;
    
    while (remaining >= sizeof (uint32))
    {
        /* respond next cycle if response isn't ready */
        if (! validateMessage (*responses))
            return;
        
        responses->read (&size, sizeof (size));
        responses->read (response.getData(), size);
        processResponse (size, response.getData());
        remaining -= (sizeof (uint32) + size);
    }
}

bool
Worker::validateMessage (RingBuffer& ring)
{
    // the worker only validates message size
    uint32 size = 0;
    ring.peak (&size, sizeof(size));
    return ring.canRead (size + sizeof(size));
}

#if 0

namespace element {


    

    bool
    WorkerThread::schedule_work (uint32_t size, const void* data)
    {
        if (! p_requests->write_space() >= sizeof(size))
            return false;

        if (p_requests->write (sizeof(size), &size) < sizeof(size))
            return false;

        if (p_requests->write (size, data) < size)
            return false;

        m_sem.post();
        return true;
    }

    bool
    WorkerThread::respond (uint32_t size, const void* data)
    {
        if (! p_responses->write_space() >= sizeof(size))
            return false;

        if (p_responses->write (sizeof(size), &size) < sizeof(size))
            return false;

        if (p_responses->write (size, data) < size)
            return false;

        return true;
    }


    bool
    WorkerThread::validate_message (RingBuffer& ring)
    {
        uint32_t available = ring.read_space();
        uint32_t size;

        ring.peek (sizeof(size), &size);
        return (available >= size + sizeof (size));
    }

    void
    WorkerThread::process_responses()
    {
        uint32_t remaining = p_responses->read_space();
        uint32_t size      = 0;

        while (remaining >= sizeof(uint32_t))
        {
            /* respond next cycle if response isn't ready */
            if (! validate_message (*p_responses))
                return;

            p_responses->read (sizeof(uint32_t), &size);
            p_responses->read (size, p_response);

            work.work_response (size, p_response);
            remaining -= (sizeof (uint32_t) + size);
        }
    }

    void
    WorkerThread::run()
    {
        void* buffer = 0;
        uint32_t buffer_size = 0;

        while (true)
        {
            m_sem.wait();

            if (m_exit) break;

            while (! validate_message (*p_requests))
                usleep (2000);

            if (m_exit) break;

            if (p_requests->read_space() < sizeof (uint32_t))
            {
                std::cerr << "Worker: no work data in the buffer\n";
                continue;
            }

            // TODO: validate message is complete

            uint32_t size = 1;
            if (p_requests->read (sizeof(size), &size) < sizeof(size))
            {
                std::cerr << "Worker: couldn't read message size\n";
                continue;
            }

            if (size > buffer_size)
            {
                buffer_size = size;
                buffer = realloc (buffer, buffer_size);
            }

            if (p_requests->read (size, buffer) < size)
            {
                std::cerr << "Worker: error reading message body\n";
                continue;
            }

            work.work (size, buffer);
        }

        std::clog << "Worker: exited, freeing scratch buffer\n";
        free (buffer);
    }
}
#endif