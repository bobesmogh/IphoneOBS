#include "usbmux.hpp"

#include <algorithm>
#include <array>
#include <regex>
#include <stdexcept>
#include <string>

namespace usbmux {
namespace {

constexpr uint32_t max_packet = 4 * 1024 * 1024;

struct PacketHeader {
    uint32_t length;
    uint32_t version;
    uint32_t type;
    uint32_t tag;
};
static_assert(sizeof(PacketHeader) == 16);

void send_all(SOCKET socket, const char *data, size_t size)
{
    while (size != 0) {
        int sent = send(socket, data, static_cast<int>(std::min<size_t>(size, 64 * 1024)), 0);
        if (sent <= 0) throw std::runtime_error("Failed to send usbmuxd request");
        data += sent;
        size -= sent;
    }
}

SOCKET open_service()
{
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) throw std::runtime_error("Cannot open socket");
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(27015);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        closesocket(socket);
        throw std::runtime_error("Apple Mobile Device Service is unavailable");
    }
    return socket;
}

std::string request(SOCKET socket, const std::string &body)
{
    const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                            "<plist version=\"1.0\"><dict>"
                            "<key>BundleID</key><string>local.iphoneusbobs</string>"
                            "<key>ClientVersionString</key><string>0.1</string>"
                            "<key>ProgName</key><string>iphone-usb-camera</string>" + body + "</dict></plist>";
    const PacketHeader header{static_cast<uint32_t>(sizeof(PacketHeader) + xml.size()), 1, 8, 1};
    send_all(socket, reinterpret_cast<const char *>(&header), sizeof(header));
    send_all(socket, xml.data(), xml.size());
    PacketHeader reply{};
    if (!read_exact(socket, &reply, sizeof(reply)) || reply.length < sizeof(reply) || reply.length > max_packet || reply.type != 8 || reply.tag != 1)
        throw std::runtime_error("Invalid usbmuxd response");
    std::string payload(reply.length - sizeof(reply), '\0');
    if (!read_exact(socket, payload.data(), payload.size())) throw std::runtime_error("Incomplete usbmuxd response");
    return payload;
}

std::string field(const std::string &xml, const std::string &key, const char *type)
{
    std::regex pattern("<key>" + key + "</key>\\s*<" + type + ">([^<]*)</" + type + ">");
    std::smatch match;
    return std::regex_search(xml, match, pattern) ? match[1].str() : "";
}

} // namespace

bool read_exact(SOCKET socket, void *destination, size_t count)
{
    char *out = static_cast<char *>(destination);
    while (count != 0) {
        int received = recv(socket, out, static_cast<int>(std::min<size_t>(count, 64 * 1024)), 0);
        if (received <= 0) return false;
        out += received;
        count -= received;
    }
    return true;
}

std::vector<Device> list_usb_devices()
{
    SOCKET socket = open_service();
    std::string xml;
    try {
        xml = request(socket, "<key>MessageType</key><string>ListDevices</string>");
    } catch (...) {
        closesocket(socket);
        throw;
    }
    closesocket(socket);

    std::vector<Device> devices;
    std::regex id_pattern("<key>DeviceID</key>\\s*<integer>([0-9]+)</integer>");
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), id_pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const size_t start = static_cast<size_t>(it->position());
        auto next = it;
        ++next;
        const size_t finish = next == end ? xml.size() : static_cast<size_t>(next->position());
        const std::string section = xml.substr(start, finish - start);
        if (field(section, "ConnectionType", "string") == "USB") {
            devices.push_back({static_cast<uint32_t>(std::stoul((*it)[1].str())), field(section, "SerialNumber", "string")});
        }
    }
    return devices;
}

SOCKET connect_device(uint32_t device_id, uint16_t port)
{
    SOCKET socket = open_service();
    try {
        // usbmuxd expects the port in network byte order, represented as an integer.
        const std::string body = "<key>MessageType</key><string>Connect</string>"
                                 "<key>DeviceID</key><integer>" + std::to_string(device_id) + "</integer>"
                                 "<key>PortNumber</key><integer>" + std::to_string(htons(port)) + "</integer>";
        const std::string response = request(socket, body);
        if (field(response, "Number", "integer") != "0") throw std::runtime_error("iPhone app is not accepting USB connections");
        return socket; // This socket now carries raw TCP data from the iPhone.
    } catch (...) {
        closesocket(socket);
        throw;
    }
}

} // namespace usbmux
