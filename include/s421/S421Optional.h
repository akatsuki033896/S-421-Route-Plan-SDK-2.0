#pragma once

/**
 * @file S421Optional.h
 * @brief `std::optional` 的 C++14 兼容层。
 *
 * 背景：s421 SDK 早期用 C++17 的 `std::optional`。为让仅有 C++14 libstdc++ 的
 *       交叉编译器（如 RK3128 用的 Linaro GCC 5.5）也能编译，把 `std::optional`
 *       统一改写为 `s421::optional`。本头根据编译器的 C++ 标准自动二选一：
 *
 *   - C++17 及以上（MSVC / 较新 MinGW / 新 GCC）→ 直接 `using std::optional`，
 *     零开销、行为与标准库完全一致，老调用方代码无需任何改动。
 *
 *   - C++14（旧 GCC 5.x 等无 `<optional>`）→ 启用下方自实现的
 *     `s421::optional<T>`，API 子集与 `std::optional` 行为对齐：
 *       默认构造为空 / 显式构造 / 拷贝 / 移动 / 赋值（含 `nullopt` 清空）
 *       `operator bool` / `has_value` / `value` / `value_or` / `operator*` / `operator->`
 *
 * 判断 C++17 的宏同时兼顾 MSVC（看 `_MSVC_LANG`，因 `__cplusplus` 在 MSVC 下常被
 * 固定为 `199711L`）。
 *
 * @note 仅覆盖 s421 实际用到的接口（`has_value` / `value` / `operator*` / `nullopt` 返回），
 *       不追求与 `std::optional` 完全等价，避免过度设计。
 */

#include <type_traits>
#include <utility>      ///< move/forward
#include <cstddef>      ///< nullptr_t

// ---- 是否有原生 std::optional 可用 ----
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
    #define S421_HAVE_STD_OPTIONAL 1
#endif

#ifdef S421_HAVE_STD_OPTIONAL
    // C++17：透传标准库，行为零差异
    #include <optional>
#endif

namespace s421 {

#ifdef S421_HAVE_STD_OPTIONAL

    using std::optional;    ///< C++17：直接使用标准库实现
    using std::nullopt_t;   ///< C++17：直接使用标准库标签类型
    using std::nullopt;     ///< C++17：直接使用标准库空值

#else

// ============================================================
//  C++14 自实现 optional（行为对齐 std::optional 的常用子集）
// ============================================================

/**
 * @struct nullopt_t
 * @brief `nullopt` 标签类型。
 *
 * 用于 `return s421::nullopt;` 或 `opt = s421::nullopt;`。
 */
struct nullopt_t {
    /// 非 explicit 的带参构造 + 内部占位，保证 nullopt 不会与任意重载歧义
    struct placeholder {};
    explicit nullopt_t(placeholder) {}
};
static const nullopt_t nullopt{nullopt_t::placeholder{}};   ///< 空值常量

/**
 * @struct in_place_t
 * @brief in-place 构造标签。
 * @note 本库暂未用到，保留以备扩展。
 */
struct in_place_t {};
static const in_place_t in_place{};   ///< in-place 标签常量

/**
 * @class optional
 * @brief 轻量级可选值容器（`std::optional` 的 C++14 常用子集）。
 *
 * @tparam T 承载的值类型；不支持引用类型。
 */
template <typename T>
class optional {
    static_assert(!std::is_reference<T>::value,
                  "s421::optional does not support reference types");

public:
    /**
     * @brief 默认构造：空。
     */
    optional() noexcept : has_(false) {}

    /**
     * @brief 以 `nullopt` 构造：空。
     *
     * 参数为标签类型（匿名），仅用于重载区分。
     */
    optional(nullopt_t) noexcept : has_(false) {}

    /**
     * @brief 拷贝构造。
     * @param other 源对象。
     */
    optional(const optional& other) : has_(other.has_) {
        if (has_) ::new (storage_ptr()) T(*other.storage_ptr());
    }

    /**
     * @brief 移动构造。
     * @param other 源对象。
     */
    optional(optional&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
        : has_(other.has_) {
        if (has_) ::new (storage_ptr()) T(std::move(*other.storage_ptr()));
    }

    /**
     * @brief 直接值构造（T 可隐式转换时也走这里，匹配 std::optional 的便捷语义）。
     * @param value 初始值。
     */
    optional(const T& value) : has_(true) {
        ::new (storage_ptr()) T(value);
    }

    /**
     * @brief 直接值构造（移动版）。
     * @param value 初始值。
     */
    optional(T&& value) : has_(true) {
        ::new (storage_ptr()) T(std::move(value));
    }

    /// @brief 析构：有值则析构之。
    ~optional() { destroy(); }

    // ---- 赋值 ----

    /**
     * @brief 以 `nullopt` 赋值，清空。
     * @return 自身引用。
     */
    optional& operator=(nullopt_t) noexcept {
        destroy();
        has_ = false;
        return *this;
    }

    /**
     * @brief 拷贝赋值。
     * @param other 源对象。
     * @return 自身引用。
     */
    optional& operator=(const optional& other) {
        if (this != &other) {
            if (other.has_) {
                if (has_) *storage_ptr() = *other.storage_ptr();
                else { ::new (storage_ptr()) T(*other.storage_ptr()); has_ = true; }
            } else {
                destroy();
                has_ = false;
            }
        }
        return *this;
    }

    /**
     * @brief 移动赋值。
     * @param other 源对象。
     * @return 自身引用。
     */
    optional& operator=(optional&& other)
        noexcept(std::is_nothrow_move_assignable<T>::value &&
                 std::is_nothrow_move_constructible<T>::value) {
        if (this != &other) {
            if (other.has_) {
                if (has_) *storage_ptr() = std::move(*other.storage_ptr());
                else { ::new (storage_ptr()) T(std::move(*other.storage_ptr())); has_ = true; }
            } else {
                destroy();
                has_ = false;
            }
        }
        return *this;
    }

    /**
     * @brief 以值赋值（拷贝）。
     * @param value 新值。
     * @return 自身引用。
     */
    optional& operator=(const T& value) {
        if (has_) *storage_ptr() = value;
        else { ::new (storage_ptr()) T(value); has_ = true; }
        return *this;
    }

    /**
     * @brief 以值赋值（移动）。
     * @param value 新值。
     * @return 自身引用。
     */
    optional& operator=(T&& value) {
        if (has_) *storage_ptr() = std::move(value);
        else { ::new (storage_ptr()) T(std::move(value)); has_ = true; }
        return *this;
    }

    // ---- 观察者 ----

    /**
     * @brief 是否持有值。
     * @return `true` = 有值。
     */
    bool has_value() const noexcept { return has_; }

    /**
     * @brief 是否持有值（布尔转换）。
     * @return `true` = 有值。
     */
    explicit operator bool() const noexcept { return has_; }

    /**
     * @brief 取值的引用（非 const）。
     *
     * @warning 无值时行为与 `std::optional` 略有差异（这里直接断言/解引用，
     *          s421 内部仅在 `has_value()` 后调用，不会越界）。
     * @return 值引用。
     */
    T&       value()       { return *storage_ptr(); }

    /// @copydoc value()
    const T& value() const { return *storage_ptr(); }

    /**
     * @brief 不检查地访问值（对应标准库的 `operator*`）。
     * @return 值引用。
     */
    T&       operator*()       { return *storage_ptr(); }

    /// @copydoc operator*()
    const T& operator*() const { return *storage_ptr(); }

    /**
     * @brief 成员访问。
     * @return 值指针。
     */
    T*       operator->()       { return storage_ptr(); }

    /// @copydoc operator->()
    const T* operator->() const { return storage_ptr(); }

    /**
     * @brief 有值则返回该值，否则返回给定默认值。
     * @param default_value 无值时的回退值。
     * @return 值或默认值。
     */
    T value_or(const T& default_value) const {
        return has_ ? *storage_ptr() : default_value;
    }

private:
    /// @brief 获取存储区指针（非 const）。
    T*       storage_ptr()       { return reinterpret_cast<T*>(&storage_); }

    /// @copydoc storage_ptr()
    const T* storage_ptr() const { return reinterpret_cast<const T*>(&storage_); }

    /// @brief 析构内部值（若有）并清空标志。
    void destroy() {
        if (has_) { storage_ptr()->~T(); has_ = false; }
    }

    bool has_;      ///< 是否持有值
    typename std::aligned_storage<sizeof(T), std::alignment_of<T>::value>::type storage_;   ///< 未初始化存储区
};

#endif // S421_HAVE_STD_OPTIONAL

} // namespace s421
