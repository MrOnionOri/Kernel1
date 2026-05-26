param(
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$BuildDir = "build"
$KernelSectors = 292
$KernelBytes = $KernelSectors * 512
$InitrdSectors = 16
$InitrdBytes = $InitrdSectors * 512
$ImagePath = Join-Path $BuildDir "kernel1.img"
$DataImagePath = Join-Path $BuildDir "data.img"
$DataImageBytes = 1024 * 1024

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
nasm -f bin apps/hello.asm -o "$BuildDir/hello.payload"
nasm -f bin apps/echo.asm -o "$BuildDir/echo.payload"
nasm -f bin apps/logger.asm -o "$BuildDir/logger.payload"

$initrdBytesList = New-Object System.Collections.Generic.List[byte]
function Add-Ascii($Text) {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text)
    $initrdBytesList.AddRange([byte[]]$bytes)
}
function Add-U32($Value) {
    $initrdBytesList.Add([byte]($Value -band 0xFF))
    $initrdBytesList.Add([byte](($Value -shr 8) -band 0xFF))
    $initrdBytesList.Add([byte](($Value -shr 16) -band 0xFF))
    $initrdBytesList.Add([byte](($Value -shr 24) -band 0xFF))
}
function Add-Record($Name, [byte[]]$Data) {
    Add-Ascii $Name
    $initrdBytesList.Add(0)
    Add-U32 $Data.Length
    $initrdBytesList.AddRange($Data)
}
function Add-U32-ToList($List, [int]$Value) {
    $List.Add([byte]($Value -band 0xFF))
    $List.Add([byte](($Value -shr 8) -band 0xFF))
    $List.Add([byte](($Value -shr 16) -band 0xFF))
    $List.Add([byte](($Value -shr 24) -band 0xFF))
}
function New-Kapp([string]$Name, [byte[]]$Payload, [int]$RequiredSyscalls) {
    $kapp = New-Object System.Collections.Generic.List[byte]
    $kapp.AddRange([byte[]][System.Text.Encoding]::ASCII.GetBytes("KAPP"))
    foreach ($value in @(64, 0, $Payload.Length, 0, 1, $RequiredSyscalls)) {
        Add-U32-ToList $kapp $value
    }

    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($Name)
    for ($i = 0; $i -lt 32; $i++) {
        if ($i -lt $nameBytes.Length) {
            $kapp.Add($nameBytes[$i])
        } else {
            $kapp.Add(0)
        }
    }

    Add-U32-ToList $kapp 0
    $kapp.AddRange($Payload)
    return [byte[]]$kapp.ToArray()
}

Add-Ascii "K1RD2"
$initrdBytesList.Add(0)
$helloPayload = [System.IO.File]::ReadAllBytes("$BuildDir/hello.payload")
$echoPayload = [System.IO.File]::ReadAllBytes("$BuildDir/echo.payload")
$loggerPayload = [System.IO.File]::ReadAllBytes("$BuildDir/logger.payload")
$helloKapp = New-Kapp "hello" $helloPayload ((1 -shl 2) -bor (1 -shl 7))
$echoKapp = New-Kapp "echo" $echoPayload ((1 -shl 2) -bor (1 -shl 7) -bor (1 -shl 11))
$loggerKapp = New-Kapp "logger" $loggerPayload ((1 -shl 2) -bor (1 -shl 7) -bor (1 -shl 10) -bor (1 -shl 11) -bor (1 -shl 14) -bor (1 -shl 15) -bor (1 -shl 16))
Add-Record "apps/demo.txt" ([System.Text.Encoding]::ASCII.GetBytes("demo is currently linked into the kernel image.`nNext: load this app from initrd.`n"))
Add-Record "apps/busy.txt" ([System.Text.Encoding]::ASCII.GetBytes("busy is a built-in no-yield scheduler stress app.`n"))
Add-Record "apps/clock.txt" ([System.Text.Encoding]::ASCII.GetBytes("clock is currently linked into the kernel image.`nNext: load this app from initrd.`n"))
Add-Record "apps/probe.txt" ([System.Text.Encoding]::ASCII.GetBytes("probe intentionally touches unmapped user memory to test isolation.`n"))
Add-Record "apps/reader.txt" ([System.Text.Encoding]::ASCII.GetBytes("reader opens files through SYS_OPEN/SYS_READ/SYS_CLOSE.`n"))
Add-Record "apps/selfmod.txt" ([System.Text.Encoding]::ASCII.GetBytes("selfmod intentionally writes built-in code to test read-only user pages.`n"))
Add-Record "apps/sleeper.txt" ([System.Text.Encoding]::ASCII.GetBytes("sleeper exercises SYS_SLEEP without blocking the shell.`n"))
Add-Record "apps/lsapp.txt" ([System.Text.Encoding]::ASCII.GetBytes("lsapp lists directories through SYS_STAT/SYS_READDIR.`n"))
Add-Record "apps/launcher.txt" ([System.Text.Encoding]::ASCII.GetBytes("launcher uses SYS_EXEC/SYS_WAIT to run another app.`n"))
Add-Record "apps/manifest.txt" ([System.Text.Encoding]::ASCII.GetBytes("demo|built-in|Demo ring3 app with SYS_YIELD`nbusy|built-in|No-yield scheduler stress app`nclock|built-in|Shows PID and ticks`nprobe|built-in|Faults on unmapped user memory`nreader|built-in|Reads files through VFS syscalls`nselfmod|built-in|Faults on read-only user code`nsleeper|built-in|Sleeps through SYS_SLEEP and resumes later`nlsapp|built-in|Lists directories through VFS syscalls`nlauncher|built-in|Execs and waits for another app`nhello|kapp|Hello from initrd`necho|kapp|Prints arguments`nlogger|kapp|Appends args to a log file`n"))
Add-Record "apps/hello.kapp" $helloKapp
Add-Record "apps/echo.kapp" $echoKapp
Add-Record "apps/logger.kapp" $loggerKapp
Add-Record "readme.txt" ([System.Text.Encoding]::ASCII.GetBytes("Kernel1 initrd v2: name + u32 size + binary-safe data records.`n"))
Add-Record "docs/kapp.txt" ([System.Text.Encoding]::ASCII.GetBytes("KAPP v1: magic KAPP, u32 header size, entry offset, image size, flags, version, required syscall mask, 32-byte app name, reserved, payload. KAPP v0 headers are still accepted.`n"))
$initrdBytesList.Add(0)
$initrd = [byte[]]$initrdBytesList.ToArray()
if ($initrd.Length -gt $InitrdBytes) {
    throw "Initrd is $($initrd.Length) bytes, but reserved space is $InitrdBytes bytes."
}

$kernel = [System.IO.File]::ReadAllBytes("$BuildDir/kernel.bin")
if ($kernel.Length -gt $KernelBytes) {
    throw "Kernel is $($kernel.Length) bytes, but bootloader loads only $KernelBytes bytes."
}

$boot = [System.IO.File]::ReadAllBytes("$BuildDir/boot.bin")
if ($boot.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes. Current size: $($boot.Length)"
}

$image = New-Object byte[] (512 + $KernelBytes + $InitrdBytes)
[Array]::Copy($boot, 0, $image, 0, $boot.Length)
[Array]::Copy($kernel, 0, $image, 512, $kernel.Length)
[Array]::Copy($initrd, 0, $image, 512 + $KernelBytes, $initrd.Length)
[System.IO.File]::WriteAllBytes($ImagePath, $image)

Write-Host "Built $ImagePath"

if (-not (Test-Path $DataImagePath)) {
    $dataImage = New-Object byte[] $DataImageBytes
    [System.IO.File]::WriteAllBytes($DataImagePath, $dataImage)
    Write-Host "Created $DataImagePath"
}

if ($Run) {
    Require-Command "qemu-system-i386"
    qemu-system-i386 -vga std -drive if=ide,index=0,format=raw,file="$ImagePath" -drive if=ide,index=1,format=raw,file="$DataImagePath"
}
