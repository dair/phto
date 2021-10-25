/*
 * phto image manager
 *
 * by Vladimir Lebedev-Schmidthof <dair.spb.ru>, (c) 2021
 * Apache License Version 2.0
 */

#pragma once

#include "IImage.h"
#include "IRaw.h"
#include <memory>

namespace phto::imager::interface {
    class IImageFactory {
    public:
        virtual ~IImageFactory() = default;

        std::shared_ptr<IImage> createImage(const IRaw&);
    };
} // namespace phto::imager::interface
