## TL;DR

x86-32 および x86-64 向けのマルチスレッド対応オペレーティングシステム。<br>
QEMU、Bochs、ACER Predator でテスト済み。<br>
ディスク I/O に関しては、実機で未検証のコードパスがあるため、**本番用途には未対応**。

## 概要

本プロジェクトは、1999 年後半に一度中断されたオペレーティングシステム開発の継続版です。<br>
当時は 32 ビット専用で、gcc と nasm によりビルドされ、jloc でリンクされていました。<br>
2025 年夏に i686-elf-gcc / nasm / i686-elf-ld へ移行し、その後 x86-64 に対応しました。

## 免責事項

EXOS は現状有姿のまま提供され、いかなる保証も行いません。EXOS の作者・貢献者、および同梱されているサードパーティソフトウェアの作者・貢献者は、本プロジェクトの利用により発生した直接的、間接的、付随的、特別、結果的、または懲罰的損害について、一切の責任を負いません。

## Debian でのビルドと実行

### 依存関係のセットアップ

./scripts/linux/setup/setup-deps.sh

./scripts/linux/setup/setup-qemu.sh		<- 新しい QEMU (9.0.2) を使用する場合

### ビルド（ext2 ディスクイメージ）

./scripts/linux/build/build --arch <x86-32|x86-64> --fs ext2 --release（または --debug）

（クリーンビルドには --clean を追加）

### ビルド（FAT32 ディスクイメージ）

./scripts/linux/build/build --arch <x86-32|x86-64> --fs fat32 --release（または --debug）

（クリーンビルドには --clean を追加）

### UEFI ブート用ビルド

./scripts/linux/build/build --arch <x86-32|x86-64> --fs ext2 --release（または --debug） --uefi

（クリーンビルドには --clean を追加）

### 実行

./scripts/linux/run/run --arch <x86-32|x86-64>

（gdb でデバッグする場合は --gdb を追加）
（または `./scripts/linux/x86-32/start-bochs.sh` で x86-32 の Bochs を使用）

## 現在の機能

- マルチアーキテクチャ : x86-32, x86-64
- マルチスレッド : ソフトウェアによるコンテキストスイッチ
- 仮想メモリ管理（CPU および DMA マッピング）（物理ページは buddy allocator を使用）
- ヒープ管理（フリーリスト）
- プロセス生成（カーネル／ユーザー空間）、タスク生成、スケジューリング
- カーネルオブジェクト単位のセキュリティ（ユーザーアカウント／セッション／権限）
- カーネルポインタのマスキング（ユーザー空間ではハンドルとして公開）
- ファイルシステムドライバ : FAT16, FAT32, EXT2, NTFS ~
- I/O APIC および Local APIC ドライバ
- PCI デバイスドライバ
- ATA、SATA/AHCI、NVMe ストレージドライバ
- xHCI ドライバ（USB 3）
- ACPI によるシャットダウン／再起動
- コンソール管理
- GOP（UEFI フレームバッファ）ドライバ
- VGA ドライバ
- VESA ドライバ
- Intel Graphics（iGPU）ドライバ
- PS/2 キーボードおよびマウスドライバ
- USB キーボード（HID）およびマウスドライバ
- USB マスストレージドライバ ~
- マウントポイント対応の仮想ファイルシステム
- スクリプト機能を備えたシェル（カーネルオブジェクトを公開）
- TOML 形式による設定
- E1000 ドライバ ~
- Realtek RTL8139 / RTL8111 / 8168 / 8411 ドライバ ~
- ARP / IPv4 / DHCP / UDP / TCP ネットワークレイヤ ~
- 最小構成の HTTP クライアント ~
- ウィンドウシステム（開発中）
- テスト用アプリケーション

（~ はエミュレータ（QEMU）では動作するが、実機では未検証または未対応）

## 今後の予定

- IPC（ページマッピングによる共有メモリ）
- マルチコア対応（SMP）
- セキュリティの強化
- 完全なネットワークスタック
- Unicode 完全対応
- PCIe ドライバ
- VMD（Volume Management Device - Intel）
- ネイティブ C コンパイラ（TinyCC の移植）
- HDA オーディオ（Intel HD Audio）
- NVIDIA GeForce ドライバ
- AMD Radeon ドライバ
- より高度なテストアプリケーション
- ACPI エネルギーセンサードライバ

## 今後の展開

- 対応アーキテクチャの拡張
- ドライバの追加

## アーキテクチャ

詳細は doc/guides/Kernel.md を参照

## 依存関係

### C 言語（ヘッダなし）

### bcrypt
パスワードのハッシュ化に使用。ソースは third/bcrypt（Apache 2.0 ライセンス、third/bcrypt/README および third/bcrypt/LICENSE を参照）。<br>
カーネルに組み込まれるファイル : bcrypt.c, blowfish.c。<br>
bcrypt は copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

### BearSSL
カーネルの暗号ユーティリティにおける SHA-256 ハッシュに使用。ソースは `third/bearssl`（MIT ライセンス、`third/bearssl/LICENSE.txt` および `third/bearssl/README.txt` を参照）。<br>
統合されている SHA-256 ソース : `third/bearssl/src/hash/sha2small.c`, `third/bearssl/src/codec/dec32be.c`, `third/bearssl/src/codec/enc32be.c`。<br>
BearSSL は copyright (c) 2016 Thomas Pornin <pornin@bolet.org>。

### miniz
カーネルの圧縮ユーティリティにおける DEFLATE/zlib 圧縮に使用。ソースは `third/miniz`（MIT ライセンス、`third/miniz/LICENSE` および `third/miniz/readme.md` を参照）。<br>
カーネルに統合されたバックエンドソース : `third/miniz/miniz.c`。<br>
miniz は copyright (c) Rich Geldreich, RAD Game Tools, および Valve Software。

### Monocypher
カーネルの署名ユーティリティにおける分離署名（Ed25519）の検証に使用。ソースは `third/monocypher`（BSD-2-Clause または CC0-1.0、`third/monocypher/LICENCE.md` および `third/monocypher/README.md` を参照）。<br>
統合された署名バックエンドソース : `third/monocypher/src/monocypher.c` および `third/monocypher/src/optional/monocypher-ed25519.c`。<br>
カーネルの freestanding 環境との互換性のため、x86-32 ビルドでは Monocypher の Argon2 は無効化されています。<br>
Monocypher は copyright (c) 2017-2019 Loup Vaillant。

### utf8-hoehrmann
レイアウト解析時の UTF-8 デコードに使用。ソースは third/utf8-hoehrmann（MIT ライセンス、ヘッダ参照）。

### フォント
Bm437_IBM_VGA_8x16.otb は VileR による Ultimate Oldschool PC Font Pack に含まれ、CC BY-SA 4.0 ライセンスで提供されています。詳細は third/fonts/oldschool_pc_font_pack/ATTRIBUTION.txt および third/fonts/oldschool_pc_font_pack/LICENSE.TXT を参照。

## 統計（cloc）

### サードパーティを除く本プロジェクトのコード行数

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

### カーネルサイズ

- 32 ビット : 1.4 mb
- 64 ビット : 1.8 mb

## 背景

1999 年、私は EXOS を単なる実験として開始しました。最小限の OS ブートローダを書いてみたかったのがきっかけです。  
しかしすぐに、それが単なるブートローダではないことに気づきました。Windows や DOS/BIOS の低レベル資料を参考にしながら、システムヘッダを一から再実装し、完全な 32 ビット OS を構築することを目指すようになりました。
このプロジェクトは 1 年間にわたる個人開発であり、環境も厳しいものでした：
- Pentium 上の DOS 環境で、デバッガや仮想マシンなし
- バグ追跡は大量のコンソール出力に依存
- プロジェクトの進行とともに必要な知識を逐次習得

EXOS のコーディングスタイルは、PascalCase の命名やユーザー関数名など、Windows に近い部分があります。好みは分かれるでしょう。ただし、これは **Windows ではありません**。よりコンパクトであり、ユーザーデータを収集・送信することは一切ありません。決して。 （デバッグ目的のクラッシュダンプを除く可能性を除いて）
