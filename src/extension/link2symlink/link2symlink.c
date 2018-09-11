#include <stdio.h>     /* rename(2), */
#include <stdlib.h>    /* atoi */
#include <unistd.h>    /* symlink(2), symlinkat(2), readlink(2), lstat(2), unlink(2), unlinkat(2)*/
#include <string.h>    /* str*, strrchr, strcat, strcpy, strncpy, strncmp */
#include <sys/types.h> /* lstat(2), */
#include <sys/stat.h>  /* lstat(2), */
#include <errno.h>     /* E*, */
#include <limits.h>    /* PATH_MAX, */

/**
 * Fix to allow getdents to hide files with a given prefix '.l2s'.
 * 
 * The URL of original source code is:
 * https://github.com/corbinlc/GNURootDebian/blob/master/GNURootDebianSource/src/main/build_rootfs/PRoot/src/extension/hidden_files/hidden_files.c
 * 
 * hidden_files.c is written by Dieter Mueller on 6/12/2015.
 */

#include "cli/note.h"
#include "extension/extension.h"
#include "tracee/tracee.h"
#include "tracee/mem.h"
#include "syscall/syscall.h"
#include "syscall/sysnum.h"
#include "path/path.h"
#include "arch.h"
#include "attribute.h"
#include "syscall/chain.h"	/* from hidden_files.c */

#ifdef USERLAND
#define PREFIX ".proot.l2s."
#endif 
#ifndef USERLAND
#define PREFIX ".l2s."
#endif 
#define DELETED_SUFFIX " (deleted)"

/**
 *  Definition of struct linux_dirent and struct linux_dirent64
 *  from hidden_files.c
 *  This difinition from hidden_files.c
 */
struct linux_dirent {
    unsigned long d_ino;
    unsigned long d_off;
    unsigned short d_reclen;
    char d_name[];
};

struct linux_dirent64 {
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

/**
 * Copy the contents of the @symlink into @value (nul terminated).
 * This function returns -errno if an error occured, otherwise 0.
 */
static int my_readlink(const char symlink[PATH_MAX], char value[PATH_MAX])
{
	ssize_t size;

	size = readlink(symlink, value, PATH_MAX);
	if (size < 0)
		return size;
	if (size >= PATH_MAX)
		return -ENAMETOOLONG;
	value[size] = '\0';

	return 0;
}

/**
 * Blind copies the given num of bytes from @src to @dst
 * from hidden_files.c
 * This function from hidden_files.c and fixed by Z.OOL. (mailto:zool@zool.jpn.org)
 */
static void mybcopy(char *src, char *dst, unsigned int num) {
    while(num--) {
		*(dst++) = *(src++);
	}
}

/**
 * Compares the given @prefix with the given string.
 * If @str has the given @prefix, return 1. Otherwise
 * return 0
 * This function from hidden_files.c and fixed by Z.OOL. (mailto:zool@zool.jpn.org)
 */
static int hasprefix(const char *prefix, char *str) {
	return (strncmp((const char *)str, prefix, strlen(prefix)) ? 0 : 1);
}

/**
 * Move the path pointed to by @tracee's @sysarg to a new location,
 * symlink the original path to this new one, make @tracee's @sysarg
 * point to the new location.  This function returns -errno if an
 * error occured, otherwise 0.
 */
static int move_and_symlink_path(Tracee *tracee, Reg sysarg)
{
	char original[PATH_MAX];
	char intermediate[PATH_MAX];
	char new_intermediate[PATH_MAX];
	char final[PATH_MAX];
	char new_final[PATH_MAX];
	char final_bak[PATH_MAX];
	char intermediate_bak[PATH_MAX];
	char * name;
	const char * l2s_directory;
	struct stat statl;
	ssize_t size;
	int status;
	int link_count;
	int first_link = 1;
	int intermediate_suffix = 1;

	/* Note: this path was already canonicalized.  */
	size = read_string(tracee, original, peek_reg(tracee, CURRENT, sysarg), PATH_MAX);
	if (size < 0)
		return size;
	if (size >= PATH_MAX)
		return -ENAMETOOLONG;

	/* Sanity check: directories can't be linked.  */
	status = lstat(original, &statl);
	if (status < 0)
		return status;
	if (S_ISDIR(statl.st_mode))
		return -EPERM;

	/* Check if it is a symbolic link.  */
	if (S_ISLNK(statl.st_mode)) {
		/* get name */
		size = my_readlink(original, intermediate);
		if (size < 0)
			return size;

		name = strrchr(intermediate, '/');
		if (name == NULL)
			name = intermediate;
		else
			name++;

		if (strncmp(name, PREFIX, strlen(PREFIX)) == 0)
			first_link = 0;
	} else {
		/* compute new name */
		name = strrchr(original,'/');
		if (name == NULL)
			name = original;
		else
			name++;

		l2s_directory = getenv("PROOT_L2S_DIR");
		if (l2s_directory != NULL && l2s_directory[0]) {
			if (strlen(PREFIX) + strlen(l2s_directory) + (strlen(original) - strlen(name)) + 6 >= PATH_MAX)
				return -ENAMETOOLONG;

			strcpy(intermediate, l2s_directory);
			if (l2s_directory[strlen(l2s_directory) - 1] != '/') {
				strcat(intermediate, "/");
			}
		} else {
			if (strlen(PREFIX) + strlen(original) + 5 >= PATH_MAX)
				return -ENAMETOOLONG;

			strncpy(intermediate, original, strlen(original) - strlen(name));
			intermediate[strlen(original) - strlen(name)] = '\0';
		}
		strcat(intermediate, PREFIX);
		strcat(intermediate, name);
	}

	if (first_link) {
		/*Move the original content to the new path. */
		do {
			sprintf(new_intermediate, "%s%04d", intermediate, intermediate_suffix);
			intermediate_suffix++;
		} while ((access(new_intermediate,F_OK) != -1) && (intermediate_suffix < 1000));
		strcpy(intermediate, new_intermediate);

		strcpy(final, intermediate);
		strcat(final, ".0002");
		status = rename(original, final);
		if (status < 0)
			return status;
		status = notify_extensions(tracee, LINK2SYMLINK_RENAME, (intptr_t) original, (intptr_t) final);
		if (status < 0)
			return status;

		/* Symlink the intermediate to the final file.  */
		status = symlink(final, intermediate);
		if (status < 0) {
			/* ensure rename final -> original */
			rename(final, original);

			return status;
		}

		/* Symlink the original path to the intermediate one.  */
		status = symlink(intermediate, original);
		if (status < 0) {
			/* ensure rename final -> original */
			rename(final, original);

			/* ensure unlink intermediate */
			unlink(intermediate);

			return status;
		}
	} else {
		/*Move the original content to new location, by incrementing count at end of path. */
		size = my_readlink(intermediate, final);
		if (size < 0)
			return size;

		link_count = atoi(final + strlen(final) - 4);
		link_count++;

		strncpy(new_final, final, strlen(final) - 4);
		sprintf(new_final + strlen(final) - 4, "%04d", link_count);

		status = rename(final, new_final);
		if (status < 0)
			return status;
		status = notify_extensions(tracee, LINK2SYMLINK_RENAME, (intptr_t) final, (intptr_t) new_final);
		if (status < 0)
			return status;

		strcpy(final_bak, final);
		strcpy(final, new_final);
		snprintf(intermediate_bak, (size_t)PATH_MAX, "%s.bak", intermediate);

		/* Symlink the intermediate to the final file.  */
		status = rename(intermediate, intermediate_bak);
		if (status < 0) {
			/* ensure rename final_bak (original final) -> final */
			rename(final, final_bak);

			return status;
		}

		status = symlink(final, intermediate);
		if (status < 0) {
			/* ensure rename intermediate_bak (original intermediate) -> intermediate */
			rename(intermediate_bak, intermediate);

			/* ensure rename final_bak (original final) -> final */
			rename(final, final_bak);

			return status;
		}

		status = unlink(intermediate_bak);
		if (status < 0)
			return status;
	}

	status = set_sysarg_path(tracee, intermediate, sysarg);
	if (status < 0)
		return status;

	return 0;
}


/* If path points a file that is a symlink to a file that begins
 *   with PREFIX, let the file be deleted, but also delete the
 *   symlink that was created and decremnt the count that is tacked
 *   to end of original file.
 */
static int decrement_link_count(Tracee *tracee, Reg sysarg)
{
	char original[PATH_MAX];
	char intermediate[PATH_MAX];
	char final[PATH_MAX];
	char new_final[PATH_MAX];
	char final_bak[PATH_MAX];
	char intermediate_bak[PATH_MAX];
	char * name;
	struct stat statl;
	ssize_t size;
	int status;
	int link_count;

	/* Note: this path was already canonicalized.  */
	size = read_string(tracee, original, peek_reg(tracee, CURRENT, sysarg), PATH_MAX);
	if (size < 0)
		return size;
	if (size >= PATH_MAX)
		return -ENAMETOOLONG;

	/* Check if it is a converted link already.  */
	status = lstat(original, &statl);
	if (status < 0)
		return 0;

	if (!S_ISLNK(statl.st_mode))
		return 0;

	size = my_readlink(original, intermediate);
	if (size < 0)
		return size;

	name = strrchr(intermediate, '/');
	if (name == NULL)
		name = intermediate;
	else
		name++;

	/* Check if an l2s file is pointed to */
	if (strncmp(name, PREFIX, strlen(PREFIX)) != 0)
		return 0;

	/* Read intermediate link - if this fails then
	 * this link2symlink is broken and we silently
	 * skip as we were removing it anyway.  */
	size = my_readlink(intermediate, final);
	if (size < 0) {
		VERBOSE(tracee, 1, "Skiping deref of broken link2symlink \"%s\" -> \"%s\"", original, intermediate);
		return 0;
	}

	link_count = atoi(final + strlen(final) - 4);
	link_count--;

	/* Check if it is or is not the last link to delete */
	if (link_count > 0) {
		strncpy(new_final, final, strlen(final) - 4);
		sprintf(new_final + strlen(final) - 4, "%04d", link_count);

		status = rename(final, new_final);
		if (status < 0)
			return status;
		status = notify_extensions(tracee, LINK2SYMLINK_RENAME, (intptr_t) final, (intptr_t) new_final);
		if (status < 0)
			return status;

		strcpy(final_bak, final);
		strcpy(final, new_final);
		snprintf(intermediate_bak, (size_t)PATH_MAX, "%s.bak", intermediate);

		/* Symlink the intermediate to the final file.  */
		status = rename(intermediate, intermediate_bak);
		if (status < 0) {
			/* ensure final_bak (original final) -> final */
			rename(final_bak, final);

			return status;
		}

		status = symlink(final, intermediate);
		if (status < 0) {
			/* ensure final_bak (original final) -> final */
			rename(final_bak, final);

			/* ensure intermediate_bak (original intermediate) -> intermediate */
			rename(intermediate_bak, intermediate);

			return status;
		}

		status = unlink(intermediate_bak);
		if(status < 0)
			return status;
	} else {
		/* If it is the last, delete the intermediate and final */
		status = unlink(intermediate);
		if (status < 0)
			return status;
		status = unlink(final);
		if (status < 0)
			return status;
		status = notify_extensions(tracee, LINK2SYMLINK_UNLINK, (intptr_t) final, 0);
		if (status < 0)
			return status;
		}

	return 0;
}

/**
 * Make it so fake hard links look like real hard link with respect to number of links and inode
 * This function returns -errno if an error occured, otherwise 0.
 */
static int handle_sysexit_end(Tracee *tracee)
{
	word_t sysnum;

	sysnum = get_sysnum(tracee, ORIGINAL);

	#ifdef USERLAND
		if ((get_sysnum(tracee, CURRENT) == PR_fstat) || (get_sysnum(tracee, CURRENT) == PR_fstat64))
			return 0;

		if (((sysnum == PR_fstat) || (sysnum == PR_fstat64)) && (get_sysnum(tracee, CURRENT) == PR_readlinkat))
			return 0;
	#endif

	switch (sysnum) {

	case PR_fstatat64:                 //int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags);
	case PR_newfstatat:                //int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags);
	case PR_stat64:                    //int stat(const char *path, struct stat *buf);
	case PR_lstat64:                   //int lstat(const char *path, struct stat *buf);
	case PR_fstat64:                   //int fstat(int fd, struct stat *buf);
	case PR_stat:                      //int stat(const char *path, struct stat *buf);
	case PR_lstat:                     //int lstat(const char *path, struct stat *buf);
	case PR_fstat: {                   //int fstat(int fd, struct stat *buf);
		word_t result;
		Reg sysarg_stat;
		Reg sysarg_path;
		int status;
		struct stat statl;
		ssize_t size;
		char original[PATH_MAX];
		char intermediate[PATH_MAX];
		char final[PATH_MAX];
		char * name;
		struct stat finalStat;

		/* Override only if it succeed.  */
		result = peek_reg(tracee, CURRENT, SYSARG_RESULT);
		if (result != 0)
			return 0;

		if (sysnum == PR_fstat64 || sysnum == PR_fstat) {
			#ifndef USERLAND
				status = readlink_proc_pid_fd(tracee->pid, peek_reg(tracee, MODIFIED, SYSARG_1), original);
				if (strcmp(original + strlen(original) - strlen(DELETED_SUFFIX), DELETED_SUFFIX) == 0)
					original[strlen(original) - strlen(DELETED_SUFFIX)] = '\0';
				if (status < 0)
					return status;
			#endif
			#ifdef USERLAND
				size = read_string(tracee, original, peek_reg(tracee, CURRENT, SYSARG_2), PATH_MAX);
				if (size < 0)
					return size;
				if (size >= PATH_MAX)
					return -ENAMETOOLONG;
			#endif
		} else {
			if (sysnum == PR_fstatat64 || sysnum == PR_newfstatat)
				sysarg_path = SYSARG_2;
			else
				sysarg_path = SYSARG_1;
			size = read_string(tracee, original, peek_reg(tracee, MODIFIED, sysarg_path), PATH_MAX);
			if (size < 0)
				return size;
			if (size >= PATH_MAX)
				return -ENAMETOOLONG;
		}

		name = strrchr(original, '/');
		if (name == NULL)
			name = original;
		else
			name++;

		/* Check if it is a link */
		status = lstat(original, &statl);

		if (strncmp(name, PREFIX, strlen(PREFIX)) == 0) {
			if (S_ISLNK(statl.st_mode)) {
				strcpy(intermediate,original);
				goto intermediate_proc;
			} else {
				strcpy(final,original);
				goto final_proc;
			}
		}

		if (!S_ISLNK(statl.st_mode))
			return 0;

		size = my_readlink(original, intermediate);
		if (size < 0)
			return size;

		name = strrchr(intermediate, '/');
		if (name == NULL)
			name = intermediate;
		else
			name++;

		if (strncmp(name, PREFIX, strlen(PREFIX)) != 0)
			return 0;

		intermediate_proc: size = my_readlink(intermediate, final);
		if (size < 0)
			return size;

		final_proc: status = lstat(final,&finalStat);
		if (status < 0)
			return status;

		finalStat.st_nlink = atoi(final + strlen(final) - 4);

		/* Get the address of the 'stat' structure.  */
		if (sysnum == PR_fstatat64 || sysnum == PR_newfstatat)
			sysarg_stat = SYSARG_3;
		else
			sysarg_stat = SYSARG_2;

		#ifdef USERLAND
			/* Overwrite the stat struct with the correct number of "links". */
			read_data(tracee, &statl, peek_reg(tracee, ORIGINAL, sysarg_stat), sizeof(statl));
			finalStat.st_mode = statl.st_mode;
			finalStat.st_uid = statl.st_uid;
			finalStat.st_gid = statl.st_gid;
		#endif
		status = write_data(tracee, peek_reg(tracee, ORIGINAL,  sysarg_stat), &finalStat, sizeof(finalStat));
		if (status < 0)
			return status;

		return 0;
	}

	default:
		return 0;
	}
}

/**
 * When @translated_path is a faked hard-link, replace it with the
 * point it (internally) points to.
 */
static void translated_path(Tracee *tracee, char translated_path[PATH_MAX])
{
	char path2[PATH_MAX];
	char path[PATH_MAX];
	char *component;
	int status;

	/* Don't translate l2s symlinks if call is (un)link */
	Sysnum sysnum = get_sysnum(tracee, ORIGINAL);
	if (   sysnum == PR_unlink
	    || sysnum == PR_unlinkat
	    || sysnum == PR_link
	    || sysnum == PR_linkat
	    || sysnum == PR_rename
	    || sysnum == PR_renameat
	    || sysnum == PR_renameat2) {
		return;
	}

	status = my_readlink(translated_path, path);
	if (status < 0)
		return;

	component = strrchr(path, '/');
	if (component == NULL)
		return;
	component++;

	if (strncmp(component, PREFIX, strlen(PREFIX)) != 0)
		return;

	status = my_readlink(path, path2);
	if (status < 0)
		return;

#if 0 /* Sanity check. */
	component = strrchr(path, '/');
	if (component == NULL)
		return;
	component++;

	if (strncmp(component, PREFIX, strlen(PREFIX)) != 0)
		return;
#endif

	strcpy(translated_path, path2);
	return;
}

/**
 * Hide all files with a given PREFIX so they don't exist to the user
 * This function from hidden_files.c and fixed by Z.OOL. (mailto:zool@zool.jpn.org)
 */
static int handle_getdents(Tracee *tracee)
{
    switch (get_sysnum(tracee, ORIGINAL)) {
    case PR_getdents64:
    case PR_getdents: {
        /* get the result of the syscall, which is the number of bytes read by getdents */
        unsigned int res = peek_reg(tracee, CURRENT, SYSARG_RESULT);
        if (res <= 0) {
            return res;
        }

        /* get the system call arguments */
        word_t orig_start = peek_reg(tracee, CURRENT, SYSARG_2);
        unsigned int count = peek_reg(tracee, CURRENT, SYSARG_3);
        char orig[count];

        char path[PATH_MAX];
        int status = readlink_proc_pid_fd(tracee->pid, peek_reg(tracee, ORIGINAL, SYSARG_1), path);
        if (status < 0) {
           return 0;
        }
        if(!belongs_to_guestfs(tracee, path))
           return 0;

        /* retrieve the data from getdents */
        status = read_data(tracee, orig, orig_start, res);
        if (status < 0) {
            return status;
        }

        /* allocate a space for the copy of the data we want */
        char copy[count];
        /* curr will hold the current struct we're examining */
        struct linux_dirent64 *curr64;
        struct linux_dirent *curr32;
        /* pos keeps track of where in memory the copy is */
        char *pos = copy;
        /* ptr keeps track of where in memory the original is */
        char *ptr = orig;
        /* nleft keeps track of how many bytes we've saved */
        unsigned int nleft = 0;

        /* while we're still within the memory allowed */
        if (get_sysnum(tracee, ORIGINAL) == PR_getdents64) {
            while (ptr < orig + res) {

                /* get the current struct */
                curr64 = (struct linux_dirent64 *)ptr;

                /* if the name does not matche a given prefix */
                if (!hasprefix(PREFIX, curr64->d_name)) {

                    /* copy the information */
                    mybcopy(ptr, pos, curr64->d_reclen);

                    /* move the pos and nleft */
                    pos += curr64->d_reclen;
                    nleft += curr64->d_reclen;
                }
                /* move to the next linux_dirent */
                ptr += curr64->d_reclen;
            }
        } else {
            while (ptr < orig + res) {

                /* get the current struct */
                curr32 = (struct linux_dirent *)ptr;

                /* if the name does not matche a given prefix */
                if (!hasprefix(PREFIX, curr32->d_name)) {

                    /* copy the information */
                    mybcopy(ptr, pos, curr32->d_reclen);

                    /* move the pos and nleft */
                    pos += curr32->d_reclen;
                    nleft += curr32->d_reclen;
                }
                /* move to the next linux_dirent */
                ptr += curr32->d_reclen;
            }
        }
        /* If there is nothing left */
        if (!nleft) {
            /* call getdents again */
            if (get_sysnum(tracee, ORIGINAL) == PR_getdents64)
                register_chained_syscall(tracee, PR_getdents64, peek_reg(tracee, ORIGINAL, SYSARG_1), orig_start, count, 0, 0, 0);
            else
                register_chained_syscall(tracee, PR_getdents, peek_reg(tracee, ORIGINAL, SYSARG_1), orig_start, count, 0, 0, 0);
        }
        else {
            /* copy the data back into the register */
            status = write_data(tracee, orig_start, copy, nleft);
            if (status < 0) {
                return status;
            }
            /* update the return value to match the data */
            poke_reg(tracee, SYSARG_RESULT, nleft);
        }

        /* return successful */
        return 0;
    }

    default:
        return 0;
    }
}

/**
 * Handler for this @extension.  It is triggered each time an @event
 * occurred.  See ExtensionEvent for the meaning of @data1 and @data2.
 * This function is fixed to call handle_getdents() and the fix is written
 * by Z.OOL. (mailto:zool@zool.jpn.org)
 */
int link2symlink_callback(Extension *extension, ExtensionEvent event,
			intptr_t data1, intptr_t data2 UNUSED)
{
	int status;

	switch (event) {
	case INITIALIZATION: {
		/* List of syscalls handled by this extensions.  */
		static FilteredSysnum filtered_sysnums[] = {
			{ PR_link,		FILTER_SYSEXIT },
			{ PR_linkat,		FILTER_SYSEXIT },
			{ PR_unlink,		FILTER_SYSEXIT },
			{ PR_unlinkat,		FILTER_SYSEXIT },
			{ PR_fstat,		FILTER_SYSEXIT },
			{ PR_fstat64,		FILTER_SYSEXIT },
			{ PR_fstatat64,		FILTER_SYSEXIT },
			{ PR_lstat,		FILTER_SYSEXIT },
			{ PR_lstat64,		FILTER_SYSEXIT },
			{ PR_newfstatat,	FILTER_SYSEXIT },
			{ PR_stat,		FILTER_SYSEXIT },
			{ PR_stat64,		FILTER_SYSEXIT },
			{ PR_rename,		FILTER_SYSEXIT },
			{ PR_renameat,		FILTER_SYSEXIT },
			{ PR_renameat2,		FILTER_SYSEXIT },
			{ PR_getdents,		FILTER_SYSEXIT },	/* From hidden_files.c */
			{ PR_getdents64,	FILTER_SYSEXIT },	/* From hidden_files.c */
			FILTERED_SYSNUM_END,
		};
		extension->filtered_sysnums = filtered_sysnums;
		return 0;
	}

	case SYSCALL_ENTER_END: {
		Tracee *tracee = TRACEE(extension);

		switch (get_sysnum(tracee, ORIGINAL)) {
		case PR_rename:
			/*int rename(const char *oldpath, const char *newpath);
			 *If newpath is a psuedo hard link decrement the link count.
			 */

			status = decrement_link_count(tracee, SYSARG_2);
			if (status < 0)
				return status;

			break;

		case PR_renameat:
		case PR_renameat2:
			/*int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
			 *If newpath is a psuedo hard link decrement the link count.
			 */

			status = decrement_link_count(tracee, SYSARG_4);
			if (status < 0)
				return status;

			break;

		case PR_unlink:
			/* If path points a file that is an symlink to a file that begins
			 *   with PREFIX, let the file be deleted, but also decrement the
			 *   hard link count, if it is greater than 1, otherwise delete
			 *   the original file and intermediate file too.
			 */

			status = decrement_link_count(tracee, SYSARG_1);
			if (status < 0)
				return status;

			break;

		case PR_unlinkat:
			/* If this is request to delete directory, don't handle it here.
			 * directories cannot be hard links.  */
			if ((peek_reg(tracee, CURRENT, SYSARG_3) & AT_REMOVEDIR) != 0)
			{
				return 0;
			}

			/* If path points a file that is a symlink to a file that begins
			 *   with PREFIX, let the file be deleted, but also delete the
			 *   symlink that was created and decremnt the count that is tacked
			 *   to end of original file.
			 */

			status = decrement_link_count(tracee, SYSARG_2);
			if (status < 0)
				return status;

			break;

		case PR_link:
			/* Convert:
			 *
			 *     int link(const char *oldpath, const char *newpath);
			 *
			 * into:
			 *
			 *     int symlink(const char *oldpath, const char *newpath);
			 */

			status = move_and_symlink_path(tracee, SYSARG_1);
			if (status < 0)
				return status;

			set_sysnum(tracee, PR_symlink);
			break;

		case PR_linkat:
			/* Convert:
			 *
			 *     int linkat(int olddirfd, const char *oldpath,
			 *                int newdirfd, const char *newpath, int flags);
			 *
			 * into:
			 *
			 *     int symlink(const char *oldpath, const char *newpath);
			 *
			 * Note: PRoot has already canonicalized
			 * linkat() paths this way:
			 *
			 *   olddirfd + oldpath -> oldpath
			 *   newdirfd + newpath -> newpath
			 */

			status = move_and_symlink_path(tracee, SYSARG_2);
			if (status < 0)
				return status;

			poke_reg(tracee, SYSARG_1, peek_reg(tracee, CURRENT, SYSARG_2));
			poke_reg(tracee, SYSARG_2, AT_FDCWD);
			poke_reg(tracee, SYSARG_3, peek_reg(tracee, CURRENT, SYSARG_4));

			set_sysnum(tracee, PR_symlinkat);
			break;

		default:
			break;
		}
		return 0;
	}

	case SYSCALL_CHAINED_EXIT: {	/* From hidden_files.c */
		return handle_getdents(TRACEE(extension));
	}

	case SYSCALL_EXIT_END: {	/* From hidden_files.c */
		Tracee *tracee = TRACEE(extension);

		switch (get_sysnum(tracee, ORIGINAL)) {
		case PR_getdents:
		case PR_getdents64:
			return handle_getdents(tracee);
		default:
			return handle_sysexit_end(tracee);
		}
	}

	case TRANSLATED_PATH:
		translated_path(TRACEE(extension), (char *) data1);
		return 0;

	default:
		return 0;
	}
}
