#pragma once

#include <variant>
#include <type_traits>
#include <stdexcept>

namespace presence_for_plex {
namespace utils {

template<typename E>
struct unexpected {
    E value;
    explicit unexpected(E e) : value(std::move(e)) {}
};

template<typename T, typename E>
class expected {
private:
    std::variant<T, E> storage;

public:
    // Constructors
    expected() requires std::is_default_constructible_v<T> : storage(T{}) {}
    expected(const T& value) : storage(value) {}
    expected(T&& value) : storage(std::move(value)) {}
    expected(const unexpected<E>& err) : storage(err.value) {}
    expected(unexpected<E>&& err) : storage(std::move(err.value)) {}

    // Assignment
    expected& operator=(const T& value) {
        storage = value;
        return *this;
    }

    expected& operator=(T&& value) {
        storage = std::move(value);
        return *this;
    }

    expected& operator=(const unexpected<E>& err) {
        storage = err.value;
        return *this;
    }

    // Check state
    bool has_value() const noexcept {
        return std::holds_alternative<T>(storage);
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    // Value access
    T& value() & {
        if (!has_value()) throw std::runtime_error("expected has no value");
        return std::get<T>(storage);
    }

    const T& value() const & {
        if (!has_value()) throw std::runtime_error("expected has no value");
        return std::get<T>(storage);
    }

    T&& value() && {
        if (!has_value()) throw std::runtime_error("expected has no value");
        return std::get<T>(std::move(storage));
    }

    // Error access
    E& error() & {
        if (has_value()) throw std::runtime_error("expected has value, not error");
        return std::get<E>(storage);
    }

    const E& error() const & {
        if (has_value()) throw std::runtime_error("expected has value, not error");
        return std::get<E>(storage);
    }

    E&& error() && {
        if (has_value()) throw std::runtime_error("expected has value, not error");
        return std::get<E>(std::move(storage));
    }

    // Dereference operators
    T& operator*() & { return value(); }
    const T& operator*() const & { return value(); }
    T&& operator*() && { return std::move(value()); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
};

// Specialization for void
template<typename E>
class expected<void, E> {
private:
    std::variant<std::monostate, E> storage;

public:
    // Constructors
    expected() : storage(std::monostate{}) {}
    expected(const unexpected<E>& err) : storage(err.value) {}
    expected(unexpected<E>&& err) : storage(std::move(err.value)) {}

    // Assignment
    expected& operator=(const unexpected<E>& err) {
        storage = err.value;
        return *this;
    }

    // Check state
    bool has_value() const noexcept {
        return std::holds_alternative<std::monostate>(storage);
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    // Value access
    void value() const {
        if (!has_value()) throw std::runtime_error("expected has no value");
    }

    // Error access
    E& error() & {
        if (has_value()) throw std::runtime_error("expected has value, not error");
        return std::get<E>(storage);
    }

    const E& error() const & {
        if (has_value()) throw std::runtime_error("expected has value, not error");
        return std::get<E>(storage);
    }

    E&& error() && {
        if (has_value()) throw std::runtime_error("expected has value, not error");
        return std::get<E>(std::move(storage));
    }
};

} // namespace utils
} // namespace presence_for_plex

// Provide compatibility with std namespace
namespace std {
    template<typename T, typename E>
    using expected = presence_for_plex::utils::expected<T, E>;

    template<typename E>
    using unexpected = presence_for_plex::utils::unexpected<E>;
}