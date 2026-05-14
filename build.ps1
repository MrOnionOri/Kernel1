param(
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$BuildDir = "build"
$KernelSectors = 192
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

Add-Ascii "K1RD2"
$initrdBytesList.Add(0)
$helloPayload = [System.IO.File]::ReadAllBytes("$BuildDir/hello.payload")
$echoPayload = [System.IO.File]::ReadAllBytes("$BuildDir/echo.payload")
$loggerPayload = [System.IO.File]::ReadAllBytes("$BuildDir/logger.payload")
$helloKapp = New-Object System.Collections.Generic.List[byte]
$helloKapp.AddRange([byte[]][System.Text.Encoding]::ASCII.GetBytes("KAPP"))
foreach ($value in @(20, 0, $helloPayload.Length, 0)) {
    $helloKapp.Add([byte]($value -band 0xFF))
    $helloKapp.Add([byte](($value -shr 8) -band 0xFF))
    $helloKapp.Add([byte](($value -shr 16) -band 0xFF))
    $helloKapp.Add([byte](($value -shr 24) -band 0xFF))
}
$helloKapp.AddRange($helloPayload)
$echoKapp = New-Object System.Collections.Generic.List[byte]
$echoKapp.AddRange([byte[]][System.Text.Encoding]::ASCII.GetBytes("KAPP"))
foreach ($value in @(20, 0, $echoPayload.Length, 0)) {
    $echoKapp.Add([byte]($value -band 0xFF))
    $echoKapp.Add([byte](($value -shr 8) -band 0xFF))
    $echoKapp.Add([byte](($value -shr 16) -band 0xFF))
    $echoKapp.Add([byte](($value -shr 24) -band 0xFF))
}
$echoKapp.AddRange($echoPayload)
$loggerKapp = New-Object System.Collections.Generic.List[byte]
$loggerKapp.AddRange([byte[]][System.Text.Encoding]::ASCII.GetBytes("KAPP"))
foreach ($value in @(20, 0, $loggerPayload.Length, 0)) {
    $loggerKapp.Add([byte]($value -band 0xFF))
    $loggerKapp.Add([byte](($value -shr 8) -band 0xFF))
    $loggerKapp.Add([byte](($value -shr 16) -band 0xFF))
    $loggerKapp.Add([byte](($value -shr 24) -band 0xFF))
}
$loggerKapp.AddRange($loggerPayload)
Add-Record "apps/demo.txt" ([System.Text.Encoding]::ASCII.GetBytes("demo is currently linked into the kernel image.`nNext: load this app from initrd.`n"))
Add-Record "apps/clock.txt" ([System.Text.Encoding]::ASCII.GetBytes("clock is currently linked into the kernel image.`nNext: load this app from initrd.`n"))
Add-Record "apps/reader.txt" ([System.Text.Encoding]::ASCII.GetBytes("reader opens files through SYS_OPEN/SYS_READ/SYS_CLOSE.`n"))
Add-Record "apps/manifest.txt" ([System.Text.Encoding]::ASCII.GetBytes("demo|built-in|Demo ring3 app`nclock|built-in|Shows PID and ticks`nreader|built-in|Reads files through VFS syscalls`nhello|kapp|Hello from initrd`necho|kapp|Prints arguments`nlogger|kapp|Appends args to tmp/app.log`n"))
Add-Record "apps/hello.kapp" ([byte[]]$helloKapp.ToArray())
Add-Record "apps/echo.kapp" ([byte[]]$echoKapp.ToArray())
Add-Record "apps/logger.kapp" ([byte[]]$loggerKapp.ToArray())
Add-Record "readme.txt" ([System.Text.Encoding]::ASCII.GetBytes("Kernel1 initrd v2: name + u32 size + binary-safe data records.`n"))
Add-Record "docs/kapp.txt" ([System.Text.Encoding]::ASCII.GetBytes("KAPP v0: magic KAPP, u32 header size, u32 entry offset, u32 image size, u32 flags, payload.`n"))
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
    qemu-system-i386 -drive if=ide,index=0,format=raw,file="$ImagePath" -drive if=ide,index=1,format=raw,file="$DataImagePath"
}
