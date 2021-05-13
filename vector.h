#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <exception>
#include <memory>

namespace notstd {
    template <typename T>
    class RawMemory {
    public:
        RawMemory() = default;

        explicit RawMemory(size_t capacity)
            : buffer_(Allocate(capacity))
            , capacity_(capacity) {
        }

        RawMemory(const RawMemory&) = delete;
        RawMemory& operator=(const RawMemory& rhs) = delete;

        RawMemory(RawMemory&& other) noexcept
            : buffer_(std::move(other.buffer_))
            , capacity_(std::move(other.capacity_)) {
            other.buffer_ = nullptr;
            other.capacity_ = 0;
        }

        ~RawMemory() {
            Deallocate(buffer_);
        }    

        RawMemory& operator=(RawMemory&& rhs) noexcept {
            Deallocate(buffer_);
            buffer_ = std::move(rhs.buffer_);
            capacity_ = rhs.capacity_;
            rhs.buffer_ = nullptr;
            rhs.capacity_ = 0;
            return *this;
        }

        T* operator+(size_t offset) noexcept {
            return buffer_ + offset;
        }

        const T* operator+(size_t offset) const noexcept {
            return const_cast<RawMemory&>(*this) + offset;
        }

        const T& operator[](size_t index) const noexcept {
            return const_cast<RawMemory&>(*this)[index];
        }

        T& operator[](size_t index) noexcept {
            assert(index < capacity_);
            return buffer_[index];
        }

        void Swap(RawMemory& other) noexcept {
            std::swap(buffer_, other.buffer_);
            std::swap(capacity_, other.capacity_);
        }

        const T* GetAddress() const noexcept {
            return buffer_;
        }

        T* GetAddress() noexcept {
            return buffer_;
        }

        size_t Capacity() const {
            return capacity_;
        }

    private:
        static T* Allocate(size_t n) {
            return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
        }

        static void Deallocate(T* buf) noexcept {
            operator delete(buf);
        }

    private:
        T* buffer_ = nullptr;
        size_t capacity_ = 0;
    };

    template <typename T>
    class Vector {
    public:
        Vector() = default;

        explicit Vector(size_t size)
            : data_(size)
            , size_(size) {
            std::uninitialized_value_construct_n(begin(), size);
        }

        Vector(const Vector& other)
            : data_(other.size_)
            , size_(other.size_) {
            std::uninitialized_copy_n(other.begin(), size_, begin());
        }

        Vector(Vector&& other)  noexcept
            : data_(std::move(other.data_))
            , size_(std::move(other.size_)) {
            other.size_ = 0;
        }

        using iterator = T*;
        using const_iterator = const T*;

        iterator begin() noexcept {
            return data_.GetAddress();
        }

        iterator end() noexcept {
            return data_.GetAddress() + size_;
        }

        const_iterator begin() const noexcept {
            return data_.GetAddress();
        }

        const_iterator end() const noexcept {
            return data_.GetAddress() + size_;
        }

        const_iterator cbegin() const noexcept {
            return data_.GetAddress();
        }

        const_iterator cend() const noexcept {
            return data_.GetAddress() + size_;
        }

        template <typename... Args>
        T& EmplaceBack(Args&&... args) {
            if (size_ < data_.Capacity()) {
                new (data_ + size_) T(std::forward<Args>(args)...);
                ++size_;
            } else {
                size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
                RawMemory<T> new_data(new_capacity);
                new (new_data + size_) T(std::forward<Args>(args)...);
                UninitializedMoveOrCopy(begin(), size_, new_data.GetAddress());
                std::destroy_n(begin(), size_);
                data_.Swap(new_data);
                ++size_;
            }
            return data_[size_ - 1];
        }
        
        template <typename V>
        void PushBack(V&& value) {
            EmplaceBack(std::forward<V>(value));
        }

        template <typename... Args>
        iterator Emplace(const_iterator pos, Args&&... args) {
            assert(pos >= begin() && pos <= end());
            size_t before = pos - begin();
            size_t after = end() - pos - 1;
            if (pos == end()) {
                EmplaceBack(std::forward<Args>(args)...);
            } else if (size_ < data_.Capacity()) {
                T tmp = T(std::forward<Args>(args)...);
                new (data_ + size_) T(std::forward<T>(*(end() - 1)));
                std::move_backward(const_cast<iterator>(pos), end() - 1, end());
                data_[before] = std::move(tmp);
                ++size_;
            } else {
                size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
                RawMemory<T> new_data(new_capacity);
                new (new_data + before) T(std::forward<Args>(args)...);
                
                try {
                    UninitializedMoveOrCopy(begin(), before, new_data.GetAddress());
                }
                catch (...) {
                    (new_data + size_)->~T();
                    throw;
                }

                try {
                    UninitializedMoveOrCopy(const_cast<iterator>(pos), after + 1, new_data.GetAddress() + (before + 1));
                }
                catch (...) {
                    std::destroy_n(new_data.GetAddress(), before);
                    throw;
                }

                std::destroy_n(begin(), size_);
                data_.Swap(new_data);
                ++size_;
            }
            return data_.GetAddress() + before;
        }
            
        template <typename V>
        iterator Insert(const_iterator pos, V&& value) {
            return Emplace(pos, std::forward<V>(value));
        }

        void PopBack() noexcept {
            std::destroy_n(begin() + (size_ - 1), 1);
            --size_;
        }

        iterator Erase(const_iterator pos) {
            assert(pos >= begin() && pos < end());
            std::move(const_cast<iterator>(pos) + 1, end(), const_cast<iterator>(pos));
            (end() - 1)->~T();
            --size_;
            return const_cast<iterator>(pos);
        }    

        Vector& operator=(const Vector& rhs) {
            if (this != &rhs) {
                if (rhs.size_ > data_.Capacity()) {
                    Vector rhs_copy(rhs);
                    Swap(rhs_copy);
                } else {
                    if (rhs.size_ < size_) {
                        std::copy(rhs.begin(), rhs.begin() + rhs.size_, begin());
                        std::destroy_n(begin() + rhs.size_, size_ - rhs.size_);
                        size_ = rhs.size_;
                    } else {
                        std::copy(rhs.begin(), rhs.begin() + size_, begin());
                        std::uninitialized_copy_n(rhs.begin() + size_, rhs.size_ - size_, end());
                        size_ = rhs.size_;
                    }
                }
            }
            return *this;
        }

        Vector& operator=(Vector&& rhs) noexcept {
            if (this != &rhs) {
                Swap(rhs);
                rhs.size_ = 0;
            }
            return *this;
        }

        void Swap(Vector& other) noexcept {
            data_.Swap(other.data_);
            std::swap(size_, other.size_);
        }

        void Reserve(size_t new_capacity) {
            if (new_capacity <= data_.Capacity()) {
                return;
            }
            RawMemory<T> new_data(new_capacity);
            UninitializedMoveOrCopy(begin(), size_, new_data.GetAddress());
            std::destroy_n(begin(), size_);
            data_.Swap(new_data);
        }

        void Resize(size_t new_size) {
            if (new_size == size_) {
                return;
            }
            if (new_size < size_) {
                std::destroy_n(begin() + new_size, size_ - new_size);
                size_ = new_size;
            } else {
                Reserve(new_size);
                std::uninitialized_value_construct_n(end(), new_size - size_);
                size_ = new_size;
            }
        }

        size_t Size() const noexcept {
            return size_;
        }

        size_t Capacity() const noexcept {
            return data_.Capacity();
        }

        const T& operator[](size_t index) const noexcept {
            return const_cast<Vector&>(*this)[index];
        }

        T& operator[](size_t index) noexcept {
            assert(index < size_);
            return data_[index];
        }

        ~Vector() {
            std::destroy_n(begin(), size_);
        }

    private:
        void UninitializedMoveOrCopy(iterator from, size_t size, iterator to) {
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(from, size, to);
            } else {
                std::uninitialized_copy_n(from, size, to);
            }
        }

    private:
        RawMemory<T> data_;
        size_t size_ = 0;
    };
}//namespace notstd
