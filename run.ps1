#requires -Version 5.1
# MiniOS launcher - starts the 1.44 MB bootable floppy image in QEMU.
$ErrorActionPreference = "Stop"

$image = Join-Path $PSScriptRoot "MiniOS.img"
if (-not (Test-Path $image)) {
    throw "MiniOS.img not found. Run: python build.py"
}

$qemu = $null
$cmd = Get-Command qemu-system-i386 -ErrorAction SilentlyContinue
if ($cmd) { $qemu = $cmd.Source }
if (-not $qemu) {
    foreach ($candidate in @(
        "C:\msys64\ucrt64\bin\qemu-system-i386.exe",
        "C:\Program Files\qemu\qemu-system-i386.exe",
        "C:\Program Files (x86)\qemu\qemu-system-i386.exe"
    )) {
        if (Test-Path $candidate) { $qemu = $candidate; break }
    }
}
if (-not $qemu) {
    throw "qemu-system-i386 was not found. Install QEMU first (e.g. 'pacman -S mingw-w64-ucrt-x86_64-qemu')."
}

Write-Host "MiniOS: $image"
Write-Host "QEMU:   $qemu"
Write-Host "Starting... use the mouse with the QEMU window, Ctrl+Alt+G releases the pointer."

& $qemu `
    -name "MiniOS" `
    -drive "file=$image,if=floppy,format=raw,readonly=on" `
    -boot a `
    -m 128 `
    -rtc base=localtime
