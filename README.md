# Calculinux opkg Fork

This is a fork of [opkg](https://git.yoctoproject.org/opkg/) with Calculinux-specific enhancements for read-only root filesystems with overlayfs and A/B RAUC updates.

## Branch: calculinux-v0.9.0

Based on upstream opkg v0.9.0 with the following enhancements:

### Image Status File Support
- New `image_status_file` configuration option in opkg.conf
- Loads read-only image status file first, then merges writable status  
- Image status file is never modified by opkg
- Proper package merging when packages appear in both files

### Query Filtering Features
- Track package source (PKG_SOURCE_WRITABLE, PKG_SOURCE_IMAGE, PKG_SOURCE_BOTH)
- `--writable-only` flag to filter queries to writable status only
- `--image-only` flag to filter queries to image status only  
- `--show-source` flag to display package source in output
- Filtering applies to status, list-installed, and files commands

## Usage in Calculinux

This fork enables proper package tracking for systems where:
- Base image contains pre-installed packages
- Writable overlay allows additional package installation
- Package reconciliation tools need to distinguish between image and overlay packages

Used by [calculinux-update](https://github.com/Calculinux/calculinux-update) for intelligent package reconciliation during system updates.

## Upstream

Upstream repository: https://git.yoctoproject.org/opkg/  
Upstream version: 0.7.0

## License

GPLv2+ (same as upstream opkg)
