# Kernel1

Kernel educativo desde cero con bootloader en ASM y kernel en C.

## Requisitos en WSL/Linux

- `nasm`
- `gcc`
- `ld`
- `objcopy`
- `qemu-system-i386` para ejecutarlo

En Ubuntu/WSL:

```bash
sudo apt update
sudo apt install build-essential nasm qemu-system-x86
```

## Requisitos en Windows

- `nasm`
- `i686-elf-gcc`
- `i686-elf-objcopy`
- `qemu-system-i386`

En Windows, instala las herramientas y asegurate de que esten disponibles en `PATH`.

## Compilar

En WSL/Linux:

```bash
./build.sh
```

En Windows PowerShell:

```powershell
.\build.ps1
```

La imagen booteable queda en:

```text
build/kernel1.img
```

## Ejecutar

En WSL/Linux:

```bash
./build.sh run
```

En Windows PowerShell:

```powershell
.\build.ps1 -Run
```

O con Make:

```bash
make run
```

## Estructura

- `boot/boot.asm`: sector de arranque BIOS de 512 bytes. Carga el kernel y entra en modo protegido de 32 bits.
- `kernel/core`: logica portable del kernel: shell, syscalls, tareas y flujo principal.
- `kernel/mm`: PMM, heap y mapa de memoria.
- `kernel/drivers`: terminal VGA, teclado PS/2 y timer PIT.
- `kernel/arch/x86`: entrada ASM, GDT/TSS, IDT/ISR, PIC, paging, I/O ports y salto a ring 3.
- `kernel/lib`: utilidades de bajo nivel compartidas.
- `user/demo`: programa de usuario enlazado para probar ring 3 y syscalls.
- `docs/ARCHITECTURE.md`: notas de arquitectura multi-plataforma.
- `linker.ld`: coloca el kernel en `0x10000`, que es donde lo carga el bootloader.
- `build.sh`: script de compilacion para WSL/Linux.
- `build.ps1`: script de compilacion y generacion de imagen.

## Initrd y apps KAPP

El bootloader carga una region reservada para el kernel y despues un initrd en
memoria. El initrd actual usa formato `K1RD2`:

```text
K1RD2\0
name\0
u32 size
data bytes
...
\0
```

Esto permite guardar archivos binarios. Los `.kapp` son el formato planeado
para apps cargables desde initrd. Puedes inspeccionar uno con:

```text
kapp apps/hello.kapp
```

## ABI de syscalls

Las apps de usuario usan `int 0x80`.

```text
eax = numero de syscall
ebx/ecx/edx = argumentos
eax = valor de retorno
```

Syscalls actuales:

```text
1 SYS_WRITE      ebx=string_c                 legado/debug
2 SYS_EXIT       ebx=exit_code                no retorna a usuario
3 SYS_YIELD      reservado                    pausado por estabilidad
4 SYS_WRITE_DEC  ebx=value                    imprime decimal/debug
5 SYS_GETPID                                  retorna pid en eax
6 SYS_TICKS                                   retorna ticks PIT en eax
7 SYS_WRITE_BUF  ebx=fd ecx=buffer edx=len    retorna bytes escritos o -1
8 SYS_OPEN       ebx=path                     retorna fd o -1
9 SYS_READ       ebx=fd ecx=buffer edx=len    retorna bytes leidos o -1
10 SYS_CLOSE     ebx=fd                       retorna 0 o -1
11 SYS_GETARGS   ebx=buffer ecx=len           retorna bytes copiados
```

Por ahora `SYS_WRITE_BUF` soporta `fd=1` para stdout.

Las syscalls validan punteros de usuario contra la imagen `.user` y el rango
reservado de stacks de usuario antes de leer o escribir buffers.

## Proximos pasos

1. Loader de apps/initrd para sacar programas de usuario del binario del kernel.
2. Modelo de contexto de proceso mas completo.
3. Scheduler cooperativo/preemptivo sobre ese contexto.
4. Drivers de disco y sistema de archivos.
