#ifndef LOCK_FREE_RING_BUFFER_H
#define LOCK_FREE_RING_BUFFER_H

#include <atomic>
#include <vector>
#include <cstddef>
#include <cstdint>

// SPSC (Single-Producer / Single-Consumer) Lock-Free Ringbuffer
// Speziell optimiert für die ALSA Capture/Playback Architektur im OAPW_Player_HiFi
template <typename T>
class LockFreeRingBuffer {
private:
    std::vector<T> buffer;
    const size_t capacity;

    // Cache-Line Alignment zur Vermeidung von False Sharing zwischen den Threads.
    // Verhindert, dass CPU-Cores sich gegenseitig blockieren, wenn Capture- 
    // und Playback-Thread auf benachbarte Speicheradressen zugreifen.
    alignas(64) std::atomic<size_t> head; // Wird vom Producer (Capture-Thread) geschrieben
    alignas(64) std::atomic<size_t> tail; // Wird vom Consumer (Playback-Thread) geschrieben

public:
    explicit LockFreeRingBuffer(size_t size) : buffer(size), capacity(size), head(0), tail(0) {}

    // Push: Schreibt Daten in den Ringbuffer (Aufruf durch Capture-Thread)
    bool push(const T* data, size_t items) {
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t current_head = head.load(std::memory_order_relaxed);

        // Prüfen, ob ausreichend Platz vorhanden ist
        size_t available_space = capacity - (current_head - current_tail);
        if (available_space < items) {
            return false; // Overrun-Gefahr
        }

        // Daten kopieren (Modulo-Operation hält den Index im Array-Bereich)
        for (size_t i = 0; i < items; ++i) {
            buffer[(current_head + i) % capacity] = data[i];
        }

        // Head aktualisieren und sichtbar machen
        head.store(current_head + items, std::memory_order_release);
        return true;
    }

    // Pop: Liest Daten aus dem Ringbuffer (Aufruf durch Playback/DSP-Thread)
    bool pop(T* dest, size_t items) {
        size_t current_head = head.load(std::memory_order_acquire);
        size_t current_tail = tail.load(std::memory_order_relaxed);

        // Prüfen, ob ausreichend Daten vorhanden sind
        size_t available_items = current_head - current_tail;
        if (available_items < items) {
            return false; // Underrun-Gefahr
        }

        // Daten auslesen
        for (size_t i = 0; i < items; ++i) {
            dest[i] = buffer[(current_tail + i) % capacity];
        }

        // Tail aktualisieren und sichtbar machen
        tail.store(current_tail + items, std::memory_order_release);
        return true;
    }

    // Reset: Setzt den Buffer zurück (z.B. bei Pausen für Re-Centering)
    void reset() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    // Füllstand abfragen (z.B. für Diagnostik oder Re-Centering Logik)
    size_t get_fill_level() const {
        size_t current_head = head.load(std::memory_order_acquire);
        size_t current_tail = tail.load(std::memory_order_acquire);
        return current_head - current_tail;
    }

    size_t get_capacity() const {
        return capacity;
    }
};

#endif // LOCK_FREE_RING_BUFFER_H
