#pragma once
#include <string>
#include "abstract_device.h"
#include "device/device.h"

namespace peripherals {
struct Mouse : public AbstractDevice {
    bool left = false, right = false;
    int8_t x = 0, y = 0;

    uint8_t handle(uint8_t byte) override;
    Mouse();
};
};  // namespace peripherals