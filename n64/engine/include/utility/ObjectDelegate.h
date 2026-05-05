#pragma once

#include "scene/object.h"

#include <utility>     // std::forward
#include <type_traits> // std::is_void_v
#include <cstddef>     // std::byte, std::size_t
#include <cstring>     // std::memcpy, std::memset

namespace P64::User::Utility
{
    template<typename Signature>
    struct ObjectDelegate;

    template<typename Ret, typename... ExtraArgs>
    struct ObjectDelegate<Ret(ExtraArgs...)>
    {
    private:
        class IDelegate
        {
        public:
            virtual ~IDelegate() = default;
            virtual Ret operator()(ExtraArgs... args) const = 0;
        };

        template<typename TData>
        class Delegate final : public IDelegate
        {
            ObjectRef target{};
            TData* userData = nullptr;
            Ret (*function)(Object&, TData*, ExtraArgs...) = nullptr;

        public:
            Delegate(const Object& obj, TData* data, Ret (*f)(Object&, TData*, ExtraArgs...))
                : target{static_cast<uint32_t>(obj.id)}
                , userData(data)
                , function(f) {}

            Ret operator()(ExtraArgs... args) const override
            {
                if (Object* objPtr = target.get(); objPtr != nullptr)
                {
                    return function(*objPtr, userData, std::forward<ExtraArgs>(args)...);
                }

                if constexpr (!std::is_void_v<Ret>)
                {
                    return Ret{};
                }
            }
        };

        static constexpr std::size_t BufferSize = sizeof(Delegate<void>);
        static constexpr std::size_t BufferAlign = alignof(Delegate<void>);

        alignas(BufferAlign) std::byte storage[BufferSize]{};
        IDelegate* callback = nullptr;

    public:
        ObjectDelegate() noexcept
        {
            std::memset(storage, 0, BufferSize);
        }

        ObjectDelegate(std::nullptr_t) noexcept
            : ObjectDelegate()
        {}


        template<typename DataT>
        ObjectDelegate(const Object& obj, DataT* data, Ret (*f)(Object&, DataT*, ExtraArgs...)) noexcept
            : ObjectDelegate()
        {
            bind<DataT>(obj, data, f);
        }

        ObjectDelegate(const ObjectDelegate& other)
        {
            std::memset(storage, 0, BufferSize);
            callback = nullptr;
            if (other.callback)
            {
                std::memcpy(storage, other.storage, BufferSize);
                callback = reinterpret_cast<IDelegate*>(storage);
            }
        }

        ObjectDelegate& operator=(const ObjectDelegate& other)
        {
            if (this != &other)
            {
                unbind();
                std::memset(storage, 0, BufferSize);
                callback = nullptr;
                if (other.callback)
                {
                    std::memcpy(storage, other.storage, BufferSize);
                    callback = reinterpret_cast<IDelegate*>(storage);
                }
            }
            return *this;
        }

        ObjectDelegate(ObjectDelegate&& other) noexcept
        {
            std::memset(storage, 0, BufferSize);
            callback = nullptr;
            if (other.callback)
            {
                std::memcpy(storage, other.storage, BufferSize);
                callback = reinterpret_cast<IDelegate*>(storage);
                other.callback = nullptr;
            }
        }

        ObjectDelegate& operator=(ObjectDelegate&& other) noexcept
        {
            if (this != &other)
            {
                unbind();
                std::memset(storage, 0, BufferSize);
                callback = nullptr;
                if (other.callback)
                {
                    std::memcpy(storage, other.storage, BufferSize);
                    callback = reinterpret_cast<IDelegate*>(storage);
                    other.callback = nullptr;
                }
            }
            return *this;
        }

        template<typename DataT>
        void bind(const Object& obj, DataT* data, Ret (*f)(Object&, DataT*, ExtraArgs...)) noexcept
        {
            unbind();
            if (data && f)
            {
                callback = new (static_cast<void*>(storage)) Delegate<DataT>(obj, data, f);
            }
        }

        void unbind() noexcept
        {
            if (callback)
            {
                callback->~IDelegate();
                callback = nullptr;
                std::memset(storage, 0, BufferSize); // extra safety
            }
        }

        ~ObjectDelegate()
        {
            unbind();
        }

        Ret operator()(ExtraArgs... args) const
        {
            if (callback != nullptr)
            {
                return (*callback)(std::forward<ExtraArgs>(args)...);
            }

            if constexpr (!std::is_void_v<Ret>)
            {
                return Ret{};
            }
        }

        explicit operator bool() const noexcept
        {
            return callback != nullptr;
        }

    private:
        // Block any accidental lambda / functor / wrong assignment
        template<typename F>
        ObjectDelegate(F&&) = delete;

        template<typename F>
        ObjectDelegate& operator=(F&&) = delete;
    };
}
