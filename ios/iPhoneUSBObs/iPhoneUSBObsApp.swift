import AVFoundation
import SwiftUI
import UIKit

@main
struct iPhoneUSBObsApp: App {
    @StateObject private var camera = CameraStreamer()

    var body: some Scene {
        WindowGroup {
            VStack(spacing: 16) {
                Text("iPhone USB Camera").font(.title2.bold())
                CameraPreview(session: camera.session)
                    .aspectRatio(16 / 9, contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                    .background(Color.black)
                Text(camera.status).multilineTextAlignment(.center)
                    .font(.callout).foregroundColor(.secondary)
                HStack(spacing: 12) {
                    Button(camera.running ? "Остановить" : "Начать") {
                        camera.running ? camera.stop() : camera.start()
                    }
                    .buttonStyle(.borderedProminent)
                    Button("Сменить камеру") { camera.flipCamera() }
                        .buttonStyle(.bordered)
                        .disabled(!camera.running)
                }
                Text("Оставьте приложение открытым и подключите iPhone к компьютеру кабелем Lightning.")
                    .font(.footnote).foregroundColor(.secondary).multilineTextAlignment(.center)
                Spacer()
            }
            .padding()
            .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
            .onDisappear { camera.stop(); UIApplication.shared.isIdleTimerDisabled = false }
        }
    }
}

private struct CameraPreview: UIViewRepresentable {
    let session: AVCaptureSession

    func makeUIView(context: Context) -> PreviewView {
        let view = PreviewView()
        view.previewLayer.session = session
        view.previewLayer.videoGravity = .resizeAspect
        return view
    }

    func updateUIView(_ uiView: PreviewView, context: Context) {}
}

private final class PreviewView: UIView {
    override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
    var previewLayer: AVCaptureVideoPreviewLayer { layer as! AVCaptureVideoPreviewLayer }
}
