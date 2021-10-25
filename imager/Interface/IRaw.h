/*
 * phto image manager
 *
 * by Vladimir Lebedev-Schmidthof <dair.spb.ru>, (c) 2021
 * Apache License Version 2.0
 */

#pragma once

#include <cstddef>

namespace phto::imager::interface {
    class IRaw {
    public:
        virtual ~IRaw() = default;

        virtual size_t size() const = 0;
        virtual void* data() const = 0;
    };
} // namespace phto::imager::interface
