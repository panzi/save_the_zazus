#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
#include <archive.h>
#include <archive_entry.h>

#if defined(_WIN16) || defined(_WIN32) || defined(_WIN64)
#	define STZ_WINDOWS
#	define STZ_PATH_SEP '\\'
#	define SEP "\\"
#else
#	define STZ_PATH_SEP '/'
#	define SEP "/"
#endif

extern unsigned char package_img_atlas0_png[];
extern unsigned char package_img_atlas1_png[];
extern unsigned char package_img_atlas2_png[];

extern unsigned int package_img_atlas0_png_len;
extern unsigned int package_img_atlas1_png_len;
extern unsigned int package_img_atlas2_png_len;

struct patch_entry {
	const char *path;
	const unsigned char *data;
	const size_t size;
};

#define _STR(X) #X
#define STR(X) _STR(X)
#define PDEBUG() perror(__FILE__ ":" STR(__LINE__) ":error")
#define FDEBUG(FMT, ...) fprintf(stderr, "%s:%u:error: " FMT "\n", __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG(MSG) fprintf(stderr, "%s:%u:error: %s\n", __FILE__, __LINE__, MSG)

#ifdef __linux__

// use sendfile() under Linux for zero-context switch file copy
#include <fcntl.h>
#include <sys/sendfile.h>

static int copyfile(const char *src, const char *dst) {
	int status =  0;
	int infd   = -1;
	int outfd  = -1;
	struct stat info;

	infd = open(src, O_RDONLY);
	if (infd < 0) {
		goto error;
	}

	outfd = open(dst, O_CREAT | O_WRONLY);
	if (outfd < 0) {
		goto error;
	}

	if (fstat(infd, &info) < 0) {
		goto error;
	}

	if (sendfile(outfd, infd, NULL, (size_t)info.st_size) < (ssize_t)info.st_size) {
		goto error;
	}

	goto end;

error:
	status = -1;

end:
	if (infd >= 0) {
		close(infd);
		infd = -1;
	}

	if (outfd >= 0) {
		close(outfd);
		outfd = -1;
	}

	return status;
}
#else
static int copyfile(const char *src, const char *dst) {
	char buf[BUFSIZ];
	FILE *fsrc = NULL;
	FILE *fdst = NULL;
	int status = 0;

	fsrc = fopen(src, "rb");
	if (!fsrc) {
		goto error;
	}

	fdst = fopen(dst, "wb");
	if (!fdst) {
		goto error;
	}

	for (;;) {
		size_t count = fread(buf, 1, BUFSIZ, fsrc);

		if (count < BUFSIZ && ferror(fsrc)) {
			goto error;
		}

		if (count > 0 && fwrite(buf, 1, count, fdst) != count) {
			goto error;
		}

		if (count < BUFSIZ) {
			break;
		}
	}

	goto end;

error:
	status = -1;

end:

	if (fsrc) {
		fclose(fsrc);
		fsrc = NULL;
	}

	if (fdst) {
		fclose(fdst);
		fdst = NULL;
	}

	return status;
}
#endif

#if defined(STZ_WINDOWS)
#	include <windows.h>

#define STZ_ARCH_PATH "\\steamapps\\common\\Save the Dodos\\package.nw"

struct reg_path {
	HKEY    hKey;
	LPCTSTR lpSubKey;
	LPCTSTR lpValueName;
};

static int get_path_from_registry(HKEY hKey, LPCTSTR lpSubKey, LPCTSTR lpValueName, char *path, size_t pathlen) {
	HKEY hSubKey = 0;
	DWORD dwType = REG_SZ;
	DWORD dwSize = pathlen;

	if (pathlen < sizeof(STZ_ARCH_PATH)) {
		return ENAMETOOLONG;
	}

	if (RegOpenKeyEx(hKey, lpSubKey, 0, KEY_QUERY_VALUE, &hSubKey) != ERROR_SUCCESS) {
		return ENOENT;
	}

	if (RegQueryValueEx(hSubKey, lpValueName, NULL, &dwType, (LPBYTE)path, &dwSize) != ERROR_SUCCESS) {
		return ENOENT;
	}

	if (dwType != REG_SZ) {
		return ENOENT;
	}
	else if (dwSize > pathlen - sizeof(STZ_ARCH_PATH)) {
		return ENAMETOOLONG;
	}

	strcat(path, STZ_ARCH_PATH);

	return 0;
}

static int find_archive(char *path, size_t pathlen) {
	static const struct reg_path reg_paths[] = {
		// Have confirmed sigthing of these keys:
		{ HKEY_LOCAL_MACHINE, TEXT("Software\\Valve\\Steam"),              TEXT("InstallPath") },
		{ HKEY_LOCAL_MACHINE, TEXT("Software\\Wow6432node\\Valve\\Steam"), TEXT("InstallPath") },
		{ HKEY_CURRENT_USER,  TEXT("Software\\Valve\\Steam"),              TEXT("SteamPath")   },

		// All the other possible combination, just to to try everything:
		{ HKEY_CURRENT_USER,  TEXT("Software\\Wow6432node\\Valve\\Steam"), TEXT("SteamPath")   },
		{ HKEY_LOCAL_MACHINE, TEXT("Software\\Valve\\Steam"),              TEXT("SteamPath")   },
		{ HKEY_LOCAL_MACHINE, TEXT("Software\\Wow6432node\\Valve\\Steam"), TEXT("SteamPath")   },
		{ HKEY_CURRENT_USER,  TEXT("Software\\Valve\\Steam"),              TEXT("InstallPath") },
		{ HKEY_CURRENT_USER,  TEXT("Software\\Wow6432node\\Valve\\Steam"), TEXT("InstallPath") },
		{ 0,                  0,                                           0                   }
	};

	for (const struct reg_path* reg_path = reg_paths; reg_path->lpSubKey; ++ reg_path) {
		int errnum = get_path_from_registry(reg_path->hKey, reg_path->lpSubKey, reg_path->lpValueName, path, pathlen);
		if (errnum == 0) {
			return 0;
		}
		else if (errnum != ENOENT) {
			errno = errnum;
			return -1;
		}
	}

	errno = ENOENT;
	return -1;
}
#elif defined(__APPLE__)

#error Mac OS X not yet supported!
#define STZ_STEAM_ARCHIVE "Library/Application Support/Steam/SteamApps/common/Save the Dodos/Save the Dodos.app/Contents/Resources/package.nw"
#define STZ_APP_ARCHIVE   "/Applications/Save the Dodos.app/Contents/Resources/package.nw"

static int stz_join_path(char *buf, size_t size, const char *comps[], size_t ncomps) {
	size_t ch_index = 0;
	bool first = true;

	for (size_t comp_index = 0; comp_index < ncomps; ++ comp_index) {
		const char *comp = comps[comp_index];

		if (first) {
			first = false;
		}
		else if (ch_index >= size) {
			errno = ENAMETOOLONG;
			return -1;
		}
		else {
			buf[ch_index] = STZ_PATH_SEP;
			++ ch_index;
		}

		size_t complen = strlen(comp);
		if (complen >= size || ch_index >= size - complen) {
			errno = ENAMETOOLONG;
			return -1;
		}

		memcpy(buf + ch_index, comp, complen);
		ch_index += complen;
	}

	if (ch_index >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}

	buf[ch_index] = '\0';

	return 0;
}

#define STZ_JOIN_PATH(BUF, SIZE, ...) \
	stz_join_path((BUF), (SIZE), \
		(const char *[]){ __VA_ARGS__ }, \
		sizeof((const char *[]){ __VA_ARGS__ }) / sizeof(char*))

static int find_archive(char *path, size_t pathlen) {
	const char *home = getenv("HOME");
	struct stat info;

	if (!home) {
		return -1;
	}

	if (STZ_JOIN_PATH(path, pathlen, home, STZ_STEAM_ARCHIVE) == 0) {
		if (stat(path, &info) < 0) {
			if (errno != ENOENT) {
				perror(path);
			}
		}
		else if (S_ISREG(info.st_mode)) {
			return 0;
		}
	}

	if (stat(STZ_APP_ARCHIVE, &info) < 0) {
		if (errno != ENOENT) {
			perror(path);
		}
		return -1;
	}
	else if (S_ISREG(info.st_mode)) {
		if (strlen(STZ_APP_ARCHIVE) + 1 > pathlen) {
			errno = ENAMETOOLONG;
			return -1;
		}
		strcpy(path, STZ_APP_ARCHIVE);
		return 0;
	}

	errno = ENOENT;
	return -1;
}
#else // default: Linux
#include <dirent.h>

static int find_path_ignore_case(const char *home, const char *prefix, const char* const path[], char buf[], size_t size) {
	int count = snprintf(buf, size, "%s/%s", home, prefix);
	if (count < 0) {
		return -1;
	}
	else if ((size_t)count >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}

	for (const char* const* nameptr = path; *nameptr; ++ nameptr) {
		const char* realname = NULL;
		DIR *dir = opendir(buf);

		if (!dir) {
			if (errno != ENOENT) {
				perror(buf);
			}
			return -1;
		}

		for (;;) {
			errno = 0;
			struct dirent *entry = readdir(dir);
			if (entry) {
				if (strcasecmp(entry->d_name, *nameptr) == 0) {
					realname = entry->d_name;
					break;
				}
			}
			else if (errno == 0) {
				break; // end of dir
			}
			else {
				perror(buf);
				return -1;
			}
		}

		if (!realname) {
			closedir(dir);
			errno = ENOENT;
			return -1;
		}

		if (strlen(buf) + strlen(realname) + 2 > size) {
			errno = ENAMETOOLONG;
			return -1;
		}

		strcat(buf, "/");
		strcat(buf, realname);

		closedir(dir);
	}

	return 0;
}

static int find_archive(char *path, size_t pathlen) {
	// Steam was developed for Windows, which has case insenstive file names.
	// Therefore I can't assume a certain case and because I don't want to write
	// a parser for registry.vdf I scan the filesystem for certain names in a case
	// insensitive manner.
	static const char* const path1[] = {".local/share", "Steam", "SteamApps", "common", "Save the Dodos", "package.nw", NULL};
	static const char* const path2[] = {".steam", "Steam", "SteamApps", "common", "Save the Dodos", "package.nw", NULL};
	static const char const* const* paths[] = {path1, path2, NULL};

	const char *home = getenv("HOME");

	if (!home) {
		return -1;
	}

	for (const char* const* const* ptr = paths; ptr; ++ ptr) {
		const char* const* path_spec = *ptr;
		if (find_path_ignore_case(home, path_spec[0], path_spec + 1, path, pathlen) == 0) {
			struct stat info;

			if (stat(path, &info) < 0) {
				if (errno != ENOENT) {
					perror(path);
				}
			}
			else if (S_ISREG(info.st_mode)) {
				return 0;
			}
		}
	}

	errno = ENOENT;
	return -1;
}
#endif

int main() {
	char archive_path[PATH_MAX];
	char backup_path[PATH_MAX];
	const struct patch_entry patches[] = {
		// TODO: Maybe I need "\\" on Windows?
		{"img/atlas0.png", package_img_atlas0_png, package_img_atlas0_png_len},
		{"img/atlas1.png", package_img_atlas1_png, package_img_atlas1_png_len},
		{"img/atlas2.png", package_img_atlas2_png, package_img_atlas2_png_len},
		{NULL, NULL, 0},
	};
	int status = EXIT_SUCCESS;
	bool made_backup = false;
	struct archive *arch = NULL;
	struct archive *backup = NULL;
	struct archive_entry *entry = NULL;
	char *buf = NULL;
	size_t buf_size = 0;

	printf("Searching game archive.\n");

	if (find_archive(archive_path, PATH_MAX) < 0) {
		DEBUG("Couldn't find package.nw");
		goto error;
	}

	printf("Creating archive backup.\n");

	int count = snprintf(backup_path, PATH_MAX, "%s.backup", archive_path);
	if (count < 0) {
		DEBUG("Faild to create backup file path");
		goto error;
	}
	else if ((size_t)count >= PATH_MAX) {
		errno = ENAMETOOLONG;
		DEBUG("Faild to create backup file path");
		goto error;
	}

	if (access(backup_path, F_OK) == -1) {
		if (rename(archive_path, backup_path) < 0) {
			if (errno != EEXIST) {
				FDEBUG("%s: %s", backup_path, strerror(errno));
				goto error;
			}
		}
	}
	made_backup = true;

	printf("Patching game archive: %s\n", archive_path);

	backup = archive_read_new();
	if (backup == NULL) {
		PDEBUG();
		goto error;
	}

	if (archive_read_support_filter_all(backup) != ARCHIVE_OK) {
		PDEBUG();
		goto error;
	}

	if (archive_read_support_format_all(backup) != ARCHIVE_OK) {
		PDEBUG();
		goto error;
	}

	if (archive_read_open_filename(backup, backup_path, 10240) != ARCHIVE_OK) {
		FDEBUG("%s: %s", backup_path, archive_error_string(backup));
		goto error;
	}

	arch = archive_write_new();
	if (arch == NULL) {
		PDEBUG();
		goto error;
	}

	if (archive_write_set_format_zip(arch) != ARCHIVE_OK) {
		FDEBUG("%s: %s", backup_path, archive_error_string(backup));
		goto error;
	}

	if (archive_write_add_filter_none(arch) != ARCHIVE_OK) {
		FDEBUG("%s: %s", backup_path, archive_error_string(backup));
		goto error;
	}

	if (archive_write_open_filename(arch, archive_path) != ARCHIVE_OK) {
		FDEBUG("%s: %s", archive_path, archive_error_string(arch));
		goto error;
	}

	entry = archive_entry_new2(backup);
	if (entry == NULL) {
		PDEBUG();
		goto error;
	}

	while (archive_read_next_header2(backup, entry) == ARCHIVE_OK) {
		const char *path = archive_entry_pathname(entry);
		const struct patch_entry *found_patch = NULL;

		for (const struct patch_entry *patch_entry = patches; patch_entry->path; ++ patch_entry) {
			if (strcmp(patch_entry->path, path) == 0) {
				found_patch = patch_entry;
				break;
			}
		}

		if (found_patch) {
			printf("Replacing: %s\n", path);
			struct archive_entry *new_entry = archive_entry_new2(arch);
			if (new_entry == NULL) {
				PDEBUG();
				goto error;
			}

			archive_entry_copy_pathname(new_entry, path);
			archive_entry_set_perm(new_entry, archive_entry_perm(entry));
			archive_entry_set_filetype(new_entry, archive_entry_filetype(entry));
			archive_entry_set_mode(new_entry, archive_entry_mode(entry));
			archive_entry_set_size(new_entry, found_patch->size);
			archive_entry_set_uid(new_entry, archive_entry_uid(entry));
			archive_entry_set_gid(new_entry, archive_entry_gid(entry));
			archive_entry_set_mtime(new_entry, archive_entry_mtime(entry),
				archive_entry_mtime_nsec(entry));

			if (archive_write_header(arch, new_entry) != ARCHIVE_OK) {
				DEBUG(archive_error_string(arch));
				archive_entry_free(new_entry);
				goto error;
			}
			
			archive_entry_free(new_entry);

			if (archive_write_data(arch, found_patch->data, found_patch->size) < 0) {
				DEBUG(archive_error_string(arch));
				goto error;
			}
		}
		else {
			printf("Copying: %s\n", path);
			if (archive_write_header(arch, entry) != ARCHIVE_OK) {
				DEBUG(archive_error_string(arch));
				goto error;
			}

			size_t size = archive_entry_size(entry);
			if (buf_size < size) {
				char *new_buf = realloc(buf, size);
				if (!new_buf) {
					PDEBUG();
					goto error;
				}
				buf = new_buf;
				buf_size = size;
			}

			if (size > 0) {
				if (archive_read_data(backup, buf, size) < 0) {
					DEBUG(archive_error_string(backup));
					goto error;
				}

				if (archive_write_data(arch, buf, size) < 0) {
					DEBUG(archive_error_string(arch));
					goto error;
				}
			}
		}
	}

	goto end;

error:
	status = EXIT_FAILURE;

end:
	if (entry) {
		archive_entry_free(entry);
		entry = NULL;
	}

	if (backup) {
		if (archive_read_free(backup) != ARCHIVE_OK) {
			DEBUG(archive_error_string(backup));
		}
		backup = NULL;
	}

	if (arch) {
		if (archive_write_free(arch) != ARCHIVE_OK) {
			DEBUG(archive_error_string(arch));
		}
		arch = NULL;
	}

	if (buf) {
		free(buf);
		buf = NULL;
	}

	if (status == EXIT_SUCCESS) {
		printf("Successfully pached game.\n");
	}
	else if (made_backup) {
		printf("Restoring backup: %s\n", backup_path);
		if (unlink(archive_path) == -1 && errno != ENOENT) {
			FDEBUG("%s: %s", archive_path, strerror(errno));
		}
		else if (copyfile(backup_path, archive_path) == -1) {
			FDEBUG("%s: %s", archive_path, strerror(errno));
		}
	}

#ifdef STZ_WINDOWS
	printf("Press ENTER to continue...");
	getchar();
#endif

	return status;
}
