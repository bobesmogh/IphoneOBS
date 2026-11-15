#include <obs-module.h>
#include <util/platform.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "usbmux.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("iphone-usb-camera", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
    return "Uses an iPhone camera as a direct OBS source over a Lightning USB cable.";
}

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t max_frame = 8 * 1024 * 1024;
constexpr uint16_t phone_port = 45321;

struct CameraSource {
    obs_source_t *source;
    std::atomic<bool> stop{false};
    std::thread worker;
};

bool decode_jpeg(IWICImagingFactory *factory, std::vector<uint8_t> &jpeg,
                 std::vector<uint8_t> &pixels, uint32_t &width, uint32_t &height)
{
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> image;
    ComPtr<IWICFormatConverter> converter;
    UINT w = 0, h = 0;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(jpeg.data(), static_cast<DWORD>(jpeg.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &image)) ||
        FAILED(image->GetSize(&w, &h)) || w == 0 || h == 0 || w > 4096 || h > 4096 ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(image.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
        return false;

    pixels.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
        return false;
    width = w;
    height = h;
    return true;
}

void receive_camera(CameraSource *camera, SOCKET socket, IWICImagingFactory *factory)
{
    std::vector<uint8_t> jpeg;
    std::vector<uint8_t> pixels;
    while (!camera->stop) {
        std::array<uint8_t, 4> prefix{};
        if (!usbmux::read_exact(socket, prefix.data(), prefix.size())) break;
        const uint32_t size = (uint32_t(prefix[0]) << 24) | (uint32_t(prefix[1]) << 16) |
                              (uint32_t(prefix[2]) << 8) | uint32_t(prefix[3]);
        if (size < 4 || size > max_frame) break;
        jpeg.resize(size);
        if (!usbmux::read_exact(socket, jpeg.data(), jpeg.size())) break;
        if (jpeg[0] != 0xff || jpeg[1] != 0xd8) break;

        uint32_t width = 0, height = 0;
        if (!decode_jpeg(factory, jpeg, pixels, width, height)) continue;
        obs_source_frame frame{};
        frame.format = VIDEO_FORMAT_BGRA;
        frame.width = width;
        frame.height = height;
        frame.data[0] = pixels.data();
        frame.linesize[0] = width * 4;
        frame.timestamp = os_gettime_ns();
        obs_source_output_video(camera->source, &frame);
    }
    obs_source_output_video(camera->source, nullptr);
}

void worker(CameraSource *camera)
{
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        WSACleanup();
        return;
    }
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        CoUninitialize();
        WSACleanup();
        return;
    }

    while (!camera->stop) {
        try {
            const auto devices = usbmux::list_usb_devices();
            if (!devices.empty()) {
                SOCKET socket = usbmux::connect_device(devices.front().id, phone_port);
                blog(LOG_INFO, "[iPhone USB Camera] Connected to %s", devices.front().serial.c_str());
                receive_camera(camera, socket, factory.Get());
                closesocket(socket);
            }
        } catch (const std::exception &error) {
            // The camera may not yet be open, paired, or connected. Retry quietly.
            blog(LOG_DEBUG, "[iPhone USB Camera] %s", error.what());
        }
        for (int i = 0; i < 15 && !camera->stop; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    factory.Reset();
    CoUninitialize();
    WSACleanup();
}

const char *source_name(void *) { return "iPhone USB Camera"; }

void *source_create(obs_data_t *, obs_source_t *source)
{
    auto *camera = new CameraSource{source};
    camera->worker = std::thread(worker, camera);
    return camera;
}

void source_destroy(void *data)
{
    auto *camera = static_cast<CameraSource *>(data);
    camera->stop = true;
    camera->worker.join();
    delete camera;
}

} // namespace

bool obs_module_load(void)
{
    obs_source_info info{};
    info.id = "iphone_usb_camera";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
    info.get_name = source_name;
    info.create = source_create;
    info.destroy = source_destroy;
    obs_register_source(&info);
    return true;
}
