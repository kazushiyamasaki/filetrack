/*
 * filetrack.c -- implementation part of a library to assist with file-related debugging
 * version 0.9.5, June 22, 2025
 *
 * License: zlib License
 *
 * Copyright (c) 2025 Kazushi Yamasaki
 *
 * This software is provided ‘as-is’, without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 */

#include "ft_llapi.h"

#define DEBUG
#ifndef FILETRACK_DISABLE


#include <string.h>
#include <errno.h>


#if !defined (__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
	#error "This program requires C99 or higher."
#endif


#if defined (_WIN32) && (!defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600))
	#error "This program requires Windows Vista or later. Define _WIN32_WINNT accordingly."
#endif


#if defined (__unix__) || defined (__linux__) || defined (__APPLE__)
	#include <unistd.h>
#endif

#if (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || defined (_POSIX_VERSION) || defined (__linux__) || defined (__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__DragonFly__)
	#define MAYBE_ERRNO_THREAD_LOCAL
#endif


#undef fopen
#undef freopen
#undef tmpfile
#undef fclose


#define FILETRACK_ENTRIES_COUNT 64
#define FILETRACK_ENTRIES_TRIAL 4

#define FT_MODE_LEN_MAX 16


static const char* FileOpenTypeNames[] = {
	"not_open",
	"fopen",
	"tmpfile",
	"freopen",
	"unknown"
};


static const char* FileClosedTypeNames[] = {
	"not_closed",
	"fclose",
	"freopen",
	"unknown"
};


typedef struct {
	FILE* stream;
#ifdef DEBUG
	char* filename;
	char* mode;
	const char* open_file;
	const char* last_change_mode_file;
	const char* close_file;
	int open_line;
	int last_change_mode_line;
	int close_line;
	FileOpenType open_type;
	FileClosedType closed_type;
	bool is_closed;
#endif
} FileTrackEntry;  /* パディングの削減のため順序がわかりにくくなっているので注意 */


#ifdef DEBUG
typedef struct {
	char* filename;
	FILE* stream;
} FilenameStreamEntry;
#endif


/* errno 記録時に関数名を記録する */
#ifdef THREAD_LOCAL
	THREAD_LOCAL const char* filetrack_errfunc = NULL;
#else
	const char* filetrack_errfunc = NULL;  /* 非スレッドセーフ */
#endif


static MHashTable* filetrack_entries = NULL;

#ifdef DEBUG
	static MHashTable* filename_stream_entries = NULL;
#endif


#define GLOBAL_LOCK_FUNC_NAME filetrack_lock
#define GLOBAL_UNLOCK_FUNC_NAME filetrack_unlock
#define GLOBAL_LOCK_FUNC_SCOPE 

#include "global_lock.h"


static void quit (void);

/* 重要: この関数は必ずロックした後に呼び出す必要があります！ */
static void init (void) {
	for (size_t i = 0; i < FILETRACK_ENTRIES_TRIAL; i++) {
		filetrack_entries = mht_uint_create(FILETRACK_ENTRIES_COUNT);
		if (LIKELY(filetrack_entries != NULL)) break;
	}
	if (UNLIKELY(filetrack_entries == NULL)) {
		fprintf(stderr, "Failed to initialize file tracking.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		filetrack_unlock();
		global_lock_quit();
		exit(EXIT_FAILURE);
	}

	atexit(quit);

#ifdef DEBUG
	filename_stream_entries = mht_str_create(FILETRACK_ENTRIES_COUNT);
	if (UNLIKELY(filename_stream_entries == NULL)) {
		filetrack_errfunc = "init";
	}
#endif
}


void filetrack_entry_add (FILE* stream, FileOpenType open_type, const char* filename, const char* mode, size_t filename_len_max, const char* file, int line) {
	if (stream == NULL) {
		fprintf(stderr, "stream is null! File cannot be tracked!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_entry_add";
		return;
	}

	if (UNLIKELY(filetrack_entries == NULL)) init();

#ifdef DEBUG
	if (filename_len_max < 1) {
		fprintf(stderr, "filename_len_max must be at least 1.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_entry_add";
		return;
	}

	char* filename_cpy = mutils_strndup(filename, filename_len_max);
	if (UNLIKELY(filename_cpy == NULL)) {
		fprintf(stderr, "Failed to duplicate filename string.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_entry_add";
	}

	char* mode_cpy = mutils_strndup(mode, FT_MODE_LEN_MAX);
	if (UNLIKELY(mode_cpy == NULL)) {
		fprintf(stderr, "Failed to duplicate mode string.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_entry_add";
	}
#endif

	FileTrackEntry entry = {
		.stream = stream
#ifdef DEBUG
		, 
		.filename = filename_cpy,
		.mode = mode_cpy,
		.open_type = open_type,
		.open_file = file,
		.open_line = line,
		.last_change_mode_file = NULL,
		.last_change_mode_line = 0,
		.is_closed = false,
		.closed_type = FILE_NOT_CLOSED,
		.close_file = NULL,
		.close_line = 0
#endif
	};

	if (UNLIKELY(!mht_uint_set(filetrack_entries, (uint_keyt)stream, &entry, sizeof(FileTrackEntry)))) {
		fprintf(stderr, "Failed to add entry to file tracking.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_entry_add";
#ifdef DEBUG
		free(filename_cpy);
		free(mode_cpy);
#endif
		return;
	}

#ifdef DEBUG
	/* tmpfile はファイル名不明なので記録しない */
	if (strncmp(mode_cpy, "(tmpfile)", 10) == 0) return;

	FilenameStreamEntry filename_stream_entry = {
		.filename = filename_cpy,
		.stream = stream
	};

	if (UNLIKELY(!mht_str_set(filename_stream_entries, filename_cpy, &filename_stream_entry, sizeof(FilenameStreamEntry)))) {
		filetrack_errfunc = "filetrack_entry_add";
	}
#endif
}


/* 注意: filename == NULL で freopen を使った場合にのみ呼び出す */
void filetrack_entry_update (FILE* stream, const char* filename, const char* mode, const char* file, int line) {
	if (filename != NULL) {
		fprintf(stderr, "filename must be NULL when updating mode with freopen!\nFile: %s   Line: %d\n", file, line);
		filetrack_unlock();
		exit(EXIT_FAILURE);
	}

	if (stream == NULL) {
		fprintf(stderr, "stream is null! File cannot be closed!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_entry_update";
		return;
	}

	if (UNLIKELY(filetrack_entries == NULL)) {
		init();

		fprintf(stderr, "No entry found to close! The file might not be tracked.\nFile: %s   Line: %d\n", file, line);
		errno = EPERM;
		filetrack_errfunc = "filetrack_entry_update";

		filetrack_entry_add(stream, FILE_OPEN_UNKNOWN, "unknown", mode, 8, file, line);
		return;
	}

	FileTrackEntry* entry = mht_uint_get(filetrack_entries, (uint_keyt)stream);
	if (entry == NULL) {
		fprintf(stderr, "No entry found to close! The file might not be tracked.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_entry_update";

		filetrack_entry_add(stream, FILE_OPEN_UNKNOWN, "unknown", mode, 8, file, line);
		return;
	}

#ifdef DEBUG
	free(entry->mode);

	char* mode_cpy = mutils_strndup(mode, FT_MODE_LEN_MAX);
	if (UNLIKELY(mode_cpy == NULL)) {
		fprintf(stderr, "Failed to duplicate mode string.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_entry_update";
	}

	entry->mode = mode_cpy;
	entry->last_change_mode_file = file;
	entry->last_change_mode_line = line;
#endif
}


void filetrack_entry_close (FILE* stream, FileClosedType closed_type, const char* file, int line) {
	if (stream == NULL) {
		fprintf(stderr, "stream is null! File cannot be closed!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_entry_close";
		return;
	}

	if (UNLIKELY(filetrack_entries == NULL)) {
		init();

		fprintf(stderr, "No entry found to close! The file might not be tracked.\nFile: %s   Line: %d\n", file, line);
		errno = EPERM;
		filetrack_errfunc = "filetrack_entry_close";
		return;
	}

	FileTrackEntry* entry = mht_uint_get(filetrack_entries, (uint_keyt)stream);
	if (entry == NULL) {
		fprintf(stderr, "No entry found to close! The file might not be tracked.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_entry_close";
		return;
	}

#ifndef DEBUG
	if (!mht_uint_delete(filetrack_entries, (uint_keyt)stream))
		filetrack_errfunc = "filetrack_entry_close";
#else
	entry->is_closed = true;
	entry->closed_type = closed_type;
	entry->close_file = file;
	entry->close_line = line;
#endif
}


static FILE* filetrack_fopen_without_lock (const char* filename, const char* mode, size_t filename_len_max, const char* file, int line) {
	if (filename == NULL) {
		fprintf(stderr, "No processing was done because the filename is NULL!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fopen";
		return NULL;
	}

	if (filename[0] == '\0') {
		fprintf(stderr, "No processing was done because the filename is empty.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fopen";
		return NULL;
	}

	if (mode == NULL) {
		fprintf(stderr, "No processing was done because the mode is NULL!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fopen";
		return NULL;
	}

	if (mode[0] == '\0') {
		fprintf(stderr, "No processing was done because the mode is empty.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fopen";
		return NULL;
	}

	if (filename_len_max < 1) {
		fprintf(stderr, "filename_len_max must be at least 1.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fopen";
		return;
	}

	char* filename_tmp = mutils_strndup(filename, filename_len_max);
	if (filename_tmp == NULL) {
		filetrack_errfunc = "filetrack_fopen";
		return NULL;
	}

	char* mode_tmp = mutils_strndup(mode, FT_MODE_LEN_MAX);
	if (mode_tmp == NULL) {
		free(filename_tmp);
		filetrack_errfunc = "filetrack_fopen";
		return NULL;
	}

	FILE* stream = fopen(filename_tmp, mode_tmp);

	free(filename_tmp);  /* ヌル終端を保証するためだけなのですぐに解放 */
	free(mode_tmp);

	if (UNLIKELY(stream == NULL)) {
		fprintf(stderr, "Failed to open file '%s' with mode '%s'.\nFile: %s   Line: %d\n", filename, mode, file, line);
		filetrack_errfunc = "filetrack_fopen";
	} else {
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		int tmp_errno = errno;
#endif
		errno = 0;

		filetrack_entry_add(stream, FILE_OPEN_FOPEN, filename, mode, filename_len_max, file, line);

		if (UNLIKELY(errno != 0)) filetrack_errfunc = "filetrack_fopen";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		else errno = tmp_errno;
#endif
	}
	return stream;
}


FILE* filetrack_fopen (const char* filename, const char* mode, size_t filename_len_max, const char* file, int line) {
	filetrack_lock();
	FILE* stream = filetrack_fopen_without_lock(filename, mode, filename_len_max, file, line);
	filetrack_unlock();
	return stream;
}


static FILE* filetrack_tmpfile_without_lock (const char* file, int line) {
	FILE* stream = tmpfile();
	if (UNLIKELY(stream == NULL)) {
		fprintf(stderr, "Failed to create a temporary file.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_tmpfile";
	} else {
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		int tmp_errno = errno;
#endif
		errno = 0;

		filetrack_entry_add(stream, FILE_OPEN_TMPFILE, "unknown", "(tmpfile)", 8, file, line);

		if (UNLIKELY(errno != 0)) filetrack_errfunc = "filetrack_tmpfile";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		else errno = tmp_errno;
#endif
	}
	return stream;
}


FILE* filetrack_tmpfile (const char* file, int line) {
	filetrack_lock();
	FILE* stream = filetrack_tmpfile_without_lock(file, line);
	filetrack_unlock();
	return stream;
}


static FILE* filetrack_freopen_without_lock (const char* filename, const char* mode, FILE* stream, size_t filename_len_max, const char* file, int line) {
	if (filename != NULL && filename[0] == '\0') {
		fprintf(stderr, "No processing was done because the filename is empty.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_freopen";
		return NULL;
	}

	if (mode == NULL) {
		fprintf(stderr, "No processing was done because the mode is NULL!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_freopen";
		return NULL;
	}

	if (mode[0] == '\0') {
		fprintf(stderr, "No processing was done because the mode is empty.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_freopen";
		return NULL;
	}

	if (stream == NULL) {
		fprintf(stderr, "No processing was done because the stream is NULL!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_freopen";
		return NULL;
	}

	if (filename_len_max < 1) {
		fprintf(stderr, "filename_len_max must be at least 1.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_freopen";
		return;
	}

	char* filename_tmp = mutils_strndup(filename, filename_len_max);
	if (filename_tmp == NULL) {
		filetrack_errfunc = "filetrack_freopen";
		return NULL;
	}

	char* mode_tmp = mutils_strndup(mode, FT_MODE_LEN_MAX);
	if (mode_tmp == NULL) {
		free(filename_tmp);
		filetrack_errfunc = "filetrack_freopen";
		return NULL;
	}

	FILE* new_stream = freopen(filename_tmp, mode_tmp, stream);

	free(filename_tmp);  /* ヌル終端を保証するためだけなのですぐに解放 */
	free(mode_tmp);

	if (UNLIKELY(new_stream == NULL)) {
		fprintf(stderr, "Failed to reopen file '%s' with mode '%s'.\nFile: %s   Line: %d\n", filename, mode, file, line);
		filetrack_errfunc = "filetrack_freopen";
		filetrack_entry_close(stream, FILE_CLOSED_FREOPEN, file, line);
		return NULL;
	}

	if (stream == stdin || stream == stdout || stream == stderr)
		return new_stream;   /* 標準ストリームは管理対象外 */

	if (filename == NULL) {  /* モード変更の場合 */
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		int tmp_errno = errno;
#endif
		errno = 0;

		filetrack_entry_update(new_stream, filename, mode, file, line);

		if (UNLIKELY(errno != 0)) filetrack_errfunc = "filetrack_freopen";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		else errno = tmp_errno;
#endif
	} else {                 /* 閉じて開き直す場合 */
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		int tmp_errno = errno;
#endif
		errno = 0;

		filetrack_entry_close(stream, FILE_CLOSED_FREOPEN, file, line);

		if (UNLIKELY(errno != 0)) filetrack_errfunc = "filetrack_freopen";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		else errno = tmp_errno;

		tmp_errno = errno;
#endif
		errno = 0;

		filetrack_entry_add(new_stream, FILE_OPEN_FREOPEN, filename, mode, filename_len_max, file, line);

		if (UNLIKELY(errno != 0)) filetrack_errfunc = "filetrack_freopen";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		else errno = tmp_errno;
#endif
	}
	return new_stream;
}


FILE* filetrack_freopen (const char* filename, const char* mode, FILE* stream, size_t filename_len_max, const char* file, int line) {
	filetrack_lock();
	FILE* new_stream = filetrack_freopen_without_lock(filename, mode, stream, filename_len_max, file, line);
	filetrack_unlock();
	return new_stream;
}


static int filetrack_fclose_without_lock (FILE* stream, const char* file, int line) {
	if (stream == NULL) {
		fprintf(stderr, "No processing was done because the stream is NULL!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fclose";
		return EOF;
	}

	if (stream == stdin) {
		fprintf(stderr, "Cannot close stdin stream! Because it is a standard input stream.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fclose";
		return EOF;
	} else if (stream == stdout) {
		fprintf(stderr, "Cannot close stdout stream! Because it is a standard output stream.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fclose";
		return EOF;
	} else if (stream == stderr) {
		fprintf(stderr, "Cannot close stderr stream! Because it is a standard error stream.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fclose";
		return EOF;
	}

	int return_value = 0;

#ifdef DEBUG
	if (UNLIKELY(filetrack_entries == NULL)) {
		init();

		fprintf(stderr, "No entry found to close! The file might not be tracked.\nFile: %s   Line: %d\n", file, line);
		errno = EPERM;
		filetrack_errfunc = "filetrack_fclose";

		return_value = fclose(stream);
		if (return_value != 0) {
			fprintf(stderr, "Failed to close file stream!\nFile: %s   Line: %d\n", file, line);
			filetrack_errfunc = "filetrack_fclose";
		}

		return return_value;
	}

	FileTrackEntry* entry = mht_uint_get(filetrack_entries, (uint_keyt)stream);
	if (entry == NULL) {
		fprintf(stderr, "No entry found to close! The file might not be tracked.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_fclose";

		return_value = fclose(stream);
		if (return_value != 0) {
			fprintf(stderr, "Failed to close file stream!\nFile: %s   Line: %d\n", file, line);
			filetrack_errfunc = "filetrack_fclose";
		}

		return return_value;
	}

	if (entry->is_closed) {
		fprintf(stderr, "File already closed!\nreclose File: %s   Line: %d\nclose File: %s   Line: %d\n", file, line, entry->close_file, entry->close_line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_fclose";
		return EOF;
	}
#endif

	return_value = fclose(stream);
	if (return_value != 0) {
		fprintf(stderr, "Failed to close file stream!\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_fclose";
	}

#ifdef MAYBE_ERRNO_THREAD_LOCAL
	int tmp_errno = errno;
#endif
	errno = 0;

	filetrack_entry_close(stream, FILE_CLOSED_FCLOSE, file, line);

	if (UNLIKELY(errno != 0)) filetrack_errfunc = "filetrack_fclose";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
	else errno = tmp_errno;
#endif

	return return_value;
}


int filetrack_fclose (FILE* stream, const char* file, int line) {
	filetrack_lock();
	int return_value = filetrack_fclose_without_lock(stream, file, line);
	filetrack_unlock();
	return return_value;
}


#ifdef DEBUG
static bool can_be_removed_check (const char* filename, size_t filename_len_max, const char* file, int line) {
	size_t filename_len = mutils_strnlen(filename, filename_len_max);
	if (UNLIKELY(filename_len == 0)) {  /* ファイル名の長さの取得に失敗した場合 */
		fprintf(stderr, "Failed to retrieve the filename length.\nFile: %s   Line: %d\n", file, line);
		filetrack_errfunc = "filetrack_remove";
		return true;  /* エラーではあるが続行 */
	}

	str_keyt filename_key = {
		.ptr = filename,
		.len = filename_len
	};

	FilenameStreamEntry* filename_stream_entry = mht_str_get(filename_stream_entries, filename_key);
	if (filename_stream_entry == NULL) return true;  /* 同じファイル名が記録されていない場合 */

	FileTrackEntry* entry = mht_uint_get(filetrack_entries, (uint_keyt)filename_stream_entry->stream);
	if (UNLIKELY(entry == NULL)) {  /* 構造が破損してエントリが見つからない場合 */
		fprintf(stderr, "The filename_stream_entry corresponding to file '%s' exists, but no corresponding entry is found.\nFile: %s   Line: %d\n", filename, file, line);
		errno = EPROTO;
		filetrack_errfunc = "filetrack_remove";
		return true;  /* エラーではあるが続行 */
	}

	if (entry->is_closed) return true;  /* 過去に開かれていたが既にクローズされている場合 */

	/* 削除しようとしたファイルがまだオープンされている場合 */
	fprintf(stderr, "File '%s' is still open and cannot be removed.\nFile: %s   Line: %d\n", filename, file, line);
	errno = EINVAL;
	filetrack_errfunc = "filetrack_remove";
	return false;
}


int filetrack_remove (const char* filename, size_t filename_len_max, const char* file, int line) {
	if (filename == NULL) {
		fprintf(stderr, "No processing was done because the filename is NULL!\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_remove";
		return 1;
	}

	if (filename[0] == '\0') {
		fprintf(stderr, "No processing was done because the filename is empty.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_remove";
		return 1;
	}

	if (filename_len_max < 1) {
		fprintf(stderr, "filename_len_max must be at least 1.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		filetrack_errfunc = "filetrack_remove";
		return 1;
	}

	if (filename_stream_entries != NULL) {
		if (!can_be_removed_check(filename, filename_len_max, file, line))
			return 1;  /* 削除不可を確認した場合、エラーを返して終了 */
	}

	int result = remove(filename);
	if (result != 0)
		filetrack_errfunc = "filetrack_remove";
	return result;
}
#endif


void filetrack_all_check (void) {
	size_t filetrack_entries_arr_cnt;
	FileTrackEntry** filetrack_entries_arr = (FileTrackEntry**)mht_all_get(filetrack_entries, &filetrack_entries_arr_cnt);
	if (UNLIKELY(filetrack_entries_arr == NULL)) {
		fprintf(stderr, "Failed to get all entries from file tracking.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		filetrack_errfunc = "filetrack_all_check";

	} else {
		printf("\n");
		for (size_t i = 0; i < filetrack_entries_arr_cnt; i++) {
			if (UNLIKELY(filetrack_entries_arr[i] == NULL)) {
				fprintf(stderr, "Entry is NULL!\nFile: %s   Line: %d\n", __FILE__, __LINE__);
				errno = EPROTO;
				filetrack_errfunc = "filetrack_all_check";
			} else if (UNLIKELY(filetrack_entries_arr[i]->stream == NULL)) {
				fprintf(stderr, "Entry stream is NULL!\nFile: %s   Line: %d\n", __FILE__, __LINE__);
				errno = EPROTO;
				filetrack_errfunc = "filetrack_all_check";
			} else {
#ifndef DEBUG
				printf("\nAlready Closed: false   Stream: %p\nPlease use debug mode if you need more detailed information.\n", filetrack_entries_arr[i]->stream);
#else
				if (filetrack_entries_arr[i]->is_closed) {
					if (filetrack_entries_arr[i]->last_change_mode_file != NULL)
						printf("\nAlready Closed: true\nStream: %p   Mode: %s\nFile Name: %s\nclosed Type: %s\nclose File: %s   Line: %d\nopen Type: %s\nopen File: %s   Line: %d\nLast change mode File: %s   Line: %d\n", filetrack_entries_arr[i]->stream, filetrack_entries_arr[i]->mode, filetrack_entries_arr[i]->filename, FileClosedTypeNames[filetrack_entries_arr[i]->closed_type], filetrack_entries_arr[i]->close_file, filetrack_entries_arr[i]->close_line, FileOpenTypeNames[filetrack_entries_arr[i]->open_type], filetrack_entries_arr[i]->open_file, filetrack_entries_arr[i]->open_line, filetrack_entries_arr[i]->last_change_mode_file, filetrack_entries_arr[i]->last_change_mode_line);
					else
						printf("\nAlready Closed: true\nStream: %p   Mode: %s\nFile Name: %s\nclosed Type: %s\nclose File: %s   Line: %d\nopen Type: %s\nopen File: %s   Line: %d\n", filetrack_entries_arr[i]->stream, filetrack_entries_arr[i]->mode, filetrack_entries_arr[i]->filename, FileClosedTypeNames[filetrack_entries_arr[i]->closed_type], filetrack_entries_arr[i]->close_file, filetrack_entries_arr[i]->close_line, FileOpenTypeNames[filetrack_entries_arr[i]->open_type], filetrack_entries_arr[i]->open_file, filetrack_entries_arr[i]->open_line);
				} else {
					if (filetrack_entries_arr[i]->last_change_mode_file != NULL)
						printf("\nAlready Closed: false\nStream: %p   Mode: %s\nFile Name: %s\nopen Type: %s\nopen File: %s   Line: %d\nLast change mode File: %s   Line: %d\n", filetrack_entries_arr[i]->stream, filetrack_entries_arr[i]->mode, filetrack_entries_arr[i]->filename, FileOpenTypeNames[filetrack_entries_arr[i]->open_type], filetrack_entries_arr[i]->open_file, filetrack_entries_arr[i]->open_line, filetrack_entries_arr[i]->last_change_mode_file, filetrack_entries_arr[i]->last_change_mode_line);
					else
						printf("\nAlready Closed: false\nStream: %p   Mode: %s\nFile Name: %s\nopen Type: %s\nopen File: %s   Line: %d\n", filetrack_entries_arr[i]->stream, filetrack_entries_arr[i]->mode, filetrack_entries_arr[i]->filename, FileOpenTypeNames[filetrack_entries_arr[i]->open_type], filetrack_entries_arr[i]->open_file, filetrack_entries_arr[i]->open_line);
				}
#endif
			}
		}
		if (!mht_all_release_arr(filetrack_entries_arr))
			filetrack_errfunc = "filetrack_all_check";

		printf("\n\n");
	}
}


static void quit (void) {
	size_t filetrack_entries_arr_cnt;
	FileTrackEntry** filetrack_entries_arr;
	for (size_t i = 0; i < FILETRACK_ENTRIES_TRIAL; i++) {
		filetrack_entries_arr = (FileTrackEntry**)mht_all_get(filetrack_entries, &filetrack_entries_arr_cnt);
		if (LIKELY(filetrack_entries_arr != NULL)) break;
	}
	if (UNLIKELY(filetrack_entries_arr == NULL)) {
		fprintf(stderr, "Failed to get all entries from file tracking.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		filetrack_errfunc = "quit";

	} else {
		for (size_t i = 0; i < filetrack_entries_arr_cnt; i++) {
			if (UNLIKELY(filetrack_entries_arr[i] == NULL)) {
				fprintf(stderr, "Entry is NULL!\nFile: %s   Line: %d\n", __FILE__, __LINE__);
				errno = EPROTO;
				filetrack_errfunc = "quit";
			} else if (UNLIKELY(filetrack_entries_arr[i]->stream == NULL)) {
				fprintf(stderr, "Entry stream is NULL!\nFile: %s   Line: %d\n", __FILE__, __LINE__);
				errno = EPROTO;
				filetrack_errfunc = "quit";
#ifdef DEBUG
				if (UNLIKELY(filetrack_entries_arr[i]->filename != NULL))
					free(filetrack_entries_arr[i]->filename);
				if (UNLIKELY(filetrack_entries_arr[i]->mode != NULL))
					free(filetrack_entries_arr[i]->mode);
#endif
			} else {
#ifndef DEBUG
				if (filetrack_fclose_without_lock(filetrack_entries_arr[i]->stream, __FILE__, __LINE__) != 0)
					filetrack_errfunc = "quit";
#else
				if (UNLIKELY(!filetrack_entries_arr[i]->is_closed)) {
					fprintf(stderr, "\nFile not closed!\nStream: %p   Mode: %s\nFile Name: %s\nopen Type: %s\nopen File: %s   Line: %d\nLast change mode File: %s   Line: %d\n", filetrack_entries_arr[i]->stream, filetrack_entries_arr[i]->mode, filetrack_entries_arr[i]->filename, FileClosedTypeNames[filetrack_entries_arr[i]->open_type], filetrack_entries_arr[i]->open_file, filetrack_entries_arr[i]->open_line, filetrack_entries_arr[i]->last_change_mode_file, filetrack_entries_arr[i]->last_change_mode_line);
					errno = EPERM;

					filetrack_fclose_without_lock(filetrack_entries_arr[i]->stream, __FILE__, __LINE__);

					filetrack_errfunc = "quit";
				}
				free(filetrack_entries_arr[i]->filename);
				free(filetrack_entries_arr[i]->mode);
#endif
			}
		}
		if (!mht_all_release_arr(filetrack_entries_arr))
			filetrack_errfunc = "quit";
	}
#ifdef MAYBE_ERRNO_THREAD_LOCAL
	int tmp_errno = errno;
#endif
	errno = 0;

	mht_destroy(filetrack_entries);

	if (UNLIKELY(errno != 0)) filetrack_errfunc = "quit";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
	else errno = tmp_errno;
#endif

	filetrack_entries = NULL;

#ifdef DEBUG
	if (filename_stream_entries != NULL) {
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		int tmp_errno = errno;
#endif
		errno = 0;

		mht_destroy(filename_stream_entries);

		if (UNLIKELY(errno != 0)) filetrack_errfunc = "quit";
#ifdef MAYBE_ERRNO_THREAD_LOCAL
		else errno = tmp_errno;
#endif
		filename_stream_entries = NULL;
	}
#endif

	global_lock_quit();
}


#endif
