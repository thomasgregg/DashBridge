import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

// Coordinates and colours are copied from web/favicon.svg, the mark in the README banner.
// The square background stays opaque; iOS applies the Home Screen corner mask.
let size = 1024
let output = URL(fileURLWithPath: CommandLine.arguments[1])
guard let colourSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
    fatalError("Could not create sRGB colour space")
}
guard let context = CGContext(data: nil, width: size, height: size,
                              bitsPerComponent: 8, bytesPerRow: 0,
                              space: colourSpace,
                              bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else {
    fatalError("Could not create icon image")
}

guard let background = CGColor(colorSpace: colourSpace,
                               components: [16 / 255, 43 / 255, 50 / 255, 1]),
      let mint = CGColor(colorSpace: colourSpace,
                         components: [121 / 255, 228 / 255, 199 / 255, 1]) else {
    fatalError("Could not create icon colours")
}
context.setFillColor(background)
context.fill(CGRect(x: 0, y: 0, width: size, height: size))
context.translateBy(x: 0, y: CGFloat(size))
context.scaleBy(x: CGFloat(size) / 64, y: -CGFloat(size) / 64)
context.setStrokeColor(mint)
context.setLineWidth(4)
context.setLineCap(.round)

context.move(to: CGPoint(x: 12, y: 46))
context.addLine(to: CGPoint(x: 52, y: 46))
context.move(to: CGPoint(x: 18, y: 46))
context.addLine(to: CGPoint(x: 18, y: 18))
context.addCurve(to: CGPoint(x: 46, y: 18),
                 control1: CGPoint(x: 18, y: 38), control2: CGPoint(x: 46, y: 38))
context.addLine(to: CGPoint(x: 46, y: 46))
context.move(to: CGPoint(x: 26, y: 34))
context.addLine(to: CGPoint(x: 26, y: 46))
context.move(to: CGPoint(x: 38, y: 34))
context.addLine(to: CGPoint(x: 38, y: 46))
context.strokePath()

guard let image = context.makeImage(),
      let destination = CGImageDestinationCreateWithURL(output as CFURL, UTType.png.identifier as CFString, 1, nil) else {
    fatalError("Could not save icon image")
}
CGImageDestinationAddImage(destination, image, nil)
guard CGImageDestinationFinalize(destination) else { fatalError("Could not finalize icon image") }
