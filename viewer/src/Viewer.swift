import AppKit
import MetalKit
import simd

enum ViewerError: Error, CustomStringConvertible {
    case invalid(String)
    var description: String { switch self { case .invalid(let message): return message } }
}
struct Reader {
    let data: Data
    var offset = 0
    mutating func bytes(_ count: Int) throws -> Data {
        guard count >= 0, count <= data.count - offset else { throw ViewerError.invalid("Truncated scene cache") }
        defer { offset += count }; return data.subdata(in: offset..<(offset + count))
    }
    mutating func u32() throws -> UInt32 {
        try bytes(4).withUnsafeBytes { $0.loadUnaligned(as: UInt32.self).littleEndian }
    }
    mutating func float() throws -> Float { Float(bitPattern: try u32()) }
    mutating func string() throws -> String {
        let count = Int(try u32()); guard count < 1_000_000 else { throw ViewerError.invalid("Invalid string length") }
        guard let value = String(data: try bytes(count), encoding: .utf8) else { throw ViewerError.invalid("Invalid UTF-8") }
        return value
    }
}
struct Draw {
    let label: String
    let vertex: MTLBuffer
    let index: MTLBuffer
    let texture: MTLTexture
    let vertexCount: Int
    let indexCount: Int
}
struct Scene {
    let name: String
    let source: String
    let note: String
    let center: SIMD3<Float>
    let radius: Float
    let draws: [Draw]
    var vertices: Int { draws.reduce(0) { $0 + $1.vertexCount } }
    var triangles: Int { draws.reduce(0) { $0 + $1.indexCount / 3 } }
}
func loadScenes(_ path: String, device: MTLDevice) throws -> [Scene] {
    var reader = Reader(data: try Data(contentsOf: URL(fileURLWithPath: path)))
    guard try reader.bytes(8) == Data("SWGVIEW1".utf8) else { throw ViewerError.invalid("Wrong cache format") }
    let count = Int(try reader.u32()); guard count == 3 else { throw ViewerError.invalid("Expected three scenes") }
    var scenes: [Scene] = []
    for _ in 0..<count {
        let name = try reader.string(), source = try reader.string(), note = try reader.string()
        let center = SIMD3<Float>(try reader.float(), try reader.float(), try reader.float())
        let radius = try reader.float(), draws = Int(try reader.u32())
        guard center.x.isFinite, center.y.isFinite, center.z.isFinite,
              radius.isFinite, radius > 0, draws > 0, draws < 10000 else { throw ViewerError.invalid("Invalid scene bounds") }
        var buffers: [Draw] = []
        for _ in 0..<draws {
            let label = try reader.string(), vertices = Int(try reader.u32()), indices = Int(try reader.u32())
            let width = Int(try reader.u32()), height = Int(try reader.u32()), textureSize = Int(try reader.u32())
            guard vertices > 0, vertices < 10_000_000, indices > 0, indices % 3 == 0, indices < 30_000_000,
                  width > 0, height > 0, width <= 8192, height <= 8192, textureSize == width * height * 4
            else { throw ViewerError.invalid("Invalid draw dimensions") }
            let vertexData = try reader.bytes(vertices * 44), indexData = try reader.bytes(indices * 4)
            let validIndices = indexData.withUnsafeBytes { raw in
                (0..<indices).allSatisfy { Int(raw.loadUnaligned(fromByteOffset: $0 * 4, as: UInt32.self).littleEndian) < vertices }
            }
            guard validIndices else { throw ViewerError.invalid("Mesh index outside vertex buffer") }
            let pixels = try reader.bytes(textureSize)
            guard let vertex = vertexData.withUnsafeBytes({ device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared) }),
                  let index = indexData.withUnsafeBytes({ device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared) })
            else { throw ViewerError.invalid("Cannot allocate Metal buffers") }
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm_srgb, width: width, height: height, mipmapped: false)
            descriptor.usage = .shaderRead; descriptor.storageMode = .shared
            guard let texture = device.makeTexture(descriptor: descriptor) else { throw ViewerError.invalid("Cannot allocate Metal texture") }
            pixels.withUnsafeBytes { texture.replace(region: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: width * 4) }
            buffers.append(Draw(label: label, vertex: vertex, index: index, texture: texture, vertexCount: vertices, indexCount: indices))
        }
        scenes.append(Scene(name: name, source: source, note: note, center: center, radius: radius, draws: buffers))
    }
    guard reader.offset == reader.data.count else { throw ViewerError.invalid("Unexpected trailing scene data") }
    return scenes
}
let shader = """
#include <metal_stdlib>
using namespace metal;
struct Vertex { packed_float3 position; packed_float3 normal; packed_float2 uv; packed_float3 color; };
struct Uniform { float4x4 mvp; };
struct Out { float4 position [[position]]; float3 normal; float2 uv; float3 color; };
vertex Out vertexMain(uint id [[vertex_id]], const device Vertex* vertices [[buffer(0)]], constant Uniform& u [[buffer(1)]]) {
    Vertex v = vertices[id]; Out o;
    o.position = u.mvp * float4(float3(v.position), 1.0);
    o.normal = float3(v.normal); o.uv = float2(v.uv); o.color = pow(float3(v.color), float3(2.2));
    return o;
}
fragment float4 fragmentMain(Out in [[stage_in]], texture2d<float> diffuse [[texture(0)]]) {
    constexpr sampler s(coord::normalized, address::repeat, filter::linear);
    float4 texel = diffuse.sample(s, in.uv);
    if (texel.a < 0.25) discard_fragment();
    float3 n = normalize(in.normal);
    float key = abs(dot(n, normalize(float3(-0.6, 0.8, 0.5))));
    float fill = abs(dot(n, normalize(float3(0.7, 0.3, -0.5))));
    float light = 0.28 + 0.57 * key + 0.15 * fill;
    return float4(texel.rgb * in.color * light, 1.0);
}
"""
func perspective(_ fov: Float, _ aspect: Float, _ near: Float, _ far: Float) -> simd_float4x4 {
    let y = 1 / tan(fov / 2), z = far / (near - far)
    return simd_float4x4(columns: (SIMD4(y / aspect, 0, 0, 0), SIMD4(0, y, 0, 0), SIMD4(0, 0, z, -1), SIMD4(0, 0, near * z, 0)))
}
func lookAt(_ eye: SIMD3<Float>, _ center: SIMD3<Float>) -> simd_float4x4 {
    let z = normalize(eye - center), x = normalize(cross(SIMD3<Float>(0, 1, 0), z)), y = cross(z, x)
    return simd_float4x4(columns: (SIMD4(x.x, y.x, z.x, 0), SIMD4(x.y, y.y, z.y, 0), SIMD4(x.z, y.z, z.z, 0), SIMD4(-dot(x, eye), -dot(y, eye), -dot(z, eye), 1)))
}
final class Renderer: NSObject, MTKViewDelegate {
    let device: MTLDevice
    let queue: MTLCommandQueue
    let pipeline: MTLRenderPipelineState
    let depth: MTLDepthStencilState
    let scenes: [Scene]
    var selected = 0
    var yaw: Float = 0.6, pitch: Float = 0.58, distance: Float = 2.9
    var wireframe = false
    var pan = SIMD3<Float>.zero
    var lastError: String?
    var frameCount = 0
    init(device: MTLDevice, scenes: [Scene]) throws {
        self.device = device; self.scenes = scenes
        guard let queue = device.makeCommandQueue() else { throw ViewerError.invalid("Metal command queue unavailable") }
        self.queue = queue
        let library = try device.makeLibrary(source: shader, options: nil)
        let p = MTLRenderPipelineDescriptor()
        p.vertexFunction = library.makeFunction(name: "vertexMain"); p.fragmentFunction = library.makeFunction(name: "fragmentMain")
        p.colorAttachments[0].pixelFormat = .bgra8Unorm_srgb; p.depthAttachmentPixelFormat = .depth32Float
        pipeline = try device.makeRenderPipelineState(descriptor: p)
        let d = MTLDepthStencilDescriptor(); d.depthCompareFunction = .less; d.isDepthWriteEnabled = true
        guard let state = device.makeDepthStencilState(descriptor: d) else { throw ViewerError.invalid("Depth buffer unavailable") }
        depth = state; super.init()
    }
    func reset() {
        yaw = selected == 2 ? 0.25 : 0.6; pitch = selected == 0 ? 0.58 : (selected == 2 ? 0.12 : 0.32)
        distance = 2.9; pan = .zero
    }
    func matrix(width: Int, height: Int) -> simd_float4x4 {
        let scene = scenes[selected], target = scene.center + pan
        let offset = SIMD3<Float>(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)) * scene.radius * distance
        return perspective(.pi / 4, Float(width) / Float(max(1, height)), max(scene.radius * 0.001, 0.001), scene.radius * 40) * lookAt(target + offset, target)
    }
    func encode(_ command: MTLCommandBuffer, pass: MTLRenderPassDescriptor, width: Int, height: Int) throws {
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { throw ViewerError.invalid("Cannot start render pass") }
        encoder.setRenderPipelineState(pipeline); encoder.setDepthStencilState(depth)
        encoder.setCullMode(.none); encoder.setTriangleFillMode(wireframe ? .lines : .fill)
        var mvp = matrix(width: width, height: height)
        encoder.setVertexBytes(&mvp, length: MemoryLayout<simd_float4x4>.stride, index: 1)
        for draw in scenes[selected].draws {
            encoder.setVertexBuffer(draw.vertex, offset: 0, index: 0); encoder.setFragmentTexture(draw.texture, index: 0)
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: draw.indexCount, indexType: .uint32, indexBuffer: draw.index, indexBufferOffset: 0)
        }
        encoder.endEncoding()
    }
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
    func draw(in view: MTKView) {
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable, let command = queue.makeCommandBuffer() else { return }
        do {
            try encode(command, pass: pass, width: drawable.texture.width, height: drawable.texture.height)
            command.present(drawable); command.commit(); frameCount += 1
        } catch { lastError = String(describing: error); NSLog("Render failure: %@", lastError!) }
    }
    func capture(_ url: URL, width: Int = 1400, height: Int = 1000) throws -> Int {
        let colorDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm_srgb, width: width, height: height, mipmapped: false)
        colorDescriptor.usage = [.renderTarget]; colorDescriptor.storageMode = .shared
        let depthDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .depth32Float, width: width, height: height, mipmapped: false)
        depthDescriptor.usage = .renderTarget; depthDescriptor.storageMode = .private
        guard let color = device.makeTexture(descriptor: colorDescriptor), let z = device.makeTexture(descriptor: depthDescriptor), let command = queue.makeCommandBuffer() else { throw ViewerError.invalid("Cannot create capture textures") }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = color; pass.colorAttachments[0].loadAction = .clear; pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = MTLClearColor(red: 0.028, green: 0.043, blue: 0.064, alpha: 1)
        pass.depthAttachment.texture = z; pass.depthAttachment.loadAction = .clear; pass.depthAttachment.clearDepth = 1
        try encode(command, pass: pass, width: width, height: height); command.commit(); command.waitUntilCompleted()
        if let error = command.error { throw error }
        var bgra = [UInt8](repeating: 0, count: width * height * 4)
        bgra.withUnsafeMutableBytes { color.getBytes($0.baseAddress!, bytesPerRow: width * 4, from: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0) }
        var rgba = bgra, foreground = 0
        let corner = Array(bgra.prefix(3))
        for i in stride(from: 0, to: rgba.count, by: 4) {
            rgba[i] = bgra[i + 2]; rgba[i + 2] = bgra[i]
            if abs(Int(bgra[i]) - Int(corner[0])) + abs(Int(bgra[i + 1]) - Int(corner[1])) + abs(Int(bgra[i + 2]) - Int(corner[2])) > 12 { foreground += 1 }
        }
        guard foreground > 1000 else { throw ViewerError.invalid("Capture is empty: \(scenes[selected].name)") }
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: width * 4, bitsPerPixel: 32)!
        _ = rgba.withUnsafeBytes { memcpy(bitmap.bitmapData!, $0.baseAddress!, rgba.count) }
        guard let png = bitmap.representation(using: .png, properties: [:]) else { throw ViewerError.invalid("PNG encoding failed") }
        try png.write(to: url, options: .atomic); return foreground
    }
}
final class OrbitView: MTKView {
    var renderer: Renderer!
    var wireframeChanged: ((Bool) -> Void)?
    var lastDragPoint: NSPoint?
    override var acceptsFirstResponder: Bool { true }
    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self); lastDragPoint = convert(event.locationInWindow, from: nil)
    }
    override func mouseUp(with event: NSEvent) { lastDragPoint = nil }
    override func mouseDragged(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        guard let previous = lastDragPoint else { lastDragPoint = point; return }
        lastDragPoint = point
        let dx = Float(point.x - previous.x), dy = Float(previous.y - point.y)
        if event.modifierFlags.contains(.shift) {
            let scale = renderer.scenes[renderer.selected].radius * renderer.distance * 0.0018
            let right = SIMD3<Float>(cos(renderer.yaw), 0, -sin(renderer.yaw))
            renderer.pan += right * -dx * scale + SIMD3<Float>(0, 1, 0) * dy * scale
        } else {
            renderer.yaw -= dx * 0.009
            renderer.pitch = min(1.45, max(-1.4, renderer.pitch + dy * 0.009))
        }
        draw()
    }
    override func scrollWheel(with event: NSEvent) {
        renderer.distance = min(15, max(0.08, renderer.distance * exp(Float(event.scrollingDeltaY) * 0.012)))
        draw()
    }
    override func keyDown(with event: NSEvent) {
        switch event.charactersIgnoringModifiers?.lowercased() {
        case "r": renderer.reset(); draw()
        case "w": renderer.wireframe.toggle(); wireframeChanged?(renderer.wireframe); draw()
        default: super.keyDown(with: event)
        }
    }
}
func label(_ text: String, size: CGFloat, weight: NSFont.Weight = .regular, color: NSColor = .labelColor) -> NSTextField {
    let field = NSTextField(wrappingLabelWithString: text); field.font = .systemFont(ofSize: size, weight: weight); field.textColor = color
    return field
}
final class App: NSObject, NSApplicationDelegate {
    let renderer: Renderer
    let manifest: [String: Any]
    var window: NSWindow!
    var view: OrbitView!
    var details: NSTextField!
    var selector: NSSegmentedControl!
    init(_ renderer: Renderer, manifest: [String: Any]) { self.renderer = renderer; self.manifest = manifest }
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        let menu = NSMenu(); let appItem = NSMenuItem(); menu.addItem(appItem); let submenu = NSMenu()
        submenu.addItem(withTitle: "Quit SWG Asset Viewer", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"); appItem.submenu = submenu; NSApp.mainMenu = menu
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 820), styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "SWG Asset Viewer — Apple silicon / Metal"; window.minSize = NSSize(width: 900, height: 620)
        window.appearance = NSAppearance(named: .darkAqua); window.backgroundColor = NSColor(calibratedRed: 0.028, green: 0.043, blue: 0.064, alpha: 1)
        let root = window.contentView!
        let heading = label("SWG  /  ASSET VIEWER", size: 22, weight: .semibold)
        let subtitle = label("APPLE SILICON     ·     METAL     ·     OFFLINE", size: 11, weight: .medium, color: .secondaryLabelColor)
        selector = NSSegmentedControl(labels: ["Terrain", "Building", "Character"], trackingMode: .selectOne, target: self, action: #selector(changeScene))
        selector.selectedSegment = 0; selector.segmentStyle = .rounded
        let reset = NSButton(title: "Reset camera", target: self, action: #selector(resetCamera))
        let wire = NSButton(checkboxWithTitle: "Wireframe", target: self, action: #selector(toggleWire))
        let buttons = NSStackView(views: [selector, reset, wire]); buttons.spacing = 16
        let title = NSStackView(views: [heading, subtitle]); title.orientation = .vertical; title.alignment = .leading; title.spacing = 8
        let header = NSStackView(views: [title, NSView(), buttons]); header.distribution = .fill; header.spacing = 24
        let sideTitle = label("LOCAL ASSET", size: 11, weight: .semibold, color: .secondaryLabelColor)
        details = label("", size: 13)
        let controls = label("CONTROLS\n\nDrag to orbit\nScroll to zoom\nShift + drag to pan\nR to reset · W for wireframe", size: 12, color: .secondaryLabelColor)
        let deviceInfo = label("\(renderer.device.name)\nNative ARM64 · 64-bit\nNo server connection", size: 11, color: .secondaryLabelColor)
        let sidebar = NSStackView(views: [sideTitle, details, NSView(), controls, deviceInfo])
        sidebar.orientation = .vertical; sidebar.alignment = .leading; sidebar.spacing = 20
        view = OrbitView(frame: .zero, device: renderer.device); view.renderer = renderer; view.delegate = renderer
        view.wireframeChanged = { enabled in wire.state = enabled ? .on : .off }
        view.colorPixelFormat = .bgra8Unorm_srgb; view.depthStencilPixelFormat = .depth32Float
        view.clearColor = MTLClearColor(red: 0.028, green: 0.043, blue: 0.064, alpha: 1)
        view.isPaused = true; view.enableSetNeedsDisplay = true
        for child in [header, sidebar, view!] { child.translatesAutoresizingMaskIntoConstraints = false; root.addSubview(child) }
        NSLayoutConstraint.activate([
            header.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 26), header.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -26), header.topAnchor.constraint(equalTo: root.topAnchor, constant: 20), header.heightAnchor.constraint(equalToConstant: 60),
            sidebar.topAnchor.constraint(equalTo: header.bottomAnchor, constant: 36), sidebar.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -26), sidebar.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -26), sidebar.widthAnchor.constraint(equalToConstant: 242),
            view.leadingAnchor.constraint(equalTo: root.leadingAnchor), view.topAnchor.constraint(equalTo: header.bottomAnchor, constant: 18), view.trailingAnchor.constraint(equalTo: sidebar.leadingAnchor, constant: -18), view.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            details.widthAnchor.constraint(equalTo: sidebar.widthAnchor), controls.widthAnchor.constraint(equalTo: sidebar.widthAnchor), deviceInfo.widthAnchor.constraint(equalTo: sidebar.widthAnchor)
        ])
        updateDetails(); window.center(); window.makeKeyAndOrderFront(nil); window.makeFirstResponder(view); NSApp.activate(ignoringOtherApps: true)
    }
    func updateDetails() {
        let scene = renderer.scenes[renderer.selected]
        let assets = manifest["assets"] as? [[String: Any]] ?? []
        let archive = assets.first { $0["path"] as? String == scene.source }?["archive"] as? String ?? "See manifest.json"
        details.stringValue = "\(scene.name)\n\n\(scene.source)\n\nArchive\n\(archive)\n\n\(scene.vertices.formatted()) vertices\n\(scene.triangles.formatted()) triangles\n\(scene.draws.count) material groups\n\n\(scene.note)"
    }
    @objc func changeScene() { renderer.selected = selector.selectedSegment; renderer.reset(); updateDetails(); view.draw() }
    @objc func resetCamera() { renderer.reset(); view.draw() }
    @objc func toggleWire(_ sender: NSButton) { renderer.wireframe = sender.state == .on; view.draw() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
do {
    let arguments = CommandLine.arguments
    let path: String
    if let index = arguments.firstIndex(of: "--scene"), index + 1 < arguments.count {
        path = arguments[index + 1]
    } else if Bundle.main.bundleURL.pathExtension == "app" {
        path = Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent("cache/scene.bin").path
    } else { throw ViewerError.invalid("Usage: SWGAssetViewer --scene scene.bin [--render-all DIRECTORY]") }
    guard let device = MTLCreateSystemDefaultDevice() else { throw ViewerError.invalid("No Metal device available") }
    let scenes = try loadScenes(path, device: device), renderer = try Renderer(device: device, scenes: scenes)
    if let capture = arguments.firstIndex(of: "--render-all"), capture + 1 < arguments.count {
        let directory = URL(fileURLWithPath: arguments[capture + 1]); try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var captures: [[String: Any]] = []
        for scene in scenes.indices {
            renderer.selected = scene; renderer.reset()
            let url = directory.appendingPathComponent(["terrain", "building", "character"][scene] + ".png")
            let pixels = try renderer.capture(url)
            captures.append(["scene": scenes[scene].name, "foreground_pixels": pixels, "triangles": scenes[scene].triangles, "file": url.path])
            print("Rendered \(scenes[scene].name): \(pixels) foreground pixels → \(url.path)")
        }
        let report: [String: Any] = ["device": device.name, "architecture": "arm64", "api": "Metal", "captures": captures]
        try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]).write(to: directory.appendingPathComponent("render-report.json"))
    } else {
        let manifestURL = URL(fileURLWithPath: path).deletingLastPathComponent().appendingPathComponent("manifest.json")
        let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: manifestURL)) as? [String: Any] ?? [:]
        let application = NSApplication.shared; let delegate = App(renderer, manifest: manifest); application.delegate = delegate
        withExtendedLifetime(delegate) { application.run() }
    }
} catch {
    fputs("SWG Asset Viewer: \(error)\n", stderr)
    if Bundle.main.bundleURL.pathExtension == "app" {
        let application = NSApplication.shared; application.setActivationPolicy(.regular)
        let alert = NSAlert(); alert.messageText = "Unable to open local assets"
        alert.informativeText = "\(error)\n\nRun run-viewer.sh --refresh in the viewer folder to rebuild the cache from your .tre files."
        alert.runModal()
    }
    exit(1)
}
