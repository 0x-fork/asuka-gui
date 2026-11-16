package windows_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// UI Automation, as a screen reader reads the app (testdata/a11y.asuka):
// a client in the VM (PowerShell's UIAutomationClient) walks the
// window's elements and prints their names, control types and help texts,
// and invokes a pressable view.
func TestAccessibility(t *testing.T) {
	w := onWindows(t)
	if w.vm == "" && os.Getenv("ASUKA_UIA") == "" {
		t.Skip("reads the desktop's UI Automation tree: in the VM, or with ASUKA_UIA set")
	}
	local, guest := w.job(t)
	src, err := os.ReadFile(filepath.Join("testdata", "a11y.asuka"))
	if err != nil {
		t.Fatal(err)
	}
	buildApp(t, w, string(src), local)
	if err := os.WriteFile(filepath.Join(local, "uia.ps1"), []byte(uiaScript), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := w.run(t, guest, `C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`, map[string]string{
		"ASUKA_SCRIPT":   "waitfor:" + guest + `\uia.done`,
		"ASUKA_SNAPSHOT": guest + `\end.png`,
		"ASUKA_DUMP":     "1",
	}, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", guest+`\uia.ps1`, guest)
	if err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	t.Logf("%s", out)
	for _, want := range []string{
		"Button|Open the message, From Ann, Lunch?|",
		"Text|Inbox|",
		"Group|Weather|Sunny all day",
		"Button|Save|Saves the note",
		"ProgressBar||",
		"range 40 0 100 True",
	} {
		if !strings.Contains(out, want) {
			t.Errorf("no %q in the tree", want)
		}
	}
	if strings.Contains(out, "Secret") {
		t.Errorf("a hidden view is read:\n%s", out)
	}
	dump, _ := os.ReadFile(filepath.Join(local, "end.png.txt"))
	if !strings.Contains(string(dump), `"Opened: 1"`) {
		t.Errorf("invoking the row didn't press it:\n%s", dump)
	}
}

// uiaScript starts the app (in the job, args[0]), finds its window, and
// prints each element under it: control type|name|help text, a range's
// value, min, max and read only; it invokes the element named "Open the
// message...", then lets the app end (uia.done) and waits for it.
const uiaScript = `
param([string]$dir)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$p = Start-Process -FilePath "$dir\app.exe" -WorkingDirectory $dir -PassThru -RedirectStandardOutput "$dir\app.txt" -RedirectStandardError "$dir\app.err.txt" -WindowStyle Normal
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 100 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 200; $p.Refresh(); $h = $p.MainWindowHandle }
if ($h -eq [IntPtr]::Zero) { "no window"; Set-Content "$dir\uia.done" "x"; $p.WaitForExit(); exit 1 }
Start-Sleep -Milliseconds 1500
$root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
$all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
foreach ($e in $all) {
  $c = $e.Current
  $type = $c.ControlType.ProgrammaticName -replace '^ControlType\.', ''
  "$type|$($c.Name)|$($c.HelpText)"
  $o = $null
  if ($e.TryGetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$o)) {
    $v = $o.Current
    "range $($v.Value) $($v.Minimum) $($v.Maximum) $($v.IsReadOnly)"
  }
  if ($c.Name -like 'Open the message*') {
    $inv = $null
    if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$inv)) { $inv.Invoke(); "invoked" }
  }
}
Start-Sleep -Milliseconds 800
Set-Content "$dir\uia.done" "x"
$p.WaitForExit()
Get-Content "$dir\app.err.txt"
exit 0
`
