#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_FILES_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_FILES_H

#include "authoring/source_package_snapshot.h"

/* Collects the complete explicit package-source forest, without environment
 * discovery or changing cwd. Roots must be absolute directories; root symlinks
 * are canonicalized. Hidden descendants are skipped. Other descendant symlinks
 * are rejected. Overlapping roots are scanned once; longest-root ownership is
 * still determined by source_package_snapshot. Files are sorted by path.
 * Limits: 256 .chibi files, 512 KiB/file, 8 MiB total, 4096 path bytes/visited
 * entries, sixteen directory levels below the longest matching root.
 * Invalid options, filesystem errors and loader limits return NULL with a
 * value-owned error. Parsed/graph errors return an owned invalid snapshot;
 * inspect source_package_snapshot_error_at. Input strings are not retained. */
source_package_snapshot *source_package_snapshot_load(
    const source_package_disk_options *,
    source_package_load_error *
);
#endif
