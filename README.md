# Snapshot

Snapshot is a powerful save management tool for the Nintendo DS, inspired by the 3DS save manager **Checkpoint**. It allows users to backup and restore save data from both SD card ROMs and physical Cartridges.

![Snapshot UI](Screenshot.jpg)

## Features

- **SD & Slot-1 Support**: Automatically scans your SD card and the physical cartridge slot for games.
- **TWiLight Menu Compatible**: Full support for TWiLight Menu save slots (`.sav`, `.sav1`, ..., `.sav9`).
- **Game Recognition**: Displays game icons and long titles extracted directly from ROM banners.
- **Custom Backups**: Create save backups with custom names or use automatic timestamps.
- **Backup Management**: Restore, rename, or delete existing backups directly from the interface.

---

> [!WARNING]  
> **Current Status of Cartridge Support**: Cartridge backup and restore functionality is currently **not fully functional** and is undergoing development. Use with caution for Slot-1 devices.

## Controls

- **D-Pad**: Navigate game list and menus.
- **A**: Confirm selection / Enter game details.
- **B**: Back / Cancel.
- **L**: Create a new Backup.
- **R**: Restore selected Backup.
- **X**: Delete selected Backup.
- **Y**: Rename selected Backup.
- **START**: Confirm.
- **SELECT**: Cancel.

## Build Requirements

The project is built using the **BlocksDS** SDK and **devkitPro**.