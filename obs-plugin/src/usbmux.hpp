#pragma once

#include <winsock2.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace usbmux {

struct Device {
    uint32_t id;
    std::string serial;
};

// The installed Apple Mobile Device Service exposes usbmuxd on localhost:27015.
std::vector<Device> list_usb_devices();
SOCKET connect_device(uint32_t device_id, uint16_t port);
bool read_exact(SOCKET socket, void *destination, size_t count);

} // namespace usbmux
