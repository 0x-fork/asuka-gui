# Installs the asuka CLI on Windows from its GitHub release:
#
#	irm https://raw.githubusercontent.com/egoist/asuka/main/scripts/install.ps1 | iex
#
# $env:ASUKA_VERSION picks a release (default: the latest);
# $env:ASUKA_INSTALL the directory (default:
# %LOCALAPPDATA%\Programs\asuka\bin), which is added to the user's PATH.
# Windows PowerShell 5.1 and PowerShell 7; x64 and arm64.

# A function, and throw, not exit: run by iex, exit would close the
# shell it's run in.
function Install-Asuka {
	$ErrorActionPreference = 'Stop'
	$ProgressPreference = 'SilentlyContinue' # 5.1's progress bar slows a download down
	if ($PSVersionTable.PSVersion.Major -lt 6) {
		# Windows PowerShell 5.1 may not offer TLS 1.2 by itself.
		[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
	}

	$repo = 'egoist/asuka'
	$dir = $env:ASUKA_INSTALL
	if (-not $dir) { $dir = Join-Path $env:LOCALAPPDATA 'Programs\asuka\bin' }

	# The OS's architecture, not the process's (an x64 PowerShell runs
	# emulated on an arm64 PC).
	$arch = $null
	try { $arch = [string][System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture } catch {}
	if (-not $arch) {
		$arch = $env:PROCESSOR_ARCHITEW6432
		if (-not $arch) { $arch = $env:PROCESSOR_ARCHITECTURE }
	}
	switch -Regex ($arch) {
		'^(Arm64|ARM64)$' { $cpu = 'arm64' }
		'^(X64|AMD64)$' { $cpu = 'x64' }
		default { throw "asuka: no build for $arch (x64 and arm64 only)" }
	}
	$asset = "asuka-windows-$cpu.exe"

	if ($env:ASUKA_VERSION) {
		$base = "https://github.com/$repo/releases/download/v$($env:ASUKA_VERSION -replace '^v', '')"
	} else {
		$base = "https://github.com/$repo/releases/latest/download"
	}

	$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("asuka-" + [guid]::NewGuid())
	New-Item -ItemType Directory -Path $tmp | Out-Null
	try {
		Write-Host "asuka: downloading $asset"
		$exe = Join-Path $tmp 'asuka.exe'
		$sums = Join-Path $tmp 'SHA256SUMS'
		Invoke-WebRequest -UseBasicParsing -Uri "$base/$asset" -OutFile $exe
		Invoke-WebRequest -UseBasicParsing -Uri "$base/SHA256SUMS" -OutFile $sums
		$want = $null
		foreach ($line in Get-Content $sums) {
			$f = $line -split '\s+', 2
			if ($f.Count -eq 2 -and $f[1].TrimStart('*') -eq $asset) { $want = $f[0].ToLower() }
		}
		if (-not $want) { throw "asuka: SHA256SUMS has no $asset" }
		$got = (Get-FileHash -Algorithm SHA256 $exe).Hash.ToLower()
		if ($want -ne $got) { throw "asuka: the download doesn't match its checksum" }

		New-Item -ItemType Directory -Force -Path $dir | Out-Null
		$dest = Join-Path $dir 'asuka.exe'
		$old = "$dest.old"
		Remove-Item -Force -ErrorAction SilentlyContinue $old
		if (Test-Path $dest) {
			# A running asuka.exe can't be overwritten, but it can be renamed.
			Move-Item -Force $dest $old
		}
		Move-Item -Force $exe $dest
		Remove-Item -Force -ErrorAction SilentlyContinue $old
		Write-Host "asuka: installed $dest ($(& $dest version))"
	} finally {
		Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $tmp
	}

	# The user's PATH, as the registry has it (not this session's, which has
	# the machine's too), and this session's.
	$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
	$parts = @()
	if ($userPath) { $parts = $userPath -split ';' | Where-Object { $_ } }
	if (-not ($parts | Where-Object { $_.TrimEnd('\') -ieq $dir.TrimEnd('\') })) {
		[Environment]::SetEnvironmentVariable('Path', (@($parts) + $dir) -join ';', 'User')
		Write-Host "asuka: added $dir to your PATH (new terminals have it)"
	}
	if (-not (($env:Path -split ';') | Where-Object { $_.TrimEnd('\') -ieq $dir.TrimEnd('\') })) {
		$env:Path = "$env:Path;$dir"
	}
}

Install-Asuka
