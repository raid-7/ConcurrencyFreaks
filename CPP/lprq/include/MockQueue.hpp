#pragma once


template<typename T>
class MockQueue {
private:
    int* ptr;

public:
    MockQueue(int) :ptr(new int) {}

    ~MockQueue() {
        delete ptr;
    }

    static std::string className() { return "MockQueue"; }

    void enqueue(T*, int) {

    }

    T* dequeue(int) {
       return reinterpret_cast<T*>(ptr);
    }
};
