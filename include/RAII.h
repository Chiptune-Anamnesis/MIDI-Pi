#pragma once

#include <SdFat.h>

// ============================================================================
// RAII (Resource Acquisition Is Initialization) Utility Classes
// ============================================================================
// These classes ensure resources are properly released even when errors occur
// or early returns happen in functions.

/**
 * RAII wrapper for SD card file handles
 * Automatically closes file when going out of scope
 */
class ScopedFile {
public:
    explicit ScopedFile(FatFile* file) : file_(file), shouldClose_(false) {}

    // Open file and take ownership
    bool open(const char* path, oflag_t flags) {
        if (file_ && file_->open(path, flags)) {
            shouldClose_ = true;
            return true;
        }
        return false;
    }

    // Get the underlying file pointer
    FatFile* get() { return file_; }

    // Check if file is open
    bool isOpen() const { return file_ && file_->isOpen(); }

    // Release ownership (caller becomes responsible for closing)
    void release() { shouldClose_ = false; }

    // Explicitly close the file
    void close() {
        if (file_ && shouldClose_ && file_->isOpen()) {
            file_->close();
        }
        shouldClose_ = false;
    }

    // Destructor automatically closes file
    ~ScopedFile() {
        close();
    }

    // Prevent copying (files shouldn't be duplicated)
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

private:
    FatFile* file_;
    bool shouldClose_;
};

/**
 * RAII wrapper for dynamically allocated memory
 * Automatically frees memory when going out of scope
 */
template<typename T>
class ScopedBuffer {
public:
    explicit ScopedBuffer(size_t size) : buffer_(nullptr), size_(size) {
        if (size > 0) {
            buffer_ = new (std::nothrow) T[size];
        }
    }

    // Get the underlying buffer pointer
    T* get() { return buffer_; }
    const T* get() const { return buffer_; }

    // Check if allocation succeeded
    bool isValid() const { return buffer_ != nullptr; }

    // Get buffer size
    size_t size() const { return size_; }

    // Array access operator
    T& operator[](size_t index) { return buffer_[index]; }
    const T& operator[](size_t index) const { return buffer_[index]; }

    // Release ownership (caller becomes responsible for deletion)
    T* release() {
        T* temp = buffer_;
        buffer_ = nullptr;
        size_ = 0;
        return temp;
    }

    // Destructor automatically frees memory
    ~ScopedBuffer() {
        if (buffer_) {
            delete[] buffer_;
            buffer_ = nullptr;
        }
    }

    // Prevent copying (ownership should be unique)
    ScopedBuffer(const ScopedBuffer&) = delete;
    ScopedBuffer& operator=(const ScopedBuffer&) = delete;

private:
    T* buffer_;
    size_t size_;
};

/**
 * RAII wrapper for mutex locks
 * Automatically releases mutex when going out of scope
 */
class ScopedMutex {
public:
    explicit ScopedMutex(mutex_t* mutex) : mutex_(mutex) {
        if (mutex_) {
            mutex_enter_blocking(mutex_);
        }
    }

    ~ScopedMutex() {
        if (mutex_) {
            mutex_exit(mutex_);
        }
    }

    // Prevent copying
    ScopedMutex(const ScopedMutex&) = delete;
    ScopedMutex& operator=(const ScopedMutex&) = delete;

private:
    mutex_t* mutex_;
};
