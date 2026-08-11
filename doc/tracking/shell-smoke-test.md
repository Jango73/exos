# Shell Smoke Test Tracking

Goal: build one dedicated shell smoke test plan that covers every built-in command, every supported option or subcommand, and at least one field for every scripting-exposed object shape.

Conventions:
- Each item starts unchecked and will be updated as blocks are implemented and validated.
- Command entries use `primary / alias` naming when the shell exposes both names in the command table.
- For scripting exposure, the objective is at least one representative field per exposed object shape, including nested arrays or sub-objects when they have their own descriptor.
- Automated coverage runs through `scripts/linux/test/smoke-test-shell.sh` (command list: `scripts/common/smoke-test-shell-commands.txt`).

## Block 1 - Core Commands

- [x] `commands / help`.
- [x] `clear / cls`.
- [x] `consoleMode / mode list`.
- [x] `consoleMode / mode <columns> <rows>`.
- [x] `keyboard / keyb` without argument.
- [x] `keyboard / keyb -l <code>`.
- [x] `keyboard / keyb --layout <code>`.
- [x] `pause / pause` without argument.
- [x] `pause / pause on`.
- [x] `pause / pause off`.
- [x] `changeFolder / cf / cd` with relative path.
- [x] `changeFolder / cf / cd` with absolute path.
- [x] `makeFolder / mf / md / mkdir <name>`.
- [x] `listFolder / lf / dir / ls` on current folder.
- [x] `listFolder / lf / dir / ls <path>`.
- [x] `listFolder / lf / dir / ls -p`.
- [x] `listFolder / lf / dir / ls -r`.
- [x] `listFolder / lf / dir / ls --stress`.
- [x] `listFolder / lf / dir / ls --sort=name`.
- [x] `listFolder / lf / dir / ls --sort=extension`.
- [x] `listFolder / lf / dir / ls --sort=modified`.
- [x] `listFolder / lf / dir / ls --limit=<n>`.
- [x] `listFolder / lf / dir / ls -sn / -se / -sm`.
- [x] `listFolder / lf / dir / ls -l<n>`.

## Block 2 - Storage and File Commands

- [ ] `copy / cp <source> <target>`.
- [ ] `type / show <path>`.
- [ ] `edit / ed` without argument.
- [ ] `edit / ed <path>`.
- [ ] `edit / ed -n`.
- [ ] `edit / ed --lineNumbers`.
- [ ] `storage / storage list`.
- [ ] `fs / fileSystem list`.

## Block 3 - User and Session Commands

- [ ] `addUser / newUser <username>` path.
- [ ] `addUser / newUser` interactive username path.
- [ ] `deleteUser / delUser <username>`.
- [ ] `login / login <username>` path.
- [ ] `login / login` interactive username path.
- [ ] `logout / logout`.
- [ ] `setPassword / passwd`.
- [ ] `whoAmI / who`.

## Block 4 - System and Diagnostics Commands

- [ ] `credits / credits`.
- [ ] `dataView / data`.
- [ ] `disasm / dis <address> <instructionCount>`.
- [ ] `memEdit / mem <address>`.
- [x] `memoryMap / memMap`.
- [x] `memoryMap --heap`.
- [ ] `net / network devices`.
- [ ] `nvme / nvme list`.
- [ ] `pic / pic`.
- [ ] `profiling / prof`.
- [ ] `profiling / prof reset`.
- [ ] `systemInfo / sys`.
- [ ] `task / task list`.
- [ ] `autotest / autotest stack`.
- [ ] `reboot / reboot`.
- [ ] `shutdown / powerOff`.
- [ ] `quit / exit`.

## Block 5 - Drivers, Graphics, Desktop, USB

- [ ] `driver / drv list`.
- [ ] `driver / drv <alias>`.
- [ ] `desktop / dskt` default status path.
- [ ] `desktop / dskt status`.
- [ ] `desktop / dskt show`.
- [ ] `desktop / dskt theme <path-or-name>`.
- [ ] `usb / usb ports`.
- [ ] `usb / usb devices`.
- [ ] `usb / usb deviceTree`.
- [ ] `usb / usb drives`.
- [ ] `usb / usb probe`.

## Block 6 - Launch and Package Commands

- [ ] `run / launch <name>`.
- [ ] `run / launch -b <name>`.
- [ ] `run / launch --background <name>`.
- [ ] `package / package run <package>`.
- [ ] `package / package run <package> <command-name>`.
- [ ] `package / package run <package> <command-name> <args...>`.
- [ ] `package / package list <package-name>`.
- [ ] `package / package list <path.epk>`.
- [ ] `package / package add <package-name>`.
- [ ] `package / package add <path.epk>`.

## Block 7 - Scripting Exposed Objects

- [x] `process.count`.
- [x] `process[0].fileName`.
- [x] `process[0].task.count`.
- [x] `process[0].task[0].name`.
- [x] one admin-visible `process[*].task[*].stack.base`.
- [x] one admin-visible `process[*].task[*].architecture.context`.
- [x] `task.count`.
- [x] `task[0].status`.
- [x] `driver.count`.
- [x] `driver[0].alias`.
- [x] `one graphics-capable driver.mode.count`.
- [x] `one graphics-capable driver.mode[0].width`.
- [x] `driver[0].enumDomain.count`.
- [x] `graphics.frontend`.
- [x] `graphics.mode.width`.
- [x] `clock.uptimeMs`.
- [x] `clock.bootDatetime.year`.
- [x] `clock.currentDatetime.second`.
- [x] `storage.count`.
- [x] `storage[0].bytesPerSector`.
- [x] `fileSystem.activePartitionName`.
- [x] `fileSystem.mounted.count`.
- [x] `fileSystem.mounted[0].name`.

Deferred from Block 7:
- [ ] `fileSystem.unused.count` in an image exposing at least one unused file system.
- [ ] `fileSystem.unused[0].name` in an image exposing at least one unused file system.

## Block 8 - Scripting Exposed Objects Continued

- [x] `pciBus.count`.
- [x] `pciBus[0].number`.
- [x] `pciDevice.count`.
- [x] `pciDevice[0].vendorId`.
- [x] `usb.port.count`.
- [x] `usb.port[0].connected`.
- [x] `usb.device.count`.
- [x] `usb.device[0].vendorId`.
- [x] `usb.drive.count`.
- [ ] `usb.drive[0].blockSize`.
- [x] `usb.node.count`.
- [x] `usb.node[0].nodeType`.
- [x] `network.device.count`.
- [x] `network.device[0].name`.
- [x] `keyboard.layout`.
- [ ] `mouse.x`.
- [x] `account.count`.
- [ ] `account[0].name`.

Deferred from Block 8:
- [ ] `usb.drive[0].blockSize` with at least one exposed USB mass-storage device.
- [ ] `mouse.x` in a scenario exposing an initialized pointer position.
- [ ] `account[0].name` in an image or scenario exposing at least one account.
