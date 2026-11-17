package main

import (
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
)

// appWithCatalogs makes an app directory with these catalogs.
func appWithCatalogs(t *testing.T, config string, catalogs map[string]string) string {
	t.Helper()
	dir := t.TempDir()
	if config != "" {
		if err := os.WriteFile(filepath.Join(dir, "asuka.toml"), []byte(config), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	for name, data := range catalogs {
		p := filepath.Join(dir, name)
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(data), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func TestCatalogsAreFound(t *testing.T) {
	dir := appWithCatalogs(t, "", map[string]string{"locales/en.json": `{"a": "A"}`, "locales/de.json": `{"a": "Ä"}`})
	c, err := loadConfig(dir)
	if err != nil {
		t.Fatal(err)
	}
	if c.I18n == nil || c.I18n.Dir != "locales" || c.I18n.Source != "en" {
		t.Fatalf("i18n %+v", c.I18n)
	}
	// One catalog is the source, whatever it's in.
	dir = appWithCatalogs(t, "", map[string]string{"locales/ja.json": `{"a": "あ"}`})
	if c, err = loadConfig(dir); err != nil || c.I18n.Source != "ja" {
		t.Fatalf("source %+v, %v", c.I18n, err)
	}
	// None, and none in asuka.toml: nothing.
	if c, err = loadConfig(t.TempDir()); err != nil || c.I18n != nil {
		t.Fatalf("%+v, %v", c.I18n, err)
	}
	dir = appWithCatalogs(t, "[i18n]\ndir = \"tr\"\nsource = \"de\"", map[string]string{"tr/de.json": `{"a": "Ä"}`, "tr/en.json": `{"a": "A"}`})
	if c, err = loadConfig(dir); err != nil || c.I18n.Dir != "tr" || c.I18n.Source != "de" {
		t.Fatalf("%+v, %v", c.I18n, err)
	}
}

func TestCheckCatalogs(t *testing.T) {
	dir := appWithCatalogs(t, "", map[string]string{"locales/en.json": `{"hello": "Hello, {name}!"}`, "locales/de.json": `{}`})
	c, err := loadConfig(dir)
	if err != nil {
		t.Fatal(err)
	}
	var out strings.Builder
	if err := c.checkCatalogs(&out); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "warning: 1 of 1 messages missing") || !strings.Contains(out.String(), "1 message in en, de") {
		t.Errorf("check said:\n%s", out.String())
	}
	// Nothing is written: the compiler reads the catalogs.
	if _, err := os.Stat(filepath.Join(dir, "locales", "catalog_gen.go")); err == nil {
		t.Error("a catalog_gen.go")
	}
	// A broken catalog is an error.
	if err := os.WriteFile(filepath.Join(dir, "locales", "en.json"), []byte(`{"hello": "{name"}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := c.checkCatalogs(io.Discard); err == nil {
		t.Error("no error for a message that doesn't parse")
	}
	if err := cmdGenerate([]string{dir}); err == nil {
		t.Error("asuka generate: no error for a message that doesn't parse")
	}
}

func TestPackagingStrings(t *testing.T) {
	dir := appWithCatalogs(t, "name = \"Notes\"\n\n[[shortcuts]]\nid = \"new\"\ntitle = \"New note\"\nurl = \"notes://new\"", map[string]string{
		"locales/en.json": `{"a": "A", "asuka.appName": "Notes"}`,
		"locales/de.json": `{"a": "Ä", "asuka.appName": "Notizen", "asuka.NSCameraUsageDescription": "Um \"Belege\" zu scannen.", "asuka.shortcut.new": "Neue Notiz"}`,
		"locales/ar.json": `{"a": "أ"}`,
	})
	c, err := loadConfig(dir)
	if err != nil {
		t.Fatal(err)
	}
	info := map[string]any{}
	c.plistLocalizations(info)
	if got := info["CFBundleLocalizations"].([]string); !slices.Equal(got, []string{"en", "ar", "de"}) {
		t.Errorf("localizations %v", got)
	}
	if info["CFBundleDevelopmentRegion"] != "en" {
		t.Errorf("region %v", info["CFBundleDevelopmentRegion"])
	}
	if !strings.Contains(c.plistLocalizationKeys(), "<string>de</string>") {
		t.Errorf("keys %s", c.plistLocalizationKeys())
	}

	bundle := t.TempDir()
	if err := c.writeLprojs(bundle); err != nil {
		t.Fatal(err)
	}
	de, _ := os.ReadFile(filepath.Join(bundle, "de.lproj", "InfoPlist.strings"))
	for _, want := range []string{`"CFBundleDisplayName" = "Notizen";`, `"NSCameraUsageDescription" = "Um \"Belege\" zu scannen.";`} {
		if !strings.Contains(string(de), want) {
			t.Errorf("no %s in %s", want, de)
		}
	}
	if loc, _ := os.ReadFile(filepath.Join(bundle, "de.lproj", "Localizable.strings")); !strings.Contains(string(loc), `"New note" = "Neue Notiz";`) {
		t.Errorf("localizable: %s", loc)
	}
	// A language needs its lproj even when it says nothing of these.
	if _, err := os.Stat(filepath.Join(bundle, "ar.lproj", "InfoPlist.strings")); err != nil {
		t.Error(err)
	}

	if got := c.androidLabel(); got != "@string/asuka_app_name" {
		t.Errorf("label %s", got)
	}
	if !strings.Contains(c.androidManifest(false), `android:localeConfig="@xml/asuka_locales_config"`) {
		t.Error("no locale config in the manifest")
	}
	res := t.TempDir()
	if wrote, err := c.androidLocaleRes(res); err != nil || !wrote {
		t.Fatalf("%v, %v", wrote, err)
	}
	cfg, _ := os.ReadFile(filepath.Join(res, "xml", "asuka_locales_config.xml"))
	if !strings.Contains(string(cfg), `<locale android:name="de" />`) {
		t.Errorf("config %s", cfg)
	}
	strs, _ := os.ReadFile(filepath.Join(res, "values-b+de", "asuka_i18n.xml"))
	for _, want := range []string{`<string name="asuka_app_name">Notizen</string>`, `<string name="asuka_shortcut_new">Neue Notiz</string>`} {
		if !strings.Contains(string(strs), want) {
			t.Errorf("no %s in %s", want, strs)
		}
	}
	if def, _ := os.ReadFile(filepath.Join(res, "values", "asuka_i18n.xml")); !strings.Contains(string(def), ">Notes<") {
		t.Errorf("default %s", def)
	}
	if androidQualifier("zh-Hant") != "b+zh+Hant" {
		t.Error(androidQualifier("zh-Hant"))
	}

	// Without catalogs: nothing, and the plain name.
	plain, err := loadConfig(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if plain.androidLabel() != plain.Name || strings.Contains(plain.androidManifest(false), "localeConfig") {
		t.Error("an app without catalogs got their settings")
	}
}

func TestI18nCommands(t *testing.T) {
	dir := t.TempDir()
	if err := cmdI18n([]string{"init", dir}); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(dir, "locales", "en.json")); err != nil {
		t.Error(err)
	}
	if err := cmdI18n([]string{"init", dir}); err == nil {
		t.Error("init twice: no error")
	}
	if err := cmdI18n([]string{"add", "de", "zh-TW", "--dir", dir}); err != nil {
		t.Fatal(err)
	}
	de, err := os.ReadFile(filepath.Join(dir, "locales", "de.json"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(de), `"appTitle": "Hello"`) || !strings.Contains(string(de), `"@@locale": "de"`) {
		t.Errorf("de.json: %s", de)
	}
	if err := cmdI18n([]string{"add", "en-XA", "--dir", dir}); err == nil {
		t.Error("a pseudo locale's catalog: no error")
	}
	if err := cmdI18n([]string{"check", dir}); err != nil {
		t.Error(err)
	}
	if err := setDevLocale("not a tag!", ""); err == nil {
		t.Error("--lang of no language: no error")
	}
	if err := setDevLocale("", "up"); err == nil {
		t.Error("--dir up: no error")
	}
	defer func() { devLang, devDirection = "", "" }()
	if err := setDevLocale("zh_TW", "rtl"); err != nil || devLang != "zh-TW" || devDirection != "rtl" {
		t.Errorf("%v %s %s", err, devLang, devDirection)
	}
	if c := (&asukaApp{cfg: &appConfig{}}).hostConfig(platformLinux, "", false); c.Lang != "zh-TW" || c.Direction != "rtl" {
		t.Errorf("the host's settings: %+v", c)
	}
	// And the module's, which every platform's player runs.
	defer func() { appbuild.DevLang, appbuild.DevDirection = "", "" }()
	if appbuild.DevLang != "zh-TW" || appbuild.DevDirection != "rtl" {
		t.Errorf("compiled in: %q %q", appbuild.DevLang, appbuild.DevDirection)
	}
}
