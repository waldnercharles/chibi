/* support/win32/sys/file.h -- BSD flock over LockFileEx. The lock covers the
 * whole file and ends when the descriptor closes. See posix.h. */
#ifndef CHIBI_SUPPORT_WIN32_SYS_FILE_H
#define CHIBI_SUPPORT_WIN32_SYS_FILE_H

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

int posix_win32_flock(int fd, int operation);

#define flock(fd, operation) posix_win32_flock(fd, operation)

#endif
