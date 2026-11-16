// asuka/lottie on the desktop: macOS runs the package's Swift
// (lottie-ios); Windows and Linux have no Lottie player of their own, so
// the view is none there (its box stays empty).
package lottie

func newView(events ViewEvents) ViewNative { return nil }
