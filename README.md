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

### Removal Journal
- Removing a package the base image also ships records the removal in the
  writable status file, so it keeps reporting as removed instead of
  springing back to "installed (image)" with its files whiteouted.
- `opkg install <pkg>` of such a journal-recorded package performs the
  restore instead of downloading and unpacking a redundant copy into the
  overlay: a bare package name restores the image copy unless the feed
  offers a newer version, and an explicitly pinned version must be exactly
  the image version. Local files, remote URLs and globbed arguments always
  install the regular way.

### `opkg restore` Command
- `opkg restore <pkgs>` removes the overlay copy of a package and unshadows
  the base image copy: the overlay removal goes through the ordinary remove
  path, then the whiteouts left over lower-layer entries (package files and
  opkg metadata alike) are removed via the `OVL_IOC_RESTORE_LOWER` ioctl of
  the [Calculinux overlayfs module](https://github.com/Calculinux/overlayfs),
  and the overlay's record of the package is dropped so the image status
  file becomes authoritative again.
- For a package that a plain `opkg remove` whiteout without recording,
  `opkg restore` is the repair: it removes nothing and only restores the
  hidden base image files and metadata.
- `--noaction` previews the whole operation; on kernels without the
  Calculinux overlayfs module the removal still happens, but opkg warns
  that the base image files cannot be restored until a kernel providing
  the module runs.

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
