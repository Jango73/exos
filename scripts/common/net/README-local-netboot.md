# Local PXE + iPXE Netboot

This setup uses one Linux machine as build host and netboot server.

## 1) Configure local netboot services

Run as root:

```bash
sudo scripts/linux/net/setup-local-netboot.sh --arch x86-64
```

The script:
- detects the active IPv4 interface,
- derives a DHCP range from the current subnet,
- writes a dedicated dnsmasq config,
- installs an iPXE EFI binary into TFTP root,
- writes `boot.ipxe` and prepares `latest/previous/safe` folders under HTTP root.

Use `--dry-run` to preview detected values.

Optional fixed DHCP lease for Predator:

```bash
sudo scripts/linux/net/setup-local-netboot.sh \
    --arch x86-64 \
    --predator-mac 98:28:A6:27:89:C8 \
    --predator-ip 192.168.88.120
```

## 2) Build EXOS UEFI image contents

```bash
./scripts/linux/build/build --arch x86-64 --fs ext2 --debug --split --uefi
```

## 3) Deploy boot files to HTTP slot

```bash
sudo scripts/linux/net/deploy-local-netboot.sh --arch x86-64 --slot latest --rotate
```

Default source is `build/image/<build-image-name>/work-uefi/esp-root`.

## 4) Configure Predator boot order

Set network boot first (UEFI PXE).
The iPXE menu chains:
- `latest`
- `previous`
- `safe`
