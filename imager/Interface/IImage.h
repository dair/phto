/*
 * phto image manager
 *
 * by Vladimir Lebedev-Schmidthof <dair.spb.ru>, (c) 2021
 * Apache License Version 2.0
 */

#pragma once

#include <cstdint>

namespace phto::imager::interface {
    class IImage {
    public:
        virtual ~IImage() = default;

        virtual uint32_t width() const = 0;
        virtual uint32_t height() const = 0;
    };
} // namespace phto::imager::interface
