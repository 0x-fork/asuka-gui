package main

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// An app that imports asuka/purchase gets Play Billing's permission (its
// library is asuka/purchase's Gradle module's, not the host's); one that
// doesn't, none.
func TestPurchaseFeature(t *testing.T) {
	var c appConfig
	importStd(t, &c, "", "purchase")
	if len(c.androidModules) > 0 || !slices.Contains(c.Android.Permissions, "com.android.vending.BILLING") {
		t.Fatalf("modules %v, permissions %v", c.androidModules, c.Android.Permissions)
	}
	if b, err := os.ReadFile(filepath.Join("..", "..", "std", "purchase", "android", "build.gradle.kts")); err != nil || !strings.Contains(string(b), "com.android.billingclient:billing") {
		t.Errorf("asuka/purchase's module hasn't Play Billing: %v", err)
	}
	var none appConfig
	importStd(t, &none, "")
	if slices.Contains(none.Android.Permissions, "com.android.vending.BILLING") {
		t.Fatalf("permissions %v", none.Android.Permissions)
	}
}

// ios.storekit goes into a simulator build's bundle, as the host looks
// for it.
func TestCopyStoreKit(t *testing.T) {
	dir, bundle := t.TempDir(), t.TempDir()
	os.WriteFile(filepath.Join(dir, "Products.storekit"), []byte(`{"products":[]}`), 0o644)
	c := &appConfig{dir: dir}
	if err := c.copyStoreKit(bundle); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(bundle, storeKitName)); err == nil {
		t.Fatal("copied without ios.storekit")
	}
	c.IOS.StoreKit = "Products.storekit"
	if err := c.copyStoreKit(bundle); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(filepath.Join(bundle, storeKitName)); string(b) != `{"products":[]}` {
		t.Fatalf("copied %q", b)
	}
	c.IOS.StoreKit = "missing.storekit"
	if err := c.copyStoreKit(bundle); err == nil {
		t.Fatal("no error for a missing file")
	}
}
