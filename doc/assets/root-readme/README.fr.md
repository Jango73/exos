## TL;DR

Système d'exploitation multi-tâches pour x86-32 et x86-64.<br>
Testé sur QEMU, Bochs, ACER Predator.<br>
**NON PRÊT** pour la production concernant les E/S disque (tous les chemins de code n'ont pas été testés sur matériel réel).

## Description

Il s'agit d'un projet de système d'exploitation en cours, qui a été abandonné fin 1999.<br>
À l'époque, il était uniquement en 32 bits et compilé avec gcc et nasm, puis lié avec jloc.<br>
À l'été 2025, j'ai porté le projet vers i686-elf-gcc/nasm/i686-elf-ld, puis vers x86-64.

## Clause de non-responsabilité

EXOS est fourni "tel quel", sans garantie d'aucune sorte. Ni les auteurs/contributeurs d'EXOS, ni les auteurs/contributeurs des logiciels tiers inclus, ne peuvent être tenus responsables de tout dommage direct, indirect, accessoire, spécial, exemplaire ou consécutif résultant de l'utilisation de ce projet.

## Compilation & exécution sous Debian

### Installation des dépendances

./scripts/linux/setup/setup-deps.sh

./scripts/linux/setup/setup-qemu.sh		<- si vous voulez un QEMU récent (9.0.2)

### Compilation (image disque avec ext2)

./scripts/linux/build/build --arch <x86-32|x86-64> --fs ext2 --release (ou --debug)

( ajouter --clean pour une compilation propre )

### Compilation (image disque avec FAT32)

./scripts/linux/build/build --arch <x86-32|x86-64> --fs fat32 --release (ou --debug)

( ajouter --clean pour une compilation propre )

### Compilation pour démarrage UEFI

./scripts/linux/build/build --arch <x86-32|x86-64> --fs ext2 --release (ou --debug) --uefi

( ajouter --clean pour une compilation propre )

### Exécution

./scripts/linux/run/run --arch <x86-32|x86-64>

( ajouter --gdb pour déboguer avec gdb )
( ou `./scripts/linux/x86-32/start-bochs.sh` pour utiliser Bochs en x86-32 )

## Fonctionnalités actuelles

- Multi-architecture : x86-32, x86-64
- Multi-tâches
- Gestion de la mémoire virtuelle (mappage CPU & DMA) (buddy allocator pour les pages physiques)
- Gestion du tas (listes libres)
- Création de processus (kernel et espace utilisateur), création de tâches, ordonnancement
- Sécurité au niveau des objets du noyau, avec comptes/sessions utilisateur et permissions
- Masquage des pointeurs noyau : handles en espace utilisateur
- Pilotes de systèmes de fichiers : FAT16, FAT32, EXT2, NTFS ~
- Pilote I/O APIC & Local APIC
- Pilote de périphériques PCI
- Pilotes de stockage ATA, SATA/AHCI & NVMe
- Pilote xHCI (USB 3)
- Extinction/redémarrage ACPI
- Gestion de la console
- Pilote GOP (framebuffer UEFI)
- Pilote VGA
- Pilote VESA
- Pilote Intel Graphics (iGPU)
- Pilotes clavier et souris PS/2
- Pilotes clavier USB (HID) et souris
- Pilote de périphérique de stockage USB ~
- Système de fichiers virtuel avec points de montage
- Shell avec scripting intégré et exposition des objets noyau
- Configuration au format TOML
- Pilote E1000 ~
- Pilotes Realtek RTL8139 & RTL8111/8168/8411 ~
- Couches réseau ARP/IPv4/DHCP/UDP/TCP ~
- Client HTTP minimal ~
- Système de bureau/fenêtrage (en cours)
- Quelques applications de test

(~ signifie fonctionnel en émulateur - QEMU, mais non testé ou pas encore fonctionnel sur matériel réel)

## Fonctionnalités prévues

- IPC (mémoire partagée via mappage de pages)
- Multi-cœur (SMP)
- Sécurité complète
- Stack réseau complet
- Unicode complet
- Pilote PCIe
- VMD (Volume Management Device - Intel)
- Compilateur C natif (port de TinyCC)
- Audio HDA (Intel HD Audio)
- Pilote NVIDIA GeForce
- Pilote AMD Radeon
- Applications de test plus avancées
- Pilotes capteurs d'énergie ACPI

## Perspectives

- Plus d'architectures (ARM64, RISC-V)
- Plus de pilotes

## Architecture

Voir doc/guides/Kernel.md pour les détails de l'architecture

## Dépendances

### Langage C (sans headers)

### bcrypt
Utilisé pour le hachage des mots de passe. Sources dans third/bcrypt (sous licence Apache 2.0, voir third/bcrypt/README et third/bcrypt/LICENSE).<br>
Fichiers compilés dans le noyau : bcrypt.c, blowfish.c.<br>
bcrypt est copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

### BearSSL
Utilisé pour le hachage SHA-256 dans les utilitaires cryptographiques du noyau. Sources dans `third/bearssl` (licence MIT, voir `third/bearssl/LICENSE.txt` et `third/bearssl/README.txt`).<br>
Sources SHA-256 intégrées : `third/bearssl/src/hash/sha2small.c`, `third/bearssl/src/codec/dec32be.c`, `third/bearssl/src/codec/enc32be.c`.<br>
BearSSL est copyright (c) 2016 Thomas Pornin <pornin@bolet.org>.

### miniz
Utilisé pour la compression DEFLATE/zlib dans les utilitaires de compression du noyau. Sources dans `third/miniz` (licence MIT, voir `third/miniz/LICENSE` et `third/miniz/readme.md`).<br>
Source backend noyau intégrée : `third/miniz/miniz.c`.<br>
miniz est copyright (c) Rich Geldreich, RAD Game Tools, et Valve Software.

### Monocypher
Utilisé pour la vérification de signatures détachées (Ed25519) dans les utilitaires de signature du noyau. Sources dans `third/monocypher` (BSD-2-Clause OU CC0-1.0, voir `third/monocypher/LICENCE.md` et `third/monocypher/README.md`).<br>
Sources backend de signature intégrées : `third/monocypher/src/monocypher.c` et `third/monocypher/src/optional/monocypher-ed25519.c`.<br>
Pour la compatibilité freestanding du noyau, Argon2 de Monocypher est désactivé dans les builds x86-32.<br>
Monocypher est copyright (c) 2017-2019 Loup Vaillant.

### utf8-hoehrmann
Utilisé pour le décodage UTF-8 lors du parsing des layouts. Sources dans third/utf8-hoehrmann (licence MIT, voir les headers).

### Fonts
Bm437_IBM_VGA_8x16.otb provenant du Ultimate Oldschool PC Font Pack par VileR, sous licence CC BY-SA 4.0. Voir third/fonts/oldschool_pc_font_pack/ATTRIBUTION.txt et third/fonts/oldschool_pc_font_pack/LICENSE.TXT.

## Statistiques (cloc)

### Nombre de lignes de code dans ce projet, hors logiciels tiers.

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

### Taille du noyau

- 32 bits : 1.4 mb
- 64 bits : 1.8 mb

## Contexte historique

En 1999, j'ai commencé EXOS comme une simple expérimentation : je voulais écrire un bootloader minimal pour le plaisir.
Très rapidement, j'ai réalisé que je programmais bien plus qu'un simple bootloader. J'ai réimplémenté des headers système complets, en m'inspirant de Windows et de références bas niveau DOS/BIOS, et un peu de Linux (que je connaissais très mal à l'époque), avec pour objectif de créer un OS 32 bits complet à partir de zéro.
Ce fut un projet solo d'un an, développé à la dur :
- Sur un Pentium, dans un environnement DOS, sans debugger ni machine virtuelle
- En s'appuyant sur d'innombrables affichages console pour tracer les bugs
- En apprenant tout au fur et à mesure de la progression du projet

Le style de code d'EXOS ressemble à celui de Windows, comme le nommage en PascalCase, les noms de fonctions utilisateur, etc... Certains apprécieront, d'autres non. Mais ce n'est **pas** Windows. Il est plus compact et ne collectera ni ne transmettra jamais de données utilisateur. Jamais. (Sauf éventuellement des extraits de journalisation de plantages pour le debug.)
