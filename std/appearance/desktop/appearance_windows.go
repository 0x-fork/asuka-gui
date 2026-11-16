//go:build windows

package appearance

import . "github.com/egoist/asuka/internal/windows"

// Windows: the accent color (Settings, Personalization, Colors: the
// accent palette's own entry, else DWM's colorization color), a contrast
// theme on (SPI_GETHIGHCONTRAST) as Increase Contrast, and transparency
// effects off (the Personalize key's EnableTransparency) as Reduce
// Transparency. They're read again as the system window hears
// WM_SETTINGCHANGE, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED or
// WM_THEMECHANGED.

func supported() error { return nil }

func settings() Settings {
	var s Settings
	if c, ok := AccentColor(); ok { // 0x00BBGGRR
		s.HasAccent, s.Red, s.Green, s.Blue = true, int(byte(c)), int(byte(c>>8)), int(byte(c>>16))
	}
	s.IncreaseContrast = HighContrast()
	s.ReduceTransparency = TransparencyOff()
	return s
}

func follow(changed func()) (stop func()) {
	return OnSystemMessage(func(m uint32, _, _ uintptr) {
		switch m {
		case WM_SETTINGCHANGE, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED, WM_THEMECHANGED:
			changed()
		}
	})
}
