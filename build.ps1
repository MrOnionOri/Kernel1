param(
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$BuildDir = "build"
$KernelSectors = 64
$KernelBytes = $KernelSectors * 512
$ImagePath = Join-Path $BuildDir "kernel1.img"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required command: $Name"
    }
}

if ($Clean) {
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    exit 0
}

Require-Command "nasm"
Require-Command "i686-elf-gcc"
Require-Command "i686-elf-objcopy"

New-Item -ItemType Directory -Force $BuildDir | Out-Null

nasm -f bin boot/boot.asm -o "$BuildDir/boot.bin"

$Objects = @()
$IncludeFlags = @(
    "-Ikernel/core",
    "-Ikernel/mm",
    "-Ikernel/drivers",
    "-Ikernel/arch/x86",
    "-Ikernel/lib"
)

$AsmFiles = @(Get-Item kernel/arch/x86/entry.asm) + @(
    Get-ChildItem kernel,user -Recurse -Filter *.asm | Where-Object { $_.Name -ne "entry.asm" } | Sort-Object FullName
)
$AsmFiles | ForEach-Object {
    $ObjectName = ($_.FullName.Substring((Get-Location).Path.Length + 1) -replace '[\\/]', '__' -replace '\.asm$', '.o')
    $ObjectPath = Join-Path $BuildDir $ObjectName
    nasm -f elf32 $_.FullName -o $ObjectPath
    $Objects += $ObjectPath
}

Get-ChildItem kernel -Recurse -Filter *.c | Sort-Object FullName | ForEach-Object {
    $ObjectName = ($_.FullName.Substring((Get-Location).Path.Length + 1) -replace '[\\/]', '__' -replace '\.c$', '.o')
    $ObjectPath = Join-Path $BuildDir $ObjectName
    i686-elf-gcc -ffreestanding -m32 -fno-pie -fno-stack-protector -nostdlib `
        $IncludeFlags -Wall -Wextra -c $_.FullName -o $ObjectPath
    $Objects += $ObjectPath
}

i686-elf-gcc -T linker.ld -ffreestanding -m32 -nostdlib "-Wl,--build-id=none" `
    $Objects -o "$BuildDir/kernel.elf"

i686-elf-objcopy -O binary "$BuildDir/kernel.elf" "$BuildDir/kernel.bin"

$kernel = [System.IO.File]::ReadAllBytes("$BuildDir/kernel.bin")
if ($kernel.Length -gt $KernelBytes) {
    throw "Kernel is $($kernel.Length) bytes, but bootloader loads only $KernelBytes bytes."
}

$boot = [System.IO.File]::ReadAllBytes("$BuildDir/boot.bin")
if ($boot.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes. Current size: $($boot.Length)"
}

$image = New-Object byte[] (512 + $KernelBytes)
[Array]::Copy($boot, 0, $image, 0, $boot.Length)
[Array]::Copy($kernel, 0, $image, 512, $kernel.Length)
[System.IO.File]::WriteAllBytes($ImagePath, $image)

Write-Host "Built $ImagePath"

if ($Run) {
    Require-Command "qemu-system-i386"
    qemu-system-i386 -drive format=raw,file="$ImagePath"
}
