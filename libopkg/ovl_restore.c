/* vi: set expandtab sw=4 sts=4: */
/* ovl_restore.c - overlayfs upper-layer whiteout restore for opkg
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
 * See ovl_restore.h for the purpose of this file.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "opkg_message.h"
#include "ovl_restore.h"

/*
 * The ioctls and their argument structs are defined by the Calculinux
 * overlayfs module (include/uapi/linux/overlayfs.h in
 * https://github.com/Calculinux/overlayfs). They are replicated here so
 * libopkg does not depend on the module's uapi header being installed; the
 * values must stay in sync with that header.
 */
struct ovl_restore_lower_args {
    __aligned_u64 path_ptr;   /* Pointer to null-terminated path string */
    __u32 path_len;           /* strlen(path) */
    __u32 flags;              /* Reserved, must be 0 */
};

struct ovl_upper_state_args {
    __aligned_u64 path_ptr;   /* in: pointer to null-terminated path */
    __u32 path_len;           /* in: strlen(path) */
    __u32 flags;              /* in: reserved, must be 0 */
    __u32 state;              /* out: OVL_UPPER_STATE_* */
    __u32 state_flags;        /* out: OVL_UPPER_STATE_F_* */
    __u32 mode;               /* out: st_mode of an UPPER entry, else 0 */
    __u32 pad;                /* in: must be 0 */
};

#define OVL_UPPER_STATE_NONE           0   /* no upper entry */
#define OVL_UPPER_STATE_WHITEOUT       1   /* upper entry is a whiteout */
#define OVL_UPPER_STATE_UPPER          2   /* real upper entry (see mode) */

/* The upper entry is an opaque directory (hides the lower directory) */
#define OVL_UPPER_STATE_F_OPAQUE       (1U << 0)
/* The parent has no lower directory (pure upper, or below an opaque dir) */
#define OVL_UPPER_STATE_F_NO_LOWER_DIR (1U << 1)

#define OVL_IOC_RESTORE_LOWER _IOW('O', 1, struct ovl_restore_lower_args)
#define OVL_IOC_IS_RESTORABLE _IOW('O', 2, struct ovl_restore_lower_args)
#define OVL_IOC_UPPER_STATE _IOWR('O', 3, struct ovl_upper_state_args)

#define MAX_OVERLAY_MOUNTS 32

static char overlay_mounts[MAX_OVERLAY_MOUNTS][PATH_MAX];
static int overlay_mount_fds[MAX_OVERLAY_MOUNTS];
static int overlay_mount_count;
static int mounts_initialized;

/* Decodes the octal escapes mountinfo uses for whitespace ("\040" et al.). */
static char *mountinfo_decode(char *token)
{
    char *src = token, *dst = token;

    while (*src) {
        if (*src == '\\' && src[1] >= '0' && src[1] <= '7'
                && src[2] >= '0' && src[2] <= '7'
                && src[3] >= '0' && src[3] <= '7') {
            *dst++ = (char) (((src[1] - '0') << 6) | ((src[2] - '0') << 3)
                             | (src[3] - '0'));
            src += 4;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return token;
}

static int mounts_load(void)
{
    char line[PATH_MAX * 4];
    FILE *fp;

    fp = fopen("/proc/self/mountinfo", "r");
    if (fp == NULL) {
        opkg_perror(ERROR, "Can't read /proc/self/mountinfo");
        return -1;
    }

    memset(overlay_mounts, 0, sizeof(overlay_mounts));
    overlay_mount_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *sep, *left, *right, *fields[16];
        int nfields = 0, i;

        sep = strstr(line, " - ");
        if (sep == NULL)
            continue;
        *sep = '\0';
        left = line;
        right = sep + 3;

        /* The mount point is the fifth field on the left side of " - ". */
        for (i = 0; i < 16 && *left; i++) {
            fields[i] = left;
            left = strchr(left, ' ');
            if (left == NULL)
                break;
            *left++ = '\0';
            nfields = i + 1;
        }
        if (nfields < 5)
            continue;

        /* The fs type is the first field on the right side of " - ". */
        {
            char *fstype_sep = strpbrk(right, " \t");

            if (fstype_sep)
                *fstype_sep = '\0';
            if (strcmp(right, "overlay") != 0 && strcmp(right, "overlayfs") != 0)
                continue;
        }

        if (overlay_mount_count >= MAX_OVERLAY_MOUNTS) {
            opkg_msg(NOTICE, "More than %d overlay mounts; giving up\n",
                     MAX_OVERLAY_MOUNTS);
            break;
        }
        mountinfo_decode(fields[4]);
        strncpy(overlay_mounts[overlay_mount_count], fields[4], PATH_MAX - 1);
        overlay_mounts[overlay_mount_count][PATH_MAX - 1] = '\0';
        overlay_mount_fds[overlay_mount_count] = -1;
        overlay_mount_count++;
    }
    fclose(fp);
    mounts_initialized = 1;
    return 0;
}

/* The longest-prefix overlay mount containing @path, or NULL. */
static const char *overlay_mount_for(const char *path)
{
    int i;
    const char *best = NULL;
    int best_len = -1;

    if (!mounts_initialized) {
        if (mounts_load() != 0)
            return NULL;
    }

    for (i = 0; i < overlay_mount_count; i++) {
        int len = (int) strlen(overlay_mounts[i]);
        if (len > best_len
                && strncmp(path, overlay_mounts[i], len) == 0
                && (path[len] == '/' || path[len] == '\0')) {
            best = overlay_mounts[i];
            best_len = len;
        }
    }
    return best;
}

static int overlay_mount_fd(const char *mount)
{
    int i;

    for (i = 0; i < overlay_mount_count; i++) {
        if (strcmp(mount, overlay_mounts[i]) == 0) {
            if (overlay_mount_fds[i] < 0) {
                overlay_mount_fds[i] = open(mount, O_RDONLY | O_DIRECTORY);
                if (overlay_mount_fds[i] < 0) {
                    opkg_perror(ERROR, "Can't open overlay mount %s", mount);
                    return -1;
                }
            }
            return overlay_mount_fds[i];
        }
    }
    return -1;
}

/* @scratch receives @path minus its trailing slashes, NUL terminated. */
static const char *strip_trailing_slashes(const char *path, char *scratch,
                                          size_t scratch_len)
{
    char *end;

    if (strlen(path) + 1 > scratch_len)
        return NULL;
    strcpy(scratch, path);
    end = scratch + strlen(scratch);
    while (end > scratch + 1 && *(end - 1) == '/')
        *--end = '\0';
    return scratch;
}

int ovl_restore_whiteouts(const char *const *paths, size_t count,
                          int dry_run, int *restored, int *unsupported)
{
    char scratch[PATH_MAX];
    size_t i;
    int result = 0;

    if (restored)
        *restored = 0;
    if (unsupported)
        *unsupported = 0;

    for (i = 0; i < count; i++) {
        const char *clean =
            strip_trailing_slashes(paths[i], scratch, sizeof(scratch));
        const char *mount;
        int fd;

        if (clean == NULL) {
            opkg_msg(DEBUG,
                     "Path %s too long for overlay restore, skipping\n",
                     paths[i]);
            continue;
        }

        /*
         * Collapse extra leading slashes: opkg builds its own paths as
         * root_dir ("/") + "/var/lib/..." and friends, yielding "//var/...".
         * open() tolerates that, but the kernel wants a clean absolute path
         * and the mount lookup below matches against mount points.
         */
        while (clean[0] == '/' && clean[1] == '/')
            memmove(clean, clean + 1, strlen(clean));

        mount = overlay_mount_for(clean);
        if (mount == NULL)
            continue;     /* not on an overlay: nothing can be shadowed there */
        fd = overlay_mount_fd(mount);
        if (fd < 0)
            return -1;

        {
            /* Which overlay mount and with which fd was resolved above, so
             * the probes below can sit in their own block scope. */
            struct ovl_upper_state_args ua;

            memset(&ua, 0, sizeof(ua));
            ua.path_ptr = (uintptr_t) clean;
            ua.path_len = (uint32_t) strlen(clean);
            errno = 0;
            if (ioctl(fd, OVL_IOC_UPPER_STATE, &ua) < 0) {
                if (errno == ENOTTY) {
                    /* Kernel without the Calculinux ioctls. */
                    if (unsupported)
                        *unsupported = 1;
                    break;
                }
                if (errno == ENOENT || errno == ENOTDIR || errno == EXDEV
                        || errno == EINVAL)
                    /* Unresolvable path or no upper entry at all. */
                    continue;
                opkg_perror(ERROR,
                            "Failed to query the upper layer state of %s",
                            clean);
                result = -1;
                break;
            }

            if (ua.state != OVL_UPPER_STATE_WHITEOUT)
                continue;
            if (ua.state_flags & OVL_UPPER_STATE_F_NO_LOWER_DIR)
                continue;     /* nothing underneath to reveal */

            if (dry_run) {
                if (restored)
                    *restored += 1;
                opkg_msg(INFO, "Would restore %s from the base image.\n",
                         clean);
                continue;
            }

            {
                struct ovl_restore_lower_args ra;

                memset(&ra, 0, sizeof(ra));
                ra.path_ptr = (uintptr_t) clean;
                ra.path_len = (uint32_t) strlen(clean);
                errno = 0;
                if (ioctl(fd, OVL_IOC_RESTORE_LOWER, &ra) < 0) {
                    if (errno == ENOENT || errno == EEXIST
                            || errno == ENODATA)
                        /* Benign: raced away or nothing underneath. */
                        continue;
                    opkg_perror(ERROR,
                                "Failed to remove the whiteout at %s", clean);
                    result = -1;
                    break;
                }
            }

            if (restored)
                *restored += 1;
            opkg_msg(INFO, "Restored %s from the base image.\n", clean);
        }
    }

    return result;
}
