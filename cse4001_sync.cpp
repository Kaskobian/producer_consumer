// cse4001_sync.cpp
// Compile: make
// Runs: ./cse4001_sync <problem#>
// Problem 1: No-starve readers-writers (5 readers, 5 writers)
// Problem 2: Writer-priority readers-writers (5 readers, 5 writers)
// Problem 3: Dining philosophers solution #1 (waiter limiting concurrency)
// Problem 4: Dining philosophers solution #2 (odd/even asymmetric pickup)

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <iostream>
#include <vector>
using namespace std;

/* Simple semaphore wrapper */
class Semaphore {
public:
    Semaphore(int initialValue = 0) { sem_init(&mSemaphore, 0, initialValue); }
    ~Semaphore() { sem_destroy(&mSemaphore); }
    void wait() { sem_wait(&mSemaphore); }
    void signal() { sem_post(&mSemaphore); }
private:
    sem_t mSemaphore;
};

// General Utilities
void msleep(int ms) { usleep(ms * 1000); }

//Problem 1: No-starve Readers/Writers
/*
Downey’s no-starvation solution relies on a “turnstile” semaphore
that every reader and writer must pass through so that no one
gets stuck waiting forever.

In this version, we use:
 - turnstile: a semaphore (initialized to 1) that keeps readers
   and writers lined up fairly
 - roomEmpty: a semaphore (also 1) that writers use to ensure they
   have exclusive access
 - readCount: a shared counter protected by 'mutex' so readers
   can safely track how many are inside
*/

namespace NoStarveRW {
    const int NUM_READERS = 5;
    const int NUM_WRITERS = 5;
    Semaphore turnstile(1);
    Semaphore roomEmpty(1);
    Semaphore rMutex(1); // protects readCount
    int readCount = 0;

    void *reader(void *id) {
        long rid = (long)id;
        for (int i = 0; i < 5; ++i) {
            msleep(100 + (rid * 20)); // stagger
            // Readers pass through turnstile to avoid writer starvation
            turnstile.wait();
            turnstile.signal();

            rMutex.wait();
            readCount++;
            if (readCount == 1) roomEmpty.wait(); // first reader locks out writers
            rMutex.signal();

            printf("Reader %ld: reading (iteration %d). readCount=%d\n", rid, i+1, readCount);
            fflush(stdout);
            msleep(150);

            rMutex.wait();
            readCount--;
            if (readCount == 0) roomEmpty.signal(); // last reader leaves
            rMutex.signal();

            msleep(100);
        }
        printf("Reader %ld: done\n", rid); fflush(stdout);
        return NULL;
    }

    void *writer(void *id) {
        long wid = (long)id;
        for (int i = 0; i < 5; ++i) {
            msleep(120 + (wid * 30));
            // Writers enter the turnstile to queue up with readers
            turnstile.wait();
            roomEmpty.wait(); // wait for readers to leave
            printf("Writer %ld: writing (iteration %d)\n", wid, i+1);
            fflush(stdout);
            msleep(200);
            roomEmpty.signal();
            turnstile.signal();
            msleep(150);
        }
        printf("Writer %ld: done\n", wid); fflush(stdout);
        return NULL;
    }

    void run_demo() {
        printf("=== No-starve Readers-Writers Demo (5 readers, 5 writers) ===\n");
        pthread_t rthreads[NUM_READERS], wthreads[NUM_WRITERS];
        for (long i = 0; i < NUM_READERS; ++i)
            pthread_create(&rthreads[i], NULL, reader, (void*)(i+1));
        for (long i = 0; i < NUM_WRITERS; ++i)
            pthread_create(&wthreads[i], NULL, writer, (void*)(i+1));
        for (int i = 0; i < NUM_READERS; ++i) pthread_join(rthreads[i], NULL);
        for (int i = 0; i < NUM_WRITERS; ++i) pthread_join(wthreads[i], NULL);
        printf("=== No-starve demo finished ===\n\n");
    }
}

//Problem 2: Writer-priority Readers/Writers
/*
This is the classic writer-priority version of the readers-writers
problem. In this approach, readers must wait whenever a writer is
either actively writing or waiting to write.

We use the following synchronization tools:
 - mutex: protects shared counters
 - writeLock: a semaphore that gives writers exclusive access
 - readTry: a semaphore that blocks readers whenever a writer
   is waiting (works like a gate)

High-level idea:
  - Writer: increase waitingWriters under mutex; if it’s the first
    one waiting, it closes the readTry gate. Then it waits on
    writeLock, performs the write, and on exit reduces
    waitingWriters—reopening readTry if no writers remain.
    
  - Reader: waits on readTry to ensure no writer is waiting, performs
    the usual reader entry steps, then releases readTry so other
    readers can continue. (This is a common pattern in writer-priority
    solutions.)

The code below shows a straightforward implementation of this
writer-priority behavior.
*/

namespace WriterPriorityRW {
    const int NUM_READERS = 5;
    const int NUM_WRITERS = 5;

    Semaphore mutex(1);     // protects counters
    Semaphore writeLock(1); // exclusive access for writers
    Semaphore readTry(1);   // block readers when writers waiting
    int readCount = 0;
    int waitingWriters = 0;

    void *reader(void *id) {
        long rid = (long)id;
        for (int i=0;i<5;++i) {
            msleep(80 + rid*10);
            readTry.wait(); // check if writers are waiting
            // now safe to manipulate readCount
            mutex.wait();
            readCount++;
            if (readCount == 1) writeLock.wait(); // first reader locks out writers
            mutex.signal();
            readTry.signal();

            printf("Reader %ld: reading (iteration %d). readCount=%d\n", rid, i+1, readCount);
            fflush(stdout);
            msleep(160);

            mutex.wait();
            readCount--;
            if (readCount == 0) writeLock.signal(); // last reader leaves
            mutex.signal();
            msleep(60);
        }
        printf("Reader %ld: done\n", rid); fflush(stdout);
        return NULL;
    }

    void *writer(void *id) {
        long wid = (long)id;
        for (int i=0;i<5;++i) {
            msleep(140 + wid*30);
            mutex.wait();
            waitingWriters++;
            if (waitingWriters == 1) readTry.wait(); // first waiting writer blocks readers
            mutex.signal();

            writeLock.wait();
            printf("Writer %ld: writing (iteration %d)\n", wid, i+1);
            fflush(stdout);
            msleep(220);
            writeLock.signal();

            mutex.wait();
            waitingWriters--;
            if (waitingWriters == 0) readTry.signal(); // last waiting writer lets readers through
            mutex.signal();
            msleep(100);
        }
        printf("Writer %ld: done\n", wid); fflush(stdout);
        return NULL;
    }

    void run_demo() {
        printf("=== Writer-priority Readers-Writers Demo (5 readers, 5 writers) ===\n");
        pthread_t rthreads[NUM_READERS], wthreads[NUM_WRITERS];
        for (long i = 0; i < NUM_READERS; ++i)
            pthread_create(&rthreads[i], NULL, reader, (void*)(i+1));
        for (long i = 0; i < NUM_WRITERS; ++i)
            pthread_create(&wthreads[i], NULL, writer, (void*)(i+1));
        for (int i = 0; i < NUM_READERS; ++i) pthread_join(rthreads[i], NULL);
        for (int i = 0; i < NUM_WRITERS; ++i) pthread_join(wthreads[i], NULL);
        printf("=== Writer-priority demo finished ===\n\n");
    }
}

// Problem 3: Dining Philosophers — Solution #1
/*
Solution #1 uses a “waiter” (also called a footman) who limits the
number of philosophers allowed to sit at the table at the same time.
By letting at most N-1 philosophers sit, we prevent the circular
waiting that leads to deadlock.

Each chopstick is represented by its own semaphore. A philosopher
requests permission from the waiter, picks up the left and right
chopsticks, eats, then puts the chopsticks down and releases the seat.
*/

namespace Dining1 {
    const int N = 5;
    Semaphore chopsticks[N] = { Semaphore(1), Semaphore(1), Semaphore(1), Semaphore(1), Semaphore(1) };
    Semaphore footman(4); // allow up to N-1 philosophers to try to pick up forks

    void *philosopher(void *id) {
        long pid = (long)id;
        for (int i=0;i<5;++i) {
            printf("Philosopher %ld: Thinking\n", pid); fflush(stdout);
            msleep(120 + pid*30);

            // ask footman for permission to pick up forks
            footman.wait();
            printf("Philosopher %ld: got permission from footman\n", pid); fflush(stdout);

            // pick up forks (left then right)
            int left = pid - 1;
            int right = (pid % N);
            chopsticks[left].wait();
            printf("Philosopher %ld: picked up left fork %d\n", pid, left+1); fflush(stdout);
            chopsticks[right].wait();
            printf("Philosopher %ld: picked up right fork %d\n", pid, right+1); fflush(stdout);

            printf("Philosopher %ld: Eating (iteration %d)\n", pid, i+1); fflush(stdout);
            msleep(200);

            // put down forks
            chopsticks[right].signal();
            chopsticks[left].signal();
            printf("Philosopher %ld: Put down forks\n", pid); fflush(stdout);

            footman.signal(); // leave seat
            msleep(100);
        }
        printf("Philosopher %ld: done\n", pid); fflush(stdout);
        return NULL;
    }

    void run_demo() {
        printf("=== Dining Philosophers Solution #1 (footman, N-1 concurrency) ===\n");
        pthread_t threads[N];
        for (long i=0;i<N;++i) pthread_create(&threads[i], NULL, philosopher, (void*)(i+1));
        for (int i=0;i<N;++i) pthread_join(threads[i], NULL);
        printf("=== Dining1 demo finished ===\n\n");
    }
}

// Problem 4: Dining Philosophers — Solution #2
/*
Solution #2 uses an asymmetric strategy: philosophers with odd IDs pick up
their left chopstick first, while even-numbered philosophers pick up their
right one first. By breaking the symmetry, we avoid circular waiting and
eliminate deadlock.
*/

namespace Dining2 {
    const int N = 5;
    Semaphore chopsticks[N] = { Semaphore(1), Semaphore(1), Semaphore(1), Semaphore(1), Semaphore(1) };

    void *philosopher(void *id) {
        long pid = (long)id;
        for (int i=0;i<5;++i) {
            printf("Philosopher %ld: Thinking\n", pid); fflush(stdout);
            msleep(100 + pid*20);

            int left = pid - 1;
            int right = (pid % N);

            if (pid % 2 == 1) {
                // odd: pick left then right
                chopsticks[left].wait();
                printf("Philosopher %ld: picked up left fork %d\n", pid, left+1); fflush(stdout);
                chopsticks[right].wait();
                printf("Philosopher %ld: picked up right fork %d\n", pid, right+1); fflush(stdout);
            } else {
                // even: pick right then left
                chopsticks[right].wait();
                printf("Philosopher %ld: picked up right fork %d\n", pid, right+1); fflush(stdout);
                chopsticks[left].wait();
                printf("Philosopher %ld: picked up left fork %d\n", pid, left+1); fflush(stdout);
            }

            printf("Philosopher %ld: Eating (iteration %d)\n", pid, i+1); fflush(stdout);
            msleep(200);

            // put down
            chopsticks[left].signal();
            chopsticks[right].signal();
            printf("Philosopher %ld: Put down forks\n", pid); fflush(stdout);

            msleep(80);
        }
        printf("Philosopher %ld: done\n", pid); fflush(stdout);
        return NULL;
    }

    void run_demo() {
        printf("=== Dining Philosophers Solution #2 (asymmetric pickup) ===\n");
        pthread_t threads[N];
        for (long i=0;i<N;++i) pthread_create(&threads[i], NULL, philosopher, (void*)(i+1));
        for (int i=0;i<N;++i) pthread_join(threads[i], NULL);
        printf("=== Dining2 demo finished ===\n\n");
    }
}

/* ---------- Main dispatcher ---------- */
int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <problem#>\n", argv[0]);
        printf("  1: No-starve readers-writers (5 readers, 5 writers)\n");
        printf("  2: Writer-priority readers-writers (5 readers, 5 writers)\n");
        printf("  3: Dining philosophers solution #1 (footman)\n");
        printf("  4: Dining philosophers solution #2 (asymmetric)\n");
        return 1;
    }
    int prob = atoi(argv[1]);
    switch (prob) {
        case 1: NoStarveRW::run_demo(); break;
        case 2: WriterPriorityRW::run_demo(); break;
        case 3: Dining1::run_demo(); break;
        case 4: Dining2::run_demo(); break;
        default:
            printf("Unknown problem number %d\n", prob);
            return 1;
    }
    return 0;
}
