#pragma once

#include <windows.h>

namespace win32 {

template <class T, BOOL (WINAPI* CloseFn)(T)>
class unique_handle {
public:
    unique_handle() = default;
    explicit unique_handle(T value) : value_(value) {}
    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;
    unique_handle(unique_handle&& other) noexcept : value_(other.release()) {}
    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) { reset(); value_ = other.release(); }
        return *this;
    }
    ~unique_handle() { reset(); }
    explicit operator bool() const { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
    T get() const { return value_; }
    T release() { T result = value_; value_ = nullptr; return result; }
    void reset(T value = nullptr) { if (*this) CloseFn(value_); value_ = value; }
private:
    T value_{};
};

inline BOOL closeWindowStation(HWINSTA value) { return CloseWindowStation(value); }
inline BOOL closeDesktop(HDESK value) { return CloseDesktop(value); }
using unique_window_station = unique_handle<HWINSTA, closeWindowStation>;
using unique_desktop = unique_handle<HDESK, closeDesktop>;

class unique_icon {
public:
    explicit unique_icon(HICON value = nullptr) : value_(value) {}
    unique_icon(const unique_icon&) = delete;
    unique_icon& operator=(const unique_icon&) = delete;
    ~unique_icon() { reset(); }
    HICON get() const { return value_; }
    void reset(HICON value = nullptr) { if (value_) DestroyIcon(value_); value_ = value; }
private:
    HICON value_{};
};

}  // namespace win32
