![EXOS](doc/assets/EXOS.png)

## TL;DR

Sistema operativo multihilo para x86-32 y x86-64.<br>
Probado en QEMU, Bochs y ACER Predator.<br>
**NO APTO** para producción en lo referente a E/S de disco (no todas las rutas de código han sido probadas en hardware real).

## Descripción

Este proyecto es la continuación de un sistema operativo que fue abandonado a finales de 1999.<br>
En aquel momento era únicamente de 32 bits y se compilaba con gcc y nasm, enlazado con jloc.<br>
En el verano de 2025, el proyecto fue migrado a i686-elf-gcc/nasm/i686-elf-ld y posteriormente portado a x86-64.

## Aviso legal

EXOS se proporciona “tal cual”, sin garantías de ningún tipo. Ni los autores o contribuidores de EXOS, ni los autores o contribuidores del software de terceros incluido, serán responsables de ningún daño directo, indirecto, incidental, especial, ejemplar o consecuente derivado del uso de este proyecto.

## Compilación y ejecución en Debian

### Instalación de dependencias

./scripts/linux/setup/setup-deps.sh

./scripts/linux/setup/setup-qemu.sh		<- si deseas usar una versión reciente de QEMU (9.0.2)

### Compilación (imagen de disco con ext2)

./scripts/linux/build/build --arch <x86-32|x86-64> --fs ext2 --release (o --debug)

( añadir --clean para una compilación limpia )

### Compilación (imagen de disco con FAT32)

./scripts/linux/build/build --arch <x86-32|x86-64> --fs fat32 --release (o --debug)

( añadir --clean para una compilación limpia )

### Compilación para arranque UEFI

./scripts/linux/build/build --arch <x86-32|x86-64> --fs ext2 --release (o --debug) --uefi

( añadir --clean para una compilación limpia )

### Ejecución

./scripts/linux/run/run --arch <x86-32|x86-64>

( añadir --gdb para depurar con gdb )
( o `./scripts/linux/x86-32/start-bochs.sh` para usar Bochs en x86-32 )

## Funcionalidades actuales

- Multi-arquitectura : x86-32, x86-64
- Multihilo : cambio de contexto por software
- Gestión de memoria virtual (mapeo CPU y DMA) (buddy allocator para páginas físicas)
- Gestión del heap (listas libres)
- Creación de procesos (kernel y espacio de usuario), creación de tareas, planificación
- Seguridad a nivel de objetos del kernel, con cuentas/sesiones de usuario y permisos
- Enmascaramiento de punteros del kernel : handles en espacio de usuario
- Controladores de sistemas de archivos : FAT16, FAT32, EXT2, NTFS ~
- Controlador I/O APIC y Local APIC
- Controlador de dispositivos PCI
- Controladores de almacenamiento ATA, SATA/AHCI y NVMe
- Controlador xHCI (USB 3)
- Apagado/reinicio mediante ACPI
- Gestión de consola
- Controlador GOP (framebuffer UEFI)
- Controlador VGA
- Controlador VESA
- Controlador Intel Graphics (iGPU)
- Controladores de teclado y ratón PS/2
- Controladores de teclado USB (HID) y ratón
- Controlador de almacenamiento USB ~
- Sistema de archivos virtual con puntos de montaje
- Shell con scripting integrado y exposición de objetos del kernel
- Configuración en formato TOML
- Controlador E1000 ~
- Controladores Realtek RTL8139 y RTL8111/8168/8411 ~
- Capas de red ARP/IPv4/DHCP/UDP/TCP ~
- Cliente HTTP mínimo ~
- Sistema de ventanas (WIP)
- Algunas aplicaciones de prueba

(~ indica que funciona en emulador - QEMU, pero no está probado o aún no funciona en hardware real)

## Funcionalidades previstas

- IPC (memoria compartida mediante mapeo de páginas)
- Multinúcleo (SMP)
- Seguridad completa
- Pila de red completa
- Soporte completo de Unicode
- Controlador PCIe
- VMD (Volume Management Device - Intel)
- Compilador C nativo (port de TinyCC)
- Audio HDA (Intel HD Audio)
- Controlador NVIDIA GeForce
- Controlador AMD Radeon
- Aplicaciones de prueba más avanzadas
- Controladores de sensores de energía ACPI

## Más adelante

- Más arquitecturas
- Más controladores

## Arquitectura

Ver doc/guides/Kernel.md para detalles de la arquitectura

## Dependencias

### Lenguaje C (sin headers)

### bcrypt
Utilizado para el hash de contraseñas. Fuentes en third/bcrypt (licencia Apache 2.0, ver third/bcrypt/README y third/bcrypt/LICENSE).<br>
Archivos compilados en el kernel: bcrypt.c, blowfish.c.<br>
bcrypt es copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

### BearSSL
Utilizado para el hash SHA-256 en utilidades criptográficas del kernel. Fuentes en `third/bearssl` (licencia MIT, ver `third/bearssl/LICENSE.txt` y `third/bearssl/README.txt`).<br>
Fuentes SHA-256 integradas: `third/bearssl/src/hash/sha2small.c`, `third/bearssl/src/codec/dec32be.c`, `third/bearssl/src/codec/enc32be.c`.<br>
BearSSL es copyright (c) 2016 Thomas Pornin <pornin@bolet.org>.

### miniz
Utilizado para compresión DEFLATE/zlib en utilidades de compresión del kernel. Fuentes en `third/miniz` (licencia MIT, ver `third/miniz/LICENSE` y `third/miniz/readme.md`).<br>
Fuente backend integrada en el kernel: `third/miniz/miniz.c`.<br>
miniz es copyright (c) Rich Geldreich, RAD Game Tools y Valve Software.

### Monocypher
Utilizado para la verificación de firmas separadas (Ed25519) en utilidades de firma del kernel. Fuentes en `third/monocypher` (BSD-2-Clause o CC0-1.0, ver `third/monocypher/LICENCE.md` y `third/monocypher/README.md`).<br>
Fuentes backend de firma integradas: `third/monocypher/src/monocypher.c` y `third/monocypher/src/optional/monocypher-ed25519.c`.<br>
Para compatibilidad freestanding del kernel, Argon2 de Monocypher está deshabilitado en builds x86-32.<br>
Monocypher es copyright (c) 2017-2019 Loup Vaillant.

### utf8-hoehrmann
Utilizado para la decodificación UTF-8 en el parsing de layouts. Fuentes en third/utf8-hoehrmann (licencia MIT, ver headers).

### Fonts
Bm437_IBM_VGA_8x16.otb del Ultimate Oldschool PC Font Pack por VileR, bajo licencia CC BY-SA 4.0. Ver third/fonts/oldschool_pc_font_pack/ATTRIBUTION.txt y third/fonts/oldschool_pc_font_pack/LICENSE.TXT.

## Métricas (cloc)

### Líneas de código en este proyecto, excluyendo software de terceros.

```
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                              346          33456          34958         113959
C/C++ Header                   245           6304           6884          15783
Assembly                        20           1981           1264           6746
-------------------------------------------------------------------------------
SUM:                           611          41741          43106         136488
-------------------------------------------------------------------------------
```

### Tamaño del kernel

- 32 bits : 1.4 mb
- 64 bits : 1.8 mb

## Contexto histórico

En 1999, comencé EXOS como un experimento sencillo: quería escribir un bootloader mínimo por curiosidad.  
Muy pronto me di cuenta de que estaba construyendo mucho más que un bootloader. Empecé a reimplementar encabezados completos del sistema, inspirándome en Windows y en referencias de bajo nivel de DOS/BIOS, con el objetivo de crear un sistema operativo completo de 32 bits desde cero.
Fue un proyecto en solitario de un año, desarrollado en condiciones difíciles:
- En un Pentium, en entorno DOS, sin depurador ni máquina virtual
- Basándome en innumerables impresiones por consola para rastrear errores
- Aprendiendo todo sobre la marcha a medida que el proyecto crecía

El estilo de código de EXOS se asemeja al de Windows, con nombres en PascalCase, funciones de usuario, etc. A algunos les gustará, a otros no. Pero **no es** Windows. Es más compacto y no recopilará ni transmitirá datos de usuario. Nunca. (Salvo, eventualmente, volcados de fallo para depuración.)
