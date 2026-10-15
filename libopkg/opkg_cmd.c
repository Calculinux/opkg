/* vi: set expandtab sw=4 sts=4: */
/* opkg_cmd.c - the opkg package management system

   Carl D. Worth

   Copyright (C) 2001 University of Southern California

   SPDX-License-Identifier: GPL-2.0-or-later

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
*/

#include "config.h"

#include <stdio.h>
#include <dirent.h>
#include <glob.h>
#include <fnmatch.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "opkg_conf.h"
#include "opkg_cmd.h"
#include "opkg_message.h"
#include "release.h"
#include "pkg.h"
#include "pkg_dest.h"
#include "pkg_parse.h"
#include "sprintf_alloc.h"
#include "file_util.h"
#include "opkg_utils.h"
#include "opkg_download.h"
#include "opkg_install.h"
#include "opkg_remove.h"
#include "opkg_configure.h"
#include "opkg_verify.h"
#include "xsystem.h"
#include "xfuncs.h"
#include "opkg_solver.h"
#include "ovl_restore.h"

static void print_pkg(pkg_t * pkg)
{
    char *version = pkg_version_str_alloc(pkg);
    printf("%s - %s", pkg->name, version);
    if (opkg_config->size) {
	if(pkg->state_status == SS_INSTALLED || pkg->state_status == SS_UNPACKED)
	    printf(" - %lu", pkg->installed_size);
	else
	    printf(" - %lu", pkg->size);
	}
    if (pkg->description)
        printf(" - %s", pkg->description);
    printf("\n");
    free(version);
}

int opkg_state_changed;

static void write_status_files_if_changed(void)
{
    if (opkg_state_changed && !opkg_config->noaction) {
        opkg_msg(INFO, "Writing status file.\n");
        opkg_conf_write_status_files();
        pkg_write_changed_filelists();
        if (!opkg_config->offline_root)
            sync();
    } else {
        opkg_msg(DEBUG, "Nothing to be done.\n");
    }
}

static void sigint_handler(int sig)
{
    signal(sig, SIG_DFL);
    opkg_msg(NOTICE, "Interrupted. Writing out status database.\n");
    write_status_files_if_changed();
    exit(128 + sig);
}


static int opkg_update_cmd(int argc, char **argv)
{
    char *tmp, *dtemp;
    int err;
    int failures;
    pkg_src_list_elt_t *iter;
    pkg_src_t *src;

    if (!file_is_dir(opkg_config->lists_dir)) {
        if (file_exists(opkg_config->lists_dir)) {
            opkg_msg(ERROR, "%s exists, but is not a directory.\n",
                     opkg_config->lists_dir);
            return -1;
        }
        err = file_mkdir_hier(opkg_config->lists_dir, 0755);
        if (err)
            return -1;
    }

    failures = 0;

    sprintf_alloc(&tmp, "%s/update-XXXXXX", opkg_config->tmp_dir);
    dtemp = mkdtemp(tmp);
    if (dtemp == NULL) {
        opkg_perror(ERROR, "Failed to make temp dir %s", opkg_config->tmp_dir);
        return -1;
    }

    for (iter = void_list_first(&opkg_config->dist_src_list); iter;
            iter = void_list_next(&opkg_config->dist_src_list, iter)) {
        char *url, *list_file_name;

        src = (pkg_src_t *) iter->data;

        sprintf_alloc(&url, "%s/dists/%s/Release", src->value, src->name);

        sprintf_alloc(&list_file_name, "%s/%s", opkg_config->lists_dir,
                      src->name);
        err = opkg_download(url, list_file_name, NULL, NULL);
        if (!err) {
            opkg_msg(NOTICE, "Downloaded release files for dist %s.\n",
                     src->name);
            release_t *release = release_new();
            err = release_init_from_file(release, list_file_name);
            if (!err) {
                if (!release_comps_supported(release, src->extra_data))
                    err = -1;
            }
            if (!err) {
                err = release_download(release, src, opkg_config->lists_dir, tmp);
            }
            release_deinit(release);
            if (err)
                unlink(list_file_name);
        }

        if (err)
            failures++;

        free(list_file_name);
        free(url);
    }

    for (iter = void_list_first(&opkg_config->pkg_src_list); iter;
            iter = void_list_next(&opkg_config->pkg_src_list, iter)) {
        src = (pkg_src_t *) iter->data;

        err = pkg_src_update(src);
        if (err)
            failures++;
    }
    rmdir(tmp);
    free(tmp);

    return failures;
}

struct opkg_intercept {
    char *oldpath;
    char *statedir;
};

typedef struct opkg_intercept *opkg_intercept_t;

static opkg_intercept_t opkg_prep_intercepts(void)
{
    opkg_intercept_t ctx;
    const char *oldpath;
    char *newpath;
    char *dtemp;

    ctx = xcalloc(1, sizeof(*ctx));
    ctx->oldpath = xstrdup(getenv("PATH"));
    oldpath = ctx->oldpath ? ctx->oldpath : "/usr/sbin:/usr/bin:/sbin:/bin";
    sprintf_alloc(&newpath, "%s:%s", opkg_config->intercepts_dir, oldpath);
    sprintf_alloc(&ctx->statedir, "%s/opkg-intercept-XXXXXX",
                  opkg_config->tmp_dir);

    dtemp = mkdtemp(ctx->statedir);
    if (dtemp == NULL) {
        opkg_perror(ERROR, "Failed to make temp dir %s", ctx->statedir);
        free(ctx->oldpath);
        free(ctx->statedir);
        free(newpath);
        free(ctx);
        return NULL;
    }

    setenv("OPKG_INTERCEPT_DIR", ctx->statedir, 1);
    setenv("PATH", newpath, 1);
    opkg_msg(DEBUG, "Added intercepts dir to PATH; new PATH=%s\n", newpath);
    free(newpath);

    return ctx;
}

static int opkg_finalize_intercepts(opkg_intercept_t ctx)
{
    DIR *dir;
    int err = 0;

    if (ctx->oldpath)
        setenv("PATH", ctx->oldpath, 1);
    else
        unsetenv("PATH");

    opkg_msg(DEBUG, "Removed intercepts dir from PATH; old PATH=%s\n", ctx->oldpath);
    free(ctx->oldpath);

    dir = opendir(ctx->statedir);
    if (dir) {
        struct dirent *de;
        while (de = readdir(dir), de != NULL) {
            char *path;

            if (de->d_name[0] == '.')
                continue;

            sprintf_alloc(&path, "%s/%s", ctx->statedir, de->d_name);
            if (access(path, X_OK) == 0) {
                const char *argv[] = { "/bin/sh", "-c", path, NULL };
                opkg_msg(DEBUG, "Run intercepted script %s\n", path);
                xsystem(argv);
            }
            free(path);
        }
        closedir(dir);
    } else
        opkg_perror(ERROR, "Failed to open dir %s", ctx->statedir);

    rm_r(ctx->statedir);
    free(ctx->statedir);
    free(ctx);

    return err;
}

/* For package pkg do the following: If it is already visited, return. If not,
   add it in visited list and recurse to its deps. Finally, add it to ordered
   list.
   pkg_vec all contains all available packages in repos.
   pkg_vec visited contains packages already visited by this function, and is
   used to end recursion and avoid an infinite loop on graph cycles.
   pkg_vec ordered will finally contain the ordered set of packages.
*/
static int opkg_recurse_pkgs_in_order(pkg_t * pkg, pkg_vec_t * all,
                                      pkg_vec_t * visited, pkg_vec_t * ordered)
{
    int j, k;
    unsigned int i, l, m;
    int count;
    pkg_t *dep;
    compound_depend_t *compound_depend;
    depend_t **possible_satisfiers;
    abstract_pkg_t *abpkg;
    abstract_pkg_t **dependents;

    /* If it's just an available package, that is, not installed and not even
     * unpacked, skip it */
    /* XXX: This is probably an overkill, since a state_status != SS_UNPACKED
     * would do here. However, if there is an intermediate node (pkg) that is
     * configured and installed between two unpacked packages, the latter
     * won't be properly reordered, unless all installed/unpacked pkgs are
     * checked */
    if (pkg->state_status == SS_NOT_INSTALLED)
        return 0;

    /* If the  package has already been visited (by this function), skip it */
    for (i = 0; i < visited->len; i++)
        if (!strcmp(visited->pkgs[i]->name, pkg->name)) {
            opkg_msg(DEBUG, "pkg %s already visited, skipping.\n", pkg->name);
            return 0;
        }

    pkg_vec_insert(visited, pkg);

    count = pkg->pre_depends_count + pkg->depends_count +
        pkg->recommends_count + pkg->suggests_count;

    opkg_msg(DEBUG, "pkg %s.\n", pkg->name);

    /* Iterate over all the dependencies of pkg. For each one, find a package
     * that is either installed or unpacked and satisfies this dependency.
     * (there should only be one such package per dependency installed or
     * unpacked). Then recurse to the dependency package */
    for (j = 0; j < count; j++) {
        compound_depend = &pkg->depends[j];
        possible_satisfiers = compound_depend->possibilities;
        for (k = 0; k < compound_depend->possibility_count; k++) {
            abpkg = possible_satisfiers[k]->pkg;
            dependents = abpkg->provided_by->pkgs;
            l = 0;
            if (dependents != NULL)
                while (l < abpkg->provided_by->len && dependents[l] != NULL) {
                    opkg_msg(DEBUG, "Descending on pkg %s.\n",
                             dependents[l]->name);

                    /* find whether dependent l is installed or unpacked,
                     * and then find which package in the list satisfies it */
                    for (m = 0; m < all->len; m++) {
                        dep = all->pkgs[m];
                        if (dep->state_status != SS_NOT_INSTALLED)
                            if (!strcmp(dep->name, dependents[l]->name)) {
                                opkg_recurse_pkgs_in_order(dep, all, visited,
                                                           ordered);
                                /* Stop the outer loop */
                                l = abpkg->provided_by->len;
                                /* break from the inner loop */
                                break;
                            }
                    }
                    l++;
                }
        }
    }

    /* When all recursions from this node down, are over, and all
     * dependencies have been added in proper order in the ordered array, add
     * also the package pkg to ordered array */
    pkg_vec_insert(ordered, pkg);

    return 0;

}

static int opkg_configure_packages(char *pkg_name)
{
    pkg_vec_t *all, *ordered, *visited;
    unsigned int i;
    pkg_t *pkg;
    opkg_intercept_t ic;
    int r, err = 0;

    if (opkg_config->offline_root && !opkg_config->force_postinstall) {
        opkg_msg(INFO,
                 "Offline root mode: not configuring unpacked packages.\n");
        return 0;
    }
    opkg_msg(INFO, "Configuring unpacked packages.\n");

    all = pkg_vec_alloc();

    pkg_hash_fetch_available(all);

    /* Reorder pkgs in order to be configured according to the Depends: tag
     * order */
    opkg_msg(INFO, "Reordering packages before configuring them...\n");
    ordered = pkg_vec_alloc();
    visited = pkg_vec_alloc();
    for (i = 0; i < all->len; i++) {
        pkg = all->pkgs[i];
        opkg_recurse_pkgs_in_order(pkg, all, visited, ordered);
    }

    ic = opkg_prep_intercepts();
    if (ic == NULL) {
        err = -1;
        goto error;
    }

    for (i = 0; i < ordered->len; i++) {
        pkg = ordered->pkgs[i];

        if (pkg_name && fnmatch(pkg_name, pkg->name, 0))
            continue;

        if (pkg->state_status == SS_UNPACKED) {
            opkg_msg(NOTICE, "Configuring %s.\n", pkg->name);
            r = opkg_configure(pkg);
            if (r == 0) {
                pkg->state_status = SS_INSTALLED;
                pkg->parent->state_status = SS_INSTALLED;
                pkg->state_flag &= ~SF_PREFER;
                opkg_state_changed++;
            } else {
                if (!opkg_config->offline_root)
                    err = -1;
            }
        }
    }

    r = opkg_finalize_intercepts(ic);
    if (r != 0)
        err = -1;

 error:
    pkg_vec_free(all);
    pkg_vec_free(ordered);
    pkg_vec_free(visited);

    return err;
}

static int opkg_remove_cmd(int argc, char **argv);

/* Metadata files opkg keeps per package below info_dir. A plain `opkg remove`
 * deletes them and thereby whiteouts the base image's copies, which hides
 * the package from opkg although the image still ships it. */
static const char *restore_metadata_suffixes[] = {
    ".list", ".control", ".conffiles",
    ".preinst", ".postinst", ".prerm", ".postrm",
};

struct restore_entry {
    char *name;             /* normalized package name */
    char *arg;              /* the original command line argument */
    const char *info_dir;   /* package's info directory, for metadata paths */
    pkg_t *pkg;             /* the package object; stable for the process */
    pkg_source_t source;    /* where the package came from at entry time */
    int in_image;           /* the base image ships the package */
    int had_overlay_copy;   /* an overlay copy had to be removed */
    str_list_t paths;       /* files recorded in the pre-removal file list */
};

/* Append the absolute paths recorded in the .list file on disk to @paths. */
static int read_disk_file_list(const char *list_file, str_list_t *paths)
{
    FILE *fp;
    char line[4096];

    fp = fopen(list_file, "r");
    if (fp == NULL)
        return 0;    /* no list readable: metadata repair is enough */
    while (fgets(line, sizeof(line), fp)) {
        char *path = line, *p, *tab;
        int is_abs;

        tab = strchr(path, '\t');
        if (tab)
            *tab = '\0';
        for (p = path + strlen(path); p > path; p--)
            if (*(p - 1) != '\n' && *(p - 1) != '\r')
                break;
        *p = '\0';
        if (*path == '\0')
            continue;
        is_abs = (*path == '/');
        if (!is_abs) {
            char *abs;
            sprintf_alloc(&abs, "/%s", path);
            str_list_append(paths, abs);
            continue;
        }
        str_list_append(paths, xstrdup(path));
    }
    fclose(fp);
    return 1;
}

/* Copy the package's recorded file list into @entry. The .list itself is
 * whiteout-healed first so that the base image's list is readable for a
 * package that is only in the image (a plain `opkg remove` whiteouts it).
 * For a removed (journal-only) package the list is read from disk directly:
 * pkg_get_installed_files() refuses to do so for non-installed packages. */
static void restore_snapshot_files(struct restore_entry *entry)
{
    pkg_t *pkg = entry->pkg;
    char *list_file = NULL;
    file_list_t *files;
    file_list_elt_t *fiter;
    int healed;

    sprintf_alloc(&list_file, "%s/%s.list", entry->info_dir, entry->name);
    if (list_file) {
        const char *lf = list_file;

        ovl_restore_whiteouts(&lf, 1, opkg_config->noaction,
                              &healed, NULL);

        if (pkg->state_status == SS_INSTALLED
                || pkg->state_status == SS_UNPACKED
                || pkg->state_status == SS_HALF_INSTALLED) {
            files = pkg_get_installed_files(pkg);
            if (files != NULL) {
                for (fiter = file_list_first(files); fiter;
                        fiter = file_list_next(files, fiter)) {
                    file_info_t *info = (file_info_t *)fiter->data;

                    if (info->path == NULL)
                        continue;
                    if (info->path[0] == '/')
                        str_list_append(&entry->paths, xstrdup(info->path));
                    else {
                        char *abs;
                        sprintf_alloc(&abs, "/%s", info->path);
                        str_list_append(&entry->paths, abs);
                    }
                }
                /* The list is cached on the package; drop the reference
                 * taken by pkg_get_installed_files (removal takes its own
                 * later). */
                pkg_free_installed_files(pkg);
            }
        } else {
            if (!read_disk_file_list(list_file, &entry->paths)
                    && opkg_config->noaction)
                opkg_msg(NOTICE,
                         "%s: its file list is whiteout-hidden and noaction "
                         "mode does not unhide it; the file-level plan for "
                         "this package is omitted\n", entry->name);
        }
        free(list_file);
    }
}

/*
 * Remove the whiteouts left by the package's removal over base image files
 * and metadata, and drop the overlay's record of the package so the image
 * status file becomes authoritative again.
 *
 * The file snapshot must be captured beforehand (see restore_snapshot_files).
 * Returns 0 on success, -1 when the heal failed; sets *@unsupported when the
 * running kernel lacks the Calculinux overlayfs ioctls.
 */
static int restore_heal_entry(struct restore_entry *entry, int meta_count,
                              int *unsupported)
{
    str_list_elt_t *pit;
    const char **snap;
    size_t snap_count = 0, idx = 0;
    int m;
    int part_restored = 0, part_unsupported = 0, r = 0;

    for (pit = str_list_first(&entry->paths); pit;
            pit = str_list_next(&entry->paths, pit))
        snap_count++;
    snap = snap_count ? xmalloc(snap_count * sizeof(*snap)) : NULL;
    for (pit = str_list_first(&entry->paths); pit;
            pit = str_list_next(&entry->paths, pit))
        snap[idx++] = (const char *)pit->data;
    if (snap)
        r = ovl_restore_whiteouts(snap, snap_count,
                                  opkg_config->noaction,
                                  &part_restored, &part_unsupported);
    free(snap);
    if (r == 0)
        for (m = 0; m < meta_count; m++) {
            char *meta_path;
            int meta_restored = 0, meta_unsupported = 0;

            sprintf_alloc(&meta_path, "%s/%s%s", entry->info_dir,
                          entry->name, restore_metadata_suffixes[m]);
            if (meta_path) {
                const char *mp = meta_path;

                r = ovl_restore_whiteouts(&mp, 1, opkg_config->noaction,
                                          &meta_restored, &meta_unsupported);
                part_restored += meta_restored;
                part_unsupported |= meta_unsupported;
                free(meta_path);
            }
            if (r != 0)
                break;
        }

    if (r != 0) {
        opkg_msg(ERROR, "Failed to restore the base image files for %s\n",
                 entry->name);
        return -1;
    }
    if (part_unsupported && !opkg_config->noaction) {
        /*
         * The whiteouts could not be lifted, so the base image files stay
         * hidden: fail, and keep the overlay record (the journal entry
         * remains, so the package still reports as removed).
         */
        *unsupported = 1;
        opkg_msg(ERROR,
                 "The base image files for %s could not be restored: the "
                 "running kernel lacks the Calculinux overlayfs ioctls.\n"
                 "The package stays removed; restore them once a kernel "
                 "with the overlayfs module runs.\n",
                 entry->name);
        return -1;
    }
    if (part_unsupported)
        *unsupported = 1;

    /*
     * The overlay no longer contributes anything to this package (the
     * overlay copy is gone, and a journal entry recorded earlier would
     * claim the opposite). Drop the overlay record so the base image
     * status file becomes authoritative again. In noaction mode the
     * plan is only reported; nothing may be written.
     */
    if (entry->source == PKG_SOURCE_BOTH && !opkg_config->noaction) {
        entry->pkg->suppress_status = 1;
        opkg_state_changed++;
    }

    if (entry->in_image) {
        if (part_restored)
            opkg_msg(NOTICE, "Restored %d base image file(s) for %s.\n",
                     part_restored, entry->name);
        else
            opkg_msg(NOTICE,
                     "Nothing was shadowed for %s; the base image copy "
                     "stands as-is.\n", entry->name);
    } else if (part_restored)
        opkg_msg(NOTICE,
                 "Removed the overlay copy of %s; restored %d shadowed base "
                 "image file(s).\n", entry->name, part_restored);
    else
        opkg_msg(NOTICE,
                 "Removed the overlay copy of %s (it is not part of the base "
                 "image).\n", entry->name);
    return 0;
}

/*
 * Shared tail for commands that relied on the Calculinux overlayfs ioctls:
 * report a kernel without them. Returns -1 to flag partial failure, except
 * in noaction mode, where the incomplete plan is only a notice.
 */
static int restore_report_unsupported(int unsupported)
{
    if (!unsupported)
        return 0;

    if (opkg_config->noaction) {
        opkg_msg(NOTICE,
                 "The running kernel lacks the Calculinux overlayfs ioctls; "
                 "the restore plan could not be completed.\n");
        return 0;
    }
    opkg_msg(ERROR,
             "The running kernel's overlayfs lacks the Calculinux overlayfs "
             "module's ioctls.\n"
             "The overlay copy was removed, but the base image files may stay "
             "hidden;\n"
             "a Calculinux kernel with the overlayfs module is required to "
             "restore them.\n");
    return -1;
}

/*
 * Detect an install argument that would just reinstall a package the base
 * image already provides, and collect it for restoring instead. Such a
 * package is a ghost: its removal was journaled in the writable status file
 * (state removed) while the image status file still ships it.
 *
 * The argument qualifies when the requested version is not newer than the
 * image's: a bare name restores unless the feed offers something newer, and
 * an explicitly pinned version must be exactly the image version. Local
 * files, remote URLs and globs always install the regular way.
 */
static int install_collect_ghost(const char *arg, struct restore_entry *entry)
{
    char *name = NULL, *version = NULL;
    version_constraint_t constraint = NONE;
    pkg_t *ghost, *candidate;
    int collected = 0;

    if (strchr(arg, '/') != NULL || strstr(arg, "://") != NULL
            || is_str_glob(arg))
        return 0;

    strip_pkg_name_and_version(arg, &name, &version, &constraint);

    ghost = pkg_hash_fetch_by_name(name);
    if (ghost == NULL
            || ghost->state_status != SS_NOT_INSTALLED
            || ghost->install_source != PKG_SOURCE_BOTH)
        goto out;

    if (version != NULL) {
        /* Only an equality pin can be satisfied by the image copy; range
         * constraints may admit a newer feed version and install normally. */
        struct pkg requested;

        if (constraint != EQUAL)
            goto out;
        parse_version(&requested, version);
        collected = pkg_version_satisfied(&requested, ghost, "=");
        free(requested.version);
        if (!collected)
            goto out;
    } else {
        candidate =
            pkg_hash_fetch_best_installation_candidate_by_name(name);
        if (candidate != NULL && candidate != ghost
                && pkg_compare_versions(candidate, ghost) > 0)
            goto out;    /* the feed offers a newer version: install it */
    }

    entry->name = xstrdup(ghost->name);
    entry->arg = NULL;
    entry->info_dir = ghost->dest->info_dir;
    entry->pkg = ghost;
    entry->source = ghost->install_source;
    entry->in_image = 1;
    entry->had_overlay_copy = 0;
    str_list_init(&entry->paths);
    restore_snapshot_files(entry);
    opkg_msg(NOTICE,
             "Restoring the base image copy of %s instead of installing it "
             "from the feed.\n", ghost->name);
    collected = 1;

out:
    free(name);
    free(version);
    return collected;
}

static int opkg_install_cmd(int argc, char **argv)
{
    struct restore_entry *ghosts;
    char **normal_argv;
    int i, ghost_count = 0, normal_count = 0;
    int err = 0;
    int r;
    int unsupported = 0;
    const size_t meta_count =
        sizeof(restore_metadata_suffixes) / sizeof(*restore_metadata_suffixes);

    signal(SIGINT, sigint_handler);

    ghosts = xcalloc(argc, sizeof(*ghosts));
    normal_argv = xmalloc(argc * sizeof(*normal_argv));

    /*
     * Partition the arguments: packages that the base image already provides
     * and whose removal was journaled are restored (their image copy becomes
     * visible) instead of being unpacked from the feed into the overlay.
     */
    for (i = 0; i < argc; i++) {
        if (install_collect_ghost(argv[i], &ghosts[ghost_count]))
            ghost_count++;
        else
            normal_argv[normal_count++] = argv[i];
    }

    /*
     * Now scan through package names and install
     */
    for (i = 0; i < normal_count; i++) {
        opkg_msg(DEBUG2, "%s\n", normal_argv[i]);
        r = opkg_prepare_url_for_install(normal_argv[i], &normal_argv[i]);
        if (r != 0)
            goto cleanup;
    }
    pkg_info_preinstall_check();

    if (normal_count > 0)
        err = opkg_solver_install(normal_count, normal_argv);

    if (ghost_count > 0 && opkg_config->offline_root)
        opkg_msg(NOTICE,
                 "Skipping the base image restore: the overlay ioctls cannot "
                 "be used on an offline root.\n");
    else
        for (i = 0; i < ghost_count; i++) {
            if (restore_heal_entry(&ghosts[i], (int) meta_count,
                                   &unsupported) != 0)
                err = -1;
        }

    if (restore_report_unsupported(unsupported) != 0)
        err = -1;

    r = opkg_configure_packages(NULL);
    if (r != 0)
        err = -1;

    write_status_files_if_changed();

 cleanup:
    for (i = 0; i < ghost_count; i++) {
        str_list_deinit(&ghosts[i].paths);
        free(ghosts[i].name);
    }
    free(normal_argv);
    free(ghosts);

    return err;
}

static int opkg_upgrade_cmd(int argc, char **argv)
{
    int err = 0;
    int r;

    signal(SIGINT, sigint_handler);

    pkg_info_preinstall_check();

    err = opkg_solver_upgrade(argc, argv);

    r = opkg_configure_packages(NULL);
    if (r != 0)
        err = -1;

    write_status_files_if_changed();

    return err;
}

static int opkg_distupgrade_cmd(int argc, char **argv)
{
    int err = 0;

#if USE_SOLVER_INTERNAL
    opkg_msg(ERROR, "dist-upgrade command not available with the internal solver enabled\n");
    err = -1;
#else
    int r;

    signal(SIGINT, sigint_handler);

    pkg_info_preinstall_check();

    err = opkg_solver_distupgrade(argc, argv);

    r = opkg_configure_packages(NULL);
    if (r != 0)
        err = -1;

    write_status_files_if_changed();
#endif
    return err;
}

static int opkg_download_cmd(int argc, char **argv)
{
    int i, err = 0;
    char *arg;
    pkg_t *pkg;
    int r;

    pkg_info_preinstall_check();
    for (i = 0; i < argc; i++) {
        arg = argv[i];

        pkg = pkg_hash_fetch_best_installation_candidate_by_name(arg);
        if (pkg == NULL) {
            err = -1;
            opkg_msg(ERROR, "Cannot find package %s.\n", arg);
            continue;
        }

        if (opkg_config->noaction) {
            opkg_msg(NOTICE, "Not downloading %s (--noaction specified).\n", pkg->name);
            continue;
        }

        r = opkg_download_pkg_to_dir(pkg, ".");
        if (r != 0) {
            err = -1;
            opkg_msg(ERROR, "Failed to download %s.\n", pkg->name);
        } else {
            opkg_msg(NOTICE, "Downloaded %s as %s.\n", pkg->name,
                     pkg->filename);
        }
    }

    return err;
}

static int opkg_list_find_cmd(int argc, char **argv, int use_desc)
{
    unsigned int i;
    pkg_vec_t *available;
    pkg_t *pkg;
    char *pkg_name = NULL;

    if (argc > 0) {
        pkg_name = argv[0];
    }
    available = pkg_vec_alloc();
    pkg_hash_fetch_available(available);
    pkg_vec_sort(available, pkg_compare_names);
    for (i = 0; i < available->len; i++) {
        pkg = available->pkgs[i];
        /* if we have package name or pattern and pkg does not match, then skip it */
        if (pkg_name && fnmatch(pkg_name, pkg->name, 0) &&
           (!use_desc || !pkg->description || fnmatch(pkg_name, pkg->description, 0)))
            continue;
        print_pkg(pkg);
    }
    pkg_vec_free(available);

    return 0;
}

static int opkg_list_cmd(int argc, char **argv)
{
    return opkg_list_find_cmd(argc, argv, 0);
}

static int opkg_find_cmd(int argc, char **argv)
{
    return opkg_list_find_cmd(argc, argv, 1);
}

static int opkg_verify_cmd(int argc, char **argv)
{
    unsigned int i;
    pkg_vec_t *available;
    pkg_t *pkg;
    char *pkg_name = NULL;

    if (argc > 0) {
        pkg_name = argv[0];
    }
    available = pkg_vec_alloc();
    pkg_hash_fetch_all_installed(available, INSTALLED);
    pkg_vec_sort(available, pkg_compare_names);
    for (i = 0; i < available->len; i++) {
        char *md5sums_file;

        pkg = available->pkgs[i];
        /* if we have package name or pattern and pkg does not match, then skip it */
        if (pkg_name && fnmatch(pkg_name, pkg->name, 0))
            continue;

        sprintf_alloc(&md5sums_file, "%s/%s.md5sums", pkg->dest->info_dir,
                pkg->name);
        if (file_exists(md5sums_file)) {
            FILE *file;
            char *expected_md5sum,  *actual_md5sum;
            char *line, *file_name, *installed_file_name;

            file  = fopen(md5sums_file, "r");
            if (file == NULL) {
                opkg_perror(ERROR, "Failed to open %s", md5sums_file);
                return -1;
            }

            while (1) {
                line = file_read_line_alloc(file);
                if (line == NULL)
                    break;

                expected_md5sum = strtok(line, " ");
                file_name = strtok(NULL, " ");
                if (file_name == NULL) {
                     opkg_msg(ERROR, "%s: Ignoring invalid line: %s\n", md5sums_file, line);
                } else {
                    sprintf_alloc(&installed_file_name, "%s%s", pkg->dest->root_dir,
                                  file_name);
                    actual_md5sum = file_md5sum_alloc(installed_file_name);
                    if (actual_md5sum != NULL) {
                        if (strcmp(expected_md5sum, actual_md5sum)) {
                           opkg_msg(ERROR, "Checksum mismatch on package: \"%s\" \"%s\"\n",
                                    pkg->name, installed_file_name);
                        }
                    }
                    free(installed_file_name);
                    free(actual_md5sum);
                }
                free(line);
            }
            fclose(file);
        }
        free(md5sums_file);
    }

    pkg_vec_free(available);

    return 0;

}

static int opkg_list_installed_cmd(int argc, char **argv)
{
    unsigned int i;
    pkg_vec_t *available;
    pkg_t *pkg;
    char *pkg_name = NULL;

    if (argc > 0) {
        pkg_name = argv[0];
    }
    available = pkg_vec_alloc();
    pkg_hash_fetch_all_installed(available, INSTALLED);
    pkg_vec_sort(available, pkg_compare_names);
    for (i = 0; i < available->len; i++) {
        pkg = available->pkgs[i];
        /* if we have package name or pattern and pkg does not match, then skip it */
        if (pkg_name && fnmatch(pkg_name, pkg->name, 0))
            continue;
        
        /* Apply source filtering */
        if (opkg_config->query_writable_only && 
            pkg->install_source != PKG_SOURCE_WRITABLE && 
            pkg->install_source != PKG_SOURCE_BOTH) {
            continue;
        }
        if (opkg_config->query_image_only && 
            pkg->install_source != PKG_SOURCE_IMAGE && 
            pkg->install_source != PKG_SOURCE_BOTH) {
            continue;
        }
        
        print_pkg(pkg);
    }

    pkg_vec_free(available);

    return 0;
}

static int opkg_list_changed_conffiles_cmd(int argc, char **argv)
{
    unsigned int i;
    pkg_vec_t *available;
    pkg_t *pkg;
    char *pkg_name = NULL;
    conffile_list_elt_t *iter;
    conffile_t *cf;

    if (argc > 0) {
        pkg_name = argv[0];
    }
    available = pkg_vec_alloc();
    pkg_hash_fetch_all_installed(available, INSTALLED);
    pkg_vec_sort(available, pkg_compare_names);
    for (i = 0; i < available->len; i++) {
        pkg = available->pkgs[i];
        /* if we have package name or pattern and pkg does not match, then skip it */
        if (pkg_name && fnmatch(pkg_name, pkg->name, 0))
            continue;
        if (nv_pair_list_empty(&pkg->conffiles))
            continue;
        for (iter = nv_pair_list_first(&pkg->conffiles); iter;
                iter = nv_pair_list_next(&pkg->conffiles, iter)) {
            cf = (conffile_t *) iter->data;
            if (cf->name && cf->value && conffile_has_been_modified(cf))
                printf("%s\n", cf->name);
        }
    }
    pkg_vec_free(available);
    return 0;
}

static int opkg_list_upgradable_cmd(int argc, char **argv)
{
    pkg_info_preinstall_check();

    return opkg_solver_list_upgradable(argc, argv);
}

static int opkg_info_status_cmd(int argc, char **argv, int installed_only)
{
    unsigned int i, err;
    pkg_vec_t *available;
    pkg_t *pkg;
    char *pkg_name = NULL;
    char b_match = 0;

    if (argc > 0) {
        pkg_name = argv[0];
    }

    available = pkg_vec_alloc();
    if (installed_only)
        pkg_hash_fetch_all_installed(available, INSTALLED_HALF_INSTALLED);
    else
        pkg_hash_fetch_available(available);

    for (i = 0; i < available->len; i++) {
        pkg = available->pkgs[i];
        if (pkg_name && fnmatch(pkg_name, pkg->name, 0)) {
            continue;
        }

        /* Apply source filtering */
        if (opkg_config->query_writable_only && 
            pkg->install_source != PKG_SOURCE_WRITABLE && 
            pkg->install_source != PKG_SOURCE_BOTH) {
            continue;
        }
        if (opkg_config->query_image_only && 
            pkg->install_source != PKG_SOURCE_IMAGE && 
            pkg->install_source != PKG_SOURCE_BOTH) {
            continue;
        }

        pkg_formatted_info(stdout, pkg, opkg_config->fields_filter);

        if (opkg_config->verbosity >= INFO) {
            conffile_list_elt_t *iter;
            for (iter = nv_pair_list_first(&pkg->conffiles); iter;
                 iter = nv_pair_list_next(&pkg->conffiles, iter)) {
                conffile_t *cf = (conffile_t *) iter->data;
                int modified = conffile_has_been_modified(cf);
                if (cf->value)
                    opkg_msg(INFO, "conffile=%s md5sum=%s modified=%d.\n",
                             cf->name, cf->value, modified);
            }
        }
        b_match = 1;
    }
    pkg_vec_free(available);

    if (!b_match && pkg_name && file_exists(pkg_name)) {
        pkg = pkg_new();
        err = pkg_init_from_file(pkg, pkg_name);
        if (err)
            return err;
        hash_insert_pkg(pkg, 0);
        pkg_formatted_info(stdout, pkg, opkg_config->fields_filter);
    }

    return 0;
}

static int opkg_info_cmd(int argc, char **argv)
{
    return opkg_info_status_cmd(argc, argv, 0);
}

static int opkg_status_cmd(int argc, char **argv)
{
    return opkg_info_status_cmd(argc, argv, 1);
}

static int opkg_configure_cmd(int argc, char **argv)
{
    int err;
    char *pkg_name = NULL;

    if (argc > 0)
        pkg_name = argv[0];

    err = opkg_configure_packages(pkg_name);

    write_status_files_if_changed();

    return err;
}

static int opkg_clean_cmd(int argc, char **argv)
{
    int err;
    err = rm_r(opkg_config->cache_dir);
    return err;
}

static int opkg_remove_cmd(int argc, char **argv)
{
    int err = 0;

    signal(SIGINT, sigint_handler);

    pkg_info_preinstall_check();

    err = opkg_solver_remove(argc, argv);

    write_status_files_if_changed();
    return err;
}


/*
 * opkg restore: discard the user (overlay) copy of a package and unshadow
 * the base image copy.
 *
 * For each package the recorded file list is snapshotted, the overlay copy
 * is removed through the ordinary removal path (maintainer scripts,
 * dependency checks and force options all apply), and the whiteouts that
 * removal leaves over lower-layer entries are removed again. The overlay's
 * record of the package (including the journal entry a plain `opkg remove`
 * of an image package leaves behind) is dropped so the base image status
 * becomes authoritative again. Packages that are only in the image status
 * are never removed themselves; for them the whiteout heal is the entire
 * operation, recovering the damage a plain `opkg remove` of an image
 * package caused.
 */
static int opkg_restore_cmd(int argc, char **argv)
{
    struct restore_entry *entries;
    char **remove_args;
    pkg_t *pkg;
    int i, err = 0;
    int remove_count = 0;
    int unsupported = 0;
    const size_t meta_count =
        sizeof(restore_metadata_suffixes) / sizeof(*restore_metadata_suffixes);

    signal(SIGINT, sigint_handler);

    pkg_info_preinstall_check();

    opkg_msg(NOTICE,
             "Discarding the overlay copy of %d package(s) and restoring the "
             "base image copy: ...\n", argc);

    entries = xcalloc(argc, sizeof(*entries));
    remove_args = xmalloc(argc * sizeof(*remove_args));
    for (i = 0; i < argc; i++)
        str_list_init(&entries[i].paths);

    for (i = 0; i < argc; i++) {
        if (opkg_config->restrict_to_default_dest)
            pkg = pkg_hash_fetch_installed_by_name_dest(argv[i],
                                                        opkg_config->default_dest);
        else
            pkg = pkg_hash_fetch_installed_by_name(argv[i]);
        if (pkg == NULL)
            /* Also accept journal entries: removals of base image packages
             * are recorded in the writable status file as removed. */
            pkg = pkg_hash_fetch_by_name(argv[i]);
        if (pkg == NULL) {
            opkg_msg(ERROR,
                     "Package %s is not installed (checked the overlay and "
                     "the image status);\n"
                     "nothing to restore.\n", argv[i]);
            err = -1;
            goto free_entries;
        }
        entries[i].name = xstrdup(pkg->name);
        entries[i].arg = argv[i];
        entries[i].info_dir = pkg->dest->info_dir;
        entries[i].pkg = pkg;
        entries[i].source = pkg->install_source;
        entries[i].in_image = (pkg->install_source == PKG_SOURCE_IMAGE
                               || pkg->install_source == PKG_SOURCE_BOTH);
        entries[i].had_overlay_copy = (pkg->state_status == SS_INSTALLED
                                       || pkg->state_status == SS_UNPACKED
                                       || pkg->state_status == SS_HALF_INSTALLED)
                && pkg->install_source != PKG_SOURCE_IMAGE;
        if (!entries[i].had_overlay_copy && !entries[i].in_image) {
            opkg_msg(ERROR,
                     "Package %s is not installed and is not shipped by the "
                     "base image;\n"
                     "nothing to restore.\n", argv[i]);
            err = -1;
            goto free_entries;
        }
        restore_snapshot_files(&entries[i]);
    }

    for (i = 0; i < argc; i++) {
        if (entries[i].had_overlay_copy)
            remove_args[remove_count++] = entries[i].arg;
    }

    if (remove_count > 0) {
        err = opkg_solver_remove(remove_count, remove_args);
        if (err)
            goto free_entries;
    }

    if (opkg_config->offline_root)
        opkg_msg(NOTICE,
                 "Skipping the base image restore: the overlay ioctls cannot "
                 "be used on an offline root.\n");
    else
        for (i = 0; i < argc; i++) {
            if (restore_heal_entry(&entries[i], (int) meta_count,
                                   &unsupported) != 0)
                err = -1;
        }

    if (restore_report_unsupported(unsupported) != 0)
        err = -1;

 free_entries:
    for (i = 0; i < argc; i++) {
        str_list_deinit(&entries[i].paths);
        free(entries[i].name);
    }
    free(remove_args);
    free(entries);

    write_status_files_if_changed();
    return err;
}

/** \brief opkg_is_state_flag: tells whether the flag is state flag
 *
 * \param flag
 * \return 1 is success, 0 otherwise
 */
static int opkg_is_state_flag(const char* flag)
{
    return (
        (strcmp(flag, "hold") == 0) ||
        (strcmp(flag, "noprune") == 0) ||
        (strcmp(flag, "ok") == 0) ||
        (strcmp(flag, "user") == 0)
    );
}

/** \brief opkg_is_state_status_flag: tells whether the flag is status flag
 *
 * \param flag
 * \return 1 is success, 0 otherwise
 */
static int opkg_is_state_status_flag(const char* flag)
{
    return (
        (strcmp(flag, "installed") == 0) ||
        (strcmp(flag, "unpacked") == 0)
    );
}

static int opkg_flag_cmd(int argc, char **argv)
{
    int i;
    pkg_t *pkg;
    const char *flags = argv[0];

    int is_state_flag = opkg_is_state_flag(flags);
    int is_state_status_flag = opkg_is_state_status_flag(flags);

    if ((!is_state_flag) && (!is_state_status_flag)) {
        opkg_msg(ERROR, "Invalid flag: %s.\n", flags);
        return -1;
    }

    signal(SIGINT, sigint_handler);

    for (i = 1; i < argc; i++) {
        if (opkg_config->restrict_to_default_dest) {
            pkg = pkg_hash_fetch_installed_by_name_dest(argv[i],
                    opkg_config->default_dest);
        } else {
            pkg = pkg_hash_fetch_installed_by_name(argv[i]);
        }

        if (pkg == NULL) {
            opkg_msg(ERROR, "Package %s is not installed.\n", argv[i]);
            continue;
        }

        if (is_state_flag) {
            pkg->state_flag = pkg_state_flag_from_str(flags);
        }

        /*
         * Useful if a package is installed in an offline_root, and
         * should be configured by opkg configure at a later date.
         */
        if (is_state_status_flag) {
            pkg->state_status = pkg_state_status_from_str(flags);
        }

        opkg_state_changed++;
        opkg_msg(NOTICE, "Setting flags for package %s to %s.\n", pkg->name,
                 flags);
    }

    write_status_files_if_changed();
    return 0;
}

static int opkg_files_cmd(int argc, char **argv)
{
    pkg_t *pkg;
    file_list_t *files;
    file_list_elt_t *iter;
    char *pkg_version;

    if (argc < 1) {
        return -1;
    }

    pkg = pkg_hash_fetch_installed_by_name(argv[0]);
    if (pkg == NULL) {
        opkg_msg(ERROR, "Package %s not installed.\n", argv[0]);
        return 0;
    }

    /* Apply source filtering */
    if (opkg_config->query_writable_only && 
        pkg->install_source != PKG_SOURCE_WRITABLE && 
        pkg->install_source != PKG_SOURCE_BOTH) {
        opkg_msg(ERROR, "Package %s not in writable status.\n", argv[0]);
        return 0;
    }
    if (opkg_config->query_image_only && 
        pkg->install_source != PKG_SOURCE_IMAGE && 
        pkg->install_source != PKG_SOURCE_BOTH) {
        opkg_msg(ERROR, "Package %s not in image status.\n", argv[0]);
        return 0;
    }

    files = pkg_get_installed_files(pkg);
    pkg_version = pkg_version_str_alloc(pkg);

    printf("Package %s (%s) is installed on %s and has the following files:\n",
           pkg->name, pkg_version, pkg->dest->name);

    for (iter = file_list_first(files); iter; iter = file_list_next(files, iter)) {
        file_info_t *info = (file_info_t *)iter->data;
        printf("%s\n", info->path);
    }

    free(pkg_version);
    pkg_free_installed_files(pkg);

    return 0;
}

static int opkg_depends_cmd(int argc, char **argv)
{
    int i, k;
    unsigned int j;
    int depends_count;
    pkg_vec_t *available_pkgs;
    compound_depend_t *cdep;
    pkg_t *pkg;
    char *str;

    pkg_info_preinstall_check();

    available_pkgs = pkg_vec_alloc();
    if (opkg_config->query_all)
        pkg_hash_fetch_available(available_pkgs);
    else
        pkg_hash_fetch_all_installed(available_pkgs, INSTALLED);

    for (i = 0; i < argc; i++) {
        for (j = 0; j < available_pkgs->len; j++) {
            pkg = available_pkgs->pkgs[j];

            if (fnmatch(argv[i], pkg->name, 0) != 0)
                continue;

            depends_count = pkg->depends_count + pkg->pre_depends_count +
                pkg->recommends_count + pkg->suggests_count;

            opkg_msg(NOTICE, "%s depends on:\n", pkg->name);

            for (k = 0; k < depends_count; k++) {
                cdep = &pkg->depends[k];

                if (cdep->type != DEPEND)
                    continue;

                str = pkg_depend_str(pkg, k);
                opkg_msg(NOTICE, "\t%s\n", str);
                free(str);
            }

        }
    }

    pkg_vec_free(available_pkgs);
    return 0;
}

static int pkg_mark_provides(pkg_t * pkg)
{
    int provides_count = pkg->provides_count;
    abstract_pkg_t **provides = pkg->provides;
    int i;
    pkg->parent->state_flag |= SF_MARKED;
    for (i = 0; i < provides_count; i++) {
        provides[i]->state_flag |= SF_MARKED;
    }
    return 0;
}

static int opkg_what_depends_conflicts_replaces_cmd(enum depend_type what_field_type,
                                                    int recursive, int argc, char **argv)
{
    depend_t *possibility;
    compound_depend_t *cdep;
    pkg_vec_t *available_pkgs;
    pkg_t *pkg;
    int i, k, l;
    unsigned int j;
    int changed, count;
    const char *rel_str = NULL;
    char *ver;

    switch (what_field_type) {
    case DEPEND:
        rel_str = "depends on";
        break;
    case CONFLICTS:
        rel_str = "conflicts with";
        break;
    case SUGGEST:
        rel_str = "suggests";
        break;
    case RECOMMEND:
        rel_str = "recommends";
        break;
    case REPLACES:
        rel_str = "replaces";
        break;
    default:
        return -1;
    }

    available_pkgs = pkg_vec_alloc();

    if (opkg_config->query_all)
        pkg_hash_fetch_available(available_pkgs);
    else
        pkg_hash_fetch_all_installed(available_pkgs, INSTALLED);

    /* mark the root set */
    pkg_vec_clear_marks(available_pkgs);
    opkg_msg(NOTICE, "Root set:\n");
    for (i = 0; i < argc; i++)
        pkg_vec_mark_if_matches(available_pkgs, argv[i]);

    for (j = 0; j < available_pkgs->len; j++) {
        pkg = available_pkgs->pkgs[j];
        if (pkg->state_flag & SF_MARKED) {
            /* mark the parent (abstract) package */
            pkg_mark_provides(pkg);
            opkg_msg(NOTICE, "  %s\n", pkg->name);
        }
    }

    opkg_msg(NOTICE, "What %s root set\n", rel_str);
    do {
        changed = 0;

        for (j = 0; j < available_pkgs->len; j++) {
            pkg = available_pkgs->pkgs[j];
            count = (what_field_type == CONFLICTS) ? pkg->conflicts_count :
                    ((what_field_type == REPLACES) ? pkg->replaces_count :
                    (pkg->pre_depends_count + pkg->depends_count + pkg->recommends_count + pkg->suggests_count));

            /* skip this package if it is already marked */
            if (pkg->parent->state_flag & SF_MARKED)
                continue;

            for (k = 0; k < count; k++) {
                cdep = (what_field_type == CONFLICTS) ? &pkg->conflicts[k] :
                       ((what_field_type == REPLACES) ? &pkg->replaces[k] : &pkg->depends[k]);

                if (what_field_type != cdep->type)
                    continue;

                for (l = 0; l < cdep->possibility_count; l++) {
                    possibility = cdep->possibilities[l];

                    if ((possibility->pkg->state_flag & SF_MARKED) != SF_MARKED)
                        continue;

                    /* mark the depending package so we
                     * won't visit it again */
                    pkg->state_flag |= SF_MARKED;
                    pkg_mark_provides(pkg);
                    changed++;

                    ver = pkg_version_str_alloc(pkg);
                    opkg_msg(NOTICE, "\t%s %s\t%s %s", pkg->name, ver, rel_str,
                             possibility->pkg->name);
                    free(ver);
                    if (possibility->version) {
                        opkg_msg(NOTICE, " (%s%s)",
                                 constraint_to_str(possibility->constraint),
                                 possibility->version);
                    }
                    if (!pkg_dependence_satisfiable(possibility))
                        opkg_msg(NOTICE, " unsatisfiable");
                    opkg_message(NOTICE, "\n");
                    goto next_package;
                }
            }
 next_package:
            ;
        }
    } while (changed && recursive);

    pkg_vec_free(available_pkgs);

    return 0;
}

static int opkg_whatdepends_recursively_cmd(int argc, char **argv)
{
    return opkg_what_depends_conflicts_replaces_cmd(DEPEND, 1, argc, argv);
}

static int opkg_whatdepends_cmd(int argc, char **argv)
{
    return opkg_what_depends_conflicts_replaces_cmd(DEPEND, 0, argc, argv);
}

static int opkg_whatsuggests_cmd(int argc, char **argv)
{
    return opkg_what_depends_conflicts_replaces_cmd(SUGGEST, 0, argc, argv);
}

static int opkg_whatrecommends_cmd(int argc, char **argv)
{
    return opkg_what_depends_conflicts_replaces_cmd(RECOMMEND, 0, argc, argv);
}

static int opkg_whatconflicts_cmd(int argc, char **argv)
{
    return opkg_what_depends_conflicts_replaces_cmd(CONFLICTS, 0, argc, argv);
}

static int opkg_whatprovides_cmd(int argc, char **argv)
{
    if (argc > 0) {
        pkg_vec_t *available_pkgs = pkg_vec_alloc();
        int i;

        pkg_info_preinstall_check();

        if (opkg_config->query_all)
            pkg_hash_fetch_available(available_pkgs);
        else
            pkg_hash_fetch_all_installed(available_pkgs, INSTALLED);
        for (i = 0; i < argc; i++) {
            const char *target = argv[i];
            unsigned int j;

            opkg_msg(NOTICE, "What provides %s\n", target);
            for (j = 0; j < available_pkgs->len; j++) {
                pkg_t *pkg = available_pkgs->pkgs[j];
                int k;
                int count = pkg->provides_count;
                for (k = 0; k < count; k++) {
                    abstract_pkg_t *apkg = pkg->provides[k];
                    if (fnmatch(target, apkg->name, 0) == 0) {
                        opkg_msg(NOTICE, "    %s", pkg->name);
                        if (strcmp(target, apkg->name) != 0)
                            opkg_msg(NOTICE, "\tprovides %s\n", apkg->name);
                        opkg_message(NOTICE, "\n");
                    }
                }
            }
        }
        pkg_vec_free(available_pkgs);
    }
    return 0;
}

static int opkg_whatreplaces_cmd(int argc, char **argv)
{
    return opkg_what_depends_conflicts_replaces_cmd(REPLACES, 0, argc, argv);
}

static int opkg_search_cmd(int argc, char **argv)
{
    unsigned int i;

    pkg_vec_t *installed;
    pkg_t *pkg;
    file_list_t *installed_files;
    file_list_elt_t *iter;
    file_info_t *installed_file;
    int found_match = 0;

    if (argc < 1) {
        return -1;
    }

    installed = pkg_vec_alloc();
    pkg_hash_fetch_all_installed(installed, INSTALLED);
    pkg_vec_sort(installed, pkg_compare_names);

    for (i = 0; i < installed->len; i++) {
        pkg = installed->pkgs[i];

        installed_files = pkg_get_installed_files(pkg);

        for (iter = file_list_first(installed_files); iter;
                iter = file_list_next(installed_files, iter)) {
            installed_file = (file_info_t *)iter->data;
            if (fnmatch(argv[0], installed_file->path, 0) == 0) {
                found_match = 1;
                print_pkg(pkg);
            }
        }

        pkg_free_installed_files(pkg);
    }

    pkg_vec_free(installed);

    if (!found_match) {
        opkg_msg(ERROR, "no path found matching pattern '%s'\n", argv[0]);
        return -1;
    }

    return 0;
}

static int opkg_compare_versions_cmd(int argc, char **argv)
{
    if (argc == 3) {
        /* this is a bit gross */
        struct pkg p1, p2;
        parse_version(&p1, argv[0]);
        parse_version(&p2, argv[2]);
        p1.force_reinstall = p2.force_reinstall = 0;
        return pkg_version_satisfied(&p1, &p2, argv[1]) ? 0 : 1;
    } else {
        opkg_msg(ERROR,
                 "opkg compare_versions <v1> <op> <v2>\n"
                 "<op> is one of <= >= << >> =\n");
        return -1;
    }
}

static int opkg_print_architecture_cmd(int argc, char **argv)
{
    nv_pair_list_elt_t *l;

    list_for_each_entry(l, &opkg_config->arch_list.head, node) {
        nv_pair_t *nv = (nv_pair_t *) l->data;
        printf("arch %s %s\n", nv->name, nv->value);
    }
    return 0;
}

/* XXX: CLEANUP: The usage strings should be incorporated into this
   array for easier maintenance */
static opkg_cmd_t cmds[] = {
    {"update", 0, (opkg_cmd_fun_t) opkg_update_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"upgrade", 0, (opkg_cmd_fun_t) opkg_upgrade_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"dist-upgrade", 0, (opkg_cmd_fun_t) opkg_distupgrade_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"list", 0, (opkg_cmd_fun_t) opkg_list_cmd, PFM_SOURCE, false},
    {"list_installed", 0, (opkg_cmd_fun_t) opkg_list_installed_cmd, PFM_SOURCE,
        false},
    {"list-installed", 0, (opkg_cmd_fun_t) opkg_list_installed_cmd, PFM_SOURCE,
        false},
    {"list_upgradable", 0, (opkg_cmd_fun_t) opkg_list_upgradable_cmd,
        PFM_SOURCE, false},
    {"list-upgradable", 0, (opkg_cmd_fun_t) opkg_list_upgradable_cmd,
        PFM_SOURCE, false},
    {"list_changed_conffiles", 0,
        (opkg_cmd_fun_t) opkg_list_changed_conffiles_cmd, PFM_SOURCE, false},
    {"list-changed-conffiles", 0,
        (opkg_cmd_fun_t) opkg_list_changed_conffiles_cmd, PFM_SOURCE, false},
    {"info", 0, (opkg_cmd_fun_t) opkg_info_cmd, 0, false},
    {"flag", 1, (opkg_cmd_fun_t) opkg_flag_cmd, PFM_DESCRIPTION | PFM_SOURCE,
        true},
    {"status", 0, (opkg_cmd_fun_t) opkg_status_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"install", 1, (opkg_cmd_fun_t) opkg_install_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"remove", 1, (opkg_cmd_fun_t) opkg_remove_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"restore", 1, (opkg_cmd_fun_t) opkg_restore_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"clean", 0, (opkg_cmd_fun_t) opkg_clean_cmd, 0, true},
    {"configure", 0, (opkg_cmd_fun_t) opkg_configure_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, true},
    {"files", 1, (opkg_cmd_fun_t) opkg_files_cmd, PFM_DESCRIPTION | PFM_SOURCE,
        false},
    {"search", 1, (opkg_cmd_fun_t) opkg_search_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"find", 1, (opkg_cmd_fun_t) opkg_find_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"verify", 0, (opkg_cmd_fun_t) opkg_verify_cmd, 0, false},
    {"download", 1, (opkg_cmd_fun_t) opkg_download_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"compare_versions", 1, (opkg_cmd_fun_t) opkg_compare_versions_cmd, 0,
        false},
    {"compare-versions", 1, (opkg_cmd_fun_t) opkg_compare_versions_cmd, 0,
        false},
    {"print-architecture", 0, (opkg_cmd_fun_t) opkg_print_architecture_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"print_architecture", 0, (opkg_cmd_fun_t) opkg_print_architecture_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"depends", 1, (opkg_cmd_fun_t) opkg_depends_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatdepends", 1, (opkg_cmd_fun_t) opkg_whatdepends_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatdependsrec", 1, (opkg_cmd_fun_t) opkg_whatdepends_recursively_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatrecommends", 1, (opkg_cmd_fun_t) opkg_whatrecommends_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatsuggests", 1, (opkg_cmd_fun_t) opkg_whatsuggests_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatprovides", 1, (opkg_cmd_fun_t) opkg_whatprovides_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatreplaces", 1, (opkg_cmd_fun_t) opkg_whatreplaces_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
    {"whatconflicts", 1, (opkg_cmd_fun_t) opkg_whatconflicts_cmd,
        PFM_DESCRIPTION | PFM_SOURCE, false},
};

opkg_cmd_t *opkg_cmd_find(const char *name)
{
    int i;
    opkg_cmd_t *cmd;
    int num_cmds = sizeof(cmds) / sizeof(opkg_cmd_t);

    for (i = 0; i < num_cmds; i++) {
        cmd = &cmds[i];
        if (strcmp(name, cmd->name) == 0)
            return cmd;
    }

    return NULL;
}

int opkg_cmd_exec(opkg_cmd_t * cmd, int argc, const char **argv)
{
    int ret = 0;

    if (cmd->privileged) {
        ret = opkg_lock();
        if (ret != 0) {
            opkg_perror(ERROR, "Command failed to capture privilege lock");
            return ret;
        }
    }

    ret = (cmd->fun) (argc, argv);

    if (cmd->privileged)
        opkg_unlock();
    return ret;
}
