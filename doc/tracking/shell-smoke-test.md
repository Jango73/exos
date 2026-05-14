# Shell Smoke Test Tracking

Goal: build one dedicated shell smoke test plan that covers every built-in command, every supported option or subcommand, and at least one field for every scripting-exposed object shape.

Conventions:
- Each item starts unchecked and will be updated as blocks are implemented and validated.
- Command entries use `primary / alias` naming when the shell exposes both names in the command table.
- For scripting exposure, the objective is at least one representative field per exposed object shape, including nested arrays or sub-objects when they have their own descriptor.

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
- [x] `changeFolder / cf` with relative path.
- [x] `changeFolder / cf` with absolute path.
- [x] `makeFolder / mf <name>`.
- [x] `listFolder / lf` on current folder.
- [x] `listFolder / lf <path>`.
- [x] `listFolder / lf -p`.
- [x] `listFolder / lf -r`.
- [x] `listFolder / lf -s`.
- [x] `listFolder / lf --stress`.

## Block 2 - Storage and File Commands

- [ ] `copy / cp <source> <target>`.
- [ ] `type / show <path>`.
- [ ] `edit / ed` without argument.
- [ ] `edit / ed <path>`.
- [ ] `edit / ed -n`.
- [ ] `edit / ed --lineNumbers`.
- [ ] `disk / disk list`.
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
- [ ] `memoryMap / memMap`.
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

## Block 7 - Scripting Exposed Objects Part 1

- [ ] `process.count`.
- [ ] `process[0].fileName`.
- [ ] `process[0].task.count`.
- [ ] `process[0].task[0].name`.
- [ ] `process[0].task[0].stack.base`.
- [ ] `process[0].task[0].architecture.context`.
- [ ] `task.count`.
- [ ] `task[0].status`.
- [ ] `driver.count`.
- [ ] `driver[0].alias`.
- [ ] `driver[0].mode.count`.
- [ ] `driver[0].mode[0].width`.
- [ ] `driver[0].enumDomain.count`.
- [ ] `graphics.frontend`.
- [ ] `graphics.mode.width`.
- [ ] `clock.uptimeMs`.
- [ ] `clock.bootDatetime.year`.
- [ ] `clock.currentDatetime.second`.
- [ ] `storage.count`.
- [ ] `storage[0].bytesPerSector`.
- [ ] `fileSystem.activePartitionName`.
- [ ] `fileSystem.mounted.count`.
- [ ] `fileSystem.mounted[0].name`.
- [ ] `fileSystem.unused.count`.
- [ ] `fileSystem.unused[0].name`.

## Block 8 - Scripting Exposed Objects Part 2

- [ ] `memoryMap.kernelRegion.count`.
- [ ] `memoryMap.kernelRegion[0].tag`.
- [ ] `pciBus.count`.
- [ ] `pciBus[0].number`.
- [ ] `pciDevice.count`.
- [ ] `pciDevice[0].vendorId`.
- [ ] `usb.port.count`.
- [ ] `usb.port[0].connected`.
- [ ] `usb.device.count`.
- [ ] `usb.device[0].vendorId`.
- [ ] `usb.drive.count`.
- [ ] `usb.drive[0].blockSize`.
- [ ] `usb.node.count`.
- [ ] `usb.node[0].nodeType`.
- [ ] `network.device.count`.
- [ ] `network.device[0].name`.
- [ ] `keyboard.layout`.
- [ ] `mouse.x`.
- [ ] `account.count`.
- [ ] `account[0].name`.
