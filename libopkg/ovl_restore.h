/* vi: set expandtab sw=4 sts=4: */
/* ovl_restore.h - overlayfs upper-layer whiteout restore for opkg
 *
 * Carl D. Worth
 * Copyright (C) 2001 University of Southern California
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Calculinux keeps packages the user installs in the overlayfs upper layer
 * on top of a read-only base image. When such a package is removed, overlayfs
 * leaves a whiteout for every removed file that also exists in the lower
 * layer, which hides the image's copy. The functions in this header use the
 * upper-state ioctls of Calculinux's overlayfs module
 * (https://github.com/Calculinux/overlayfs,
 * include/uapi/linux/overlayfs.h) to remove those whiteouts again.
 */

#ifndef OVL_RESTORE_H
#define OVL_RESTORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Remove the whiteouts at @paths so the base-image (lower layer) entries
 * underneath become visible again.
 *
 * @paths/@count: absolute paths to check; trailing slashes are ignored and
 *                paths that are not on an overlay mount are skipped.
 * @dry_run:      count what would be restored without changing anything.
 * @restored:     receives the number of whiteouts removed (or to be removed
 *                with @dry_run); may be NULL.
 * @unsupported:  set to 1 if the running kernel's overlayfs lacks the
 *                Calculinux ioctls; probing stops at the first such path. May
 *                be NULL.
 *
 * Returns 0 on success (including "nothing to restore"), -1 when a restore
 * call failed unexpectedly.
 */
int ovl_restore_whiteouts(const char *const *paths, size_t count,
                          int dry_run, int *restored, int *unsupported);

#ifdef __cplusplus
}
#endif

#endif                          /* OVL_RESTORE_H */
