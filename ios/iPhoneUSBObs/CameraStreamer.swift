import AVFoundation
import CoreImage
import Network
import SwiftUI

final class CameraStreamer: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    @Published private(set) var status = "Нажмите «Начать»"
    @Published private(set) var running = false
    @Published private(set) var usingFrontCamera = false

    let session = AVCaptureSession()
    private let cameraQueue = DispatchQueue(label: "local.iphoneusbobs.camera")
    private let networkQueue = DispatchQueue(label: "local.iphoneusbobs.network")
    private let context = CIContext()
    private var listener: NWListener?
    private var client: NWConnection?
    private var sending = false

    func start() {
        AVCaptureDevice.requestAccess(for: .video) { [weak self] allowed in
            guard let self = self else { return }
            guard allowed else {
                self.setStatus("Разрешите доступ к камере в настройках iPhone")
                return
            }
            self.cameraQueue.async {
                guard !self.session.isRunning else { return }
                do {
                    try self.configureCamera(front: self.usingFrontCamera)
                    self.session.startRunning()
                    try self.startListener()
                    DispatchQueue.main.async { self.running = true }
                    self.setStatus("Камера включена. Ожидание USB-подключения…")
                } catch {
                    self.session.stopRunning()
                    self.setStatus("Ошибка: \(error.localizedDescription)")
                }
            }
        }
    }

    func stop() {
        cameraQueue.async {
            self.networkQueue.async {
                self.client?.cancel()
                self.client = nil
                self.listener?.cancel()
                self.listener = nil
            }
            if self.session.isRunning { self.session.stopRunning() }
            self.sending = false
            DispatchQueue.main.async {
                self.running = false
                self.status = "Остановлено"
            }
        }
    }

    func flipCamera() {
        cameraQueue.async {
            let front = !self.usingFrontCamera
            do {
                try self.configureCamera(front: front)
                DispatchQueue.main.async { self.usingFrontCamera = front }
            } catch {
                self.setStatus("Не удалось переключить камеру: \(error.localizedDescription)")
            }
        }
    }

    private func configureCamera(front: Bool) throws {
        guard let camera = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: front ? .front : .back) else {
            throw CameraError.noCamera
        }
        let input = try AVCaptureDeviceInput(device: camera)
        session.beginConfiguration()
        defer { session.commitConfiguration() }
        session.sessionPreset = .hd1280x720
        for oldInput in session.inputs { session.removeInput(oldInput) }
        guard session.canAddInput(input) else { throw CameraError.cannotConfigure }
        session.addInput(input)
        if session.outputs.isEmpty {
            let output = AVCaptureVideoDataOutput()
            output.alwaysDiscardsLateVideoFrames = true
            output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
            guard session.canAddOutput(output) else { throw CameraError.cannotConfigure }
            session.addOutput(output)
            output.setSampleBufferDelegate(self, queue: cameraQueue)
        }
        if let videoConnection = session.outputs.first?.connection(with: .video) {
            videoConnection.videoOrientation = .landscapeRight
            if videoConnection.isVideoMirroringSupported { videoConnection.isVideoMirrored = front }
        }
        try camera.lockForConfiguration()
        if camera.activeFormat.videoSupportedFrameRateRanges.contains(where: { $0.minFrameRate <= 15 && $0.maxFrameRate >= 15 }) {
            camera.activeVideoMinFrameDuration = CMTime(value: 1, timescale: 15)
            camera.activeVideoMaxFrameDuration = CMTime(value: 1, timescale: 15)
        }
        camera.unlockForConfiguration()
    }

    private func startListener() throws {
        let listener = try NWListener(using: .tcp, on: NWEndpoint.Port(rawValue: 45321)!)
        self.listener = listener
        listener.newConnectionHandler = { [weak self] connection in
            guard let self = self else { return }
            self.networkQueue.async {
                self.client?.cancel()
                self.client = connection
                connection.stateUpdateHandler = { [weak self, weak connection] state in
                    guard let self = self else { return }
                    switch state {
                    case .ready:
                        self.setStatus("Компьютер подключён — видео передаётся")
                    case .failed, .cancelled:
                        if self.client === connection {
                            self.client = nil
                            self.setStatus("Ожидание USB-подключения…")
                        }
                    default: break
                    }
                }
                connection.start(queue: self.networkQueue)
            }
        }
        listener.stateUpdateHandler = { [weak self] state in
            if case .failed(let error) = state { self?.setStatus("Ошибка USB-потока: \(error.localizedDescription)") }
        }
        listener.start(queue: networkQueue)
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard !sending, let pixel = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        // 720p JPEG at 15 fps keeps the first version simple and usable in OBS.
        guard let jpeg = context.jpegRepresentation(of: CIImage(cvPixelBuffer: pixel), colorSpace: CGColorSpaceCreateDeviceRGB(), options: [.lossyCompressionQuality: 0.62]) else { return }
        var length = UInt32(jpeg.count).bigEndian
        var packet = Data(bytes: &length, count: 4)
        packet.append(jpeg)
        sending = true
        networkQueue.async {
            guard let client = self.client else {
                self.cameraQueue.async { self.sending = false }
                return
            }
            client.send(content: packet, completion: .contentProcessed { [weak self] error in
                guard let self = self else { return }
                if error != nil { client.cancel() }
                self.cameraQueue.async { self.sending = false }
            })
        }
    }

    private func setStatus(_ text: String) {
        DispatchQueue.main.async { self.status = text }
    }

    private enum CameraError: LocalizedError {
        case noCamera, cannotConfigure
        var errorDescription: String? {
            switch self {
            case .noCamera: return "Камера не найдена"
            case .cannotConfigure: return "Не удалось настроить камеру"
            }
        }
    }
}
