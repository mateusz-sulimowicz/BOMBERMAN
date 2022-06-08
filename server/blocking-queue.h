#ifndef ROBOTS_SERVER_BLOCKING_QUEUE_H
#define ROBOTS_SERVER_BLOCKING_QUEUE_H

#include <condition_variable>
#include <queue>

/**
 * To jest bezpieczna dla wątków
 * kolejka blokująca.
 */
template<typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::queue<T> inital_state) : queue(std::move(inital_state)) {}

    /**
     * Zdejmuje element z kolejki.
     * Jeśli kolejka jest pusta, to wywołujący wątek
     * zostaje zablokowany.
     */
    T pop() {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return !queue.empty() || !is_open; });

        if (!is_open) {
            throw std::runtime_error{"Client connection closed."};
        }

        T first = queue.front();
        queue.pop();
        return first;
    }

    /**
     * Wstawia nowy element na koniec kolejki.
     */
    void push(T val) {
        std::unique_lock lock(mutex);
        queue.push(val);
        cv.notify_all();
    }

    void close() {
        std::unique_lock lock(mutex);
        is_open = false;
        cv.notify_all();
    }

    bool isOpen() {
        std::unique_lock lock(mutex);
        return is_open;
    }

private:
    std::condition_variable cv;
    std::mutex mutex;
    std::queue<T> queue;
    bool is_open = true;
};


#endif //ROBOTS_SERVER_BLOCKING_QUEUE_H
