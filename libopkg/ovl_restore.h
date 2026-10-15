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
 *
 * ovl_purge_stale_upper() deals with the other, rarer way the base image can
 * stay hidden: an upper-layer entry that no status file knows about (typically
 * a package that was installed from a feed on an earlier image and whose
 * record was lost when the image was flashed, while /data persisted).
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

/*
 * Find upper-layer entries at @paths that no opkg status file records
 * (state UPPER instead of a whiteout or no entry), and optionally remove
 * them so the base image entries underneath become visible again.
 *
 * @paths/@count:   as for ovl_restore_whiteouts().
 * @do_purge:       0 = report stale entries only; 1 = remove them.
 * @dry_run:        with @do_purge, count the removals without performing
 *                  them. Ignored when @do_purge is 0.
 * @exempt_pkg:     the name of the package whose own bookkeeping (for
 *                  example its base image record) does not disqualify
 *                  a path; entries recorded by any other installed
 *                  package are skipped. May be NULL.
 * @stale:          receives the number of stale entries found; may be NULL.
 * @purged:         receives the number of entries removed (or to be removed
 *                  with @dry_run); may be NULL.
 * @unsupported:    as for ovl_restore_whiteouts(); may be NULL.
 *
 * Entries are dropped through the overlay's own VFS operations (the
 * Calculinux module owns the upper layer and its path caches; the
 * backing upper directory must never be mutated directly). Removing an
 * entry the base image also carries is a sticky removal as far as the
 * module is concerned, so the whiteouts such removals leave behind are
 * lifted again (OVL_IOC_RESTORE_LOWER) as part of this call. Directory
 * entries are only removed when empty and opaque.
 *
 * Returns 0 on success (including "nothing to do"); entries the module
 * declines to drop are reported and left in place.
 */
int ovl_purge_stale_upper(const char *const *paths, size_t count,
                          int do_purge, int dry_run,
                          const char *exempt_pkg,
                          int *stale, int *purged, int *unsupported);

#ifdef __cplusplus
}
#endif

#endif                          /* OVL_RESTORE_H */
