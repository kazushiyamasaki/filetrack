/*
 * filetrack.h -- interface of a library to assist with file-related debugging
 * version 0.9.3, June 15, 2025
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
 *
 *
 *
 * IMPORTANT:
 * This header replaces fopen, tmpfile, freopen, fclose and remove with file-tracking
 * versions using macros. To avoid unintended side effects in other headers, make sure to
 * include this header after all standard or third-party headers.
 *
 * BAD (may break other code):
 *     #include "filetrack.h"
 *     #include <some_lib.h>
 *
 * GOOD:
 *     #include <some_lib.h>
 *     #include "filetrack.h"
 *
 * The functions fopen, tmpfile, freopen, fclose and remove are replaced by macros and
 * therefore cannot be passed to function pointers. If you need to use these functions
 * with function pointers, please use the actual functions they expand to, which are
 * prefixed with filetrack_.
 * 
 * You can disable the replacement of fopen, tmpfile, freopen, fclose and remove
 * functions by defining the FILETRACK_DISABLE_REPLACE_STANDARD_FUNC macro before
 * including this file.
 *
 *
 * Note:
 * This library uses a global lock to ensure thread safety. As a result, performance
 * may degrade significantly when accessed concurrently by many threads.
 * In high-load environments or those with many threads, it is recommended to design
 * your application to minimize simultaneous access whenever possible.
 *
 * To enable debug mode, define DEBUG macro before including this file.
 *
 * This library depends on the mhashtable library.
 */

#pragma once

#ifndef FILETRACK_H
#define FILETRACK_H


#include "mhashtable.h"


MHT_CPP_C_BEGIN


#include <stdio.h>
#include <stdlib.h>


#if defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
	#define THREAD_LOCAL _Thread_local
#elif defined (__GNUC__)
	#define THREAD_LOCAL __thread
#elif defined (_MSC_VER)
	#define THREAD_LOCAL __declspec(thread)
#endif


/*
 * filetrack_errfunc is a global variable that stores the name of the function
 * where the most recent error occurred within the hash table library.
 *
 * It is set to NULL when no error has occurred.
 * This variable is used to provide more informative error diagnostics,
 * especially in combination with errno.
 *
 * It is recommended to check this variable and errno after calling
 * any library function that may fail.
 */
#ifdef THREAD_LOCAL
	extern THREAD_LOCAL const char* filetrack_errfunc;
#else
	extern const char* filetrack_errfunc;
#endif


#ifndef FILETRACK_DISABLE


/*
 * Replaces fopen, tmpfile, freopen, fclose and remove with filetrack_ versions.
 */
#ifndef FILETRACK_DISABLE_REPLACE_STANDARD_FUNC
	#define fopen(filename, mode) filetrack_fopen((filename), (mode), __FILE__, __LINE__)
	#define tmpfile() filetrack_tmpfile(__FILE__, __LINE__)
	#define freopen(filename, mode, stream) filetrack_freopen((filename), (mode), (stream), __FILE__, __LINE__)
	#define fclose(stream) filetrack_fclose((stream), __FILE__, __LINE__)

	#ifdef DEBUG
		#define remove(filename) filetrack_remove((filename), __FILE__, __LINE__)
	#endif
#endif



/*
 * filetrack_fopen
 * @param filename: name of the file to open
 * @param mode: mode in which to open the file (e.g., "r", "w", "a")
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the opened file stream, or NULL on failure
 */
extern FILE* filetrack_fopen (const char* filename, const char* mode, const char* file, int line);

/*
 * filetrack_tmpfile
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the temporary file stream, or NULL on failure
 */
extern FILE* filetrack_tmpfile (const char* file, int line);

/*
 * filetrack_freopen
 * @param filename: name of the file to reopen, or NULL to change the mode of the existing stream
 * @param mode: mode in which to reopen the file (e.g., "r", "w", "a")
 * @param stream: the file stream to reopen
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the reopened file stream, or NULL on failure
 */
extern FILE* filetrack_freopen (const char* filename, const char* mode, FILE* stream, const char* file, int line);

/*
 * filetrack_fclose
 * @param stream: the file stream to close
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: 0 on success, EOF on failure
 */
extern int filetrack_fclose (FILE* stream, const char* file, int line);

#ifdef DEBUG
/*
 * filetrack_remove
 * @param filename: name of the file to remove
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: 0 on success, non-zero on failure
 */
extern int filetrack_remove (const char* filename, const char* file, int line);
#endif


/*
 * filetrack_all_check
 * @note: use the printf function to output all information stored in the file management hashtable during runtime
 */
extern void filetrack_all_check (void);


#endif  /* 以下は FILETRACK_DISABLE が定義されていても有効 */

/*
 * The following function is not part of this library's original purpose, but we ended up
 * creating one that is generally useful during development, so we've decided to make it
 * publicly available.
 */

/*
 * filetrack_strndup
 * @param string: the string to duplicate
 * @param max_bytes: the maximum number of bytes to copy from the string
 * @return: a newly allocated string that is a duplicate of the input string, or NULL on failure
 * @note: the strings duplicated by this function must be freed with free()
 */
extern char* filetrack_strndup (const char* string, size_t max_bytes);

#ifndef FILETRACK_DISABLE



/*
 * If you want to manipulate file tracking entries, you can use the functions below,
 * but it's not recommended unless you're creating a wrapper for a function that needs
 * to be freed with free().
 */


/*
 * You must not use functions declared before this lock/unlock function within the
 * lock/unlock block.
 *
 * For functions declared after this one, it is recommended to wrap their usage—along
 * with related logic—within the lock/unlock block.
 */

/*
 * filetrack_lock
 * @note: this function locks the file tracking system to prevent concurrent access
 */
extern void filetrack_lock (void);

/*
 * filetrack_unlock
 * @note: this function unlocks the file tracking system to allow access
 */
extern void filetrack_unlock (void);


typedef enum {
	FILE_NOT_OPEN,
	FILE_OPEN_FOPEN,
	FILE_OPEN_TMPFILE,
	FILE_OPEN_FREOPEN,
	FILE_OPEN_UNKNOWN
} FileOpenType;


typedef enum {
	FILE_NOT_CLOSED,
	FILE_CLOSED_FCLOSE,
	FILE_CLOSED_FREOPEN,
	FILE_CLOSED_UNKNOWN
} FileClosedType;


/*
 * filetrack_entry_add
 * @param stream: the file stream to track
 * @param open_type: the type of file opening (e.g., FILE_OPEN_FOPEN, FILE_OPEN_TMPFILE, FILE_OPEN_FREOPEN)
 * @param filename: the name of the file being opened
 * @param mode: the mode in which the file is opened (e.g., "r", "w", "a")
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function is called when a file is opened, such as with fopen, tmpfile, or freopen
 */
extern void filetrack_entry_add (FILE* stream, FileOpenType open_type, const char* filename, const char* mode, const char* file, int line);

/*
 * filetrack_entry_update
 * @param stream: the file stream to update
 * @param filename: the name of the file being updated must always be NULL
 * @param mode: the new mode in which the file is opened (e.g., "r", "w", "a")
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function is called when a file's mode is changed, such as when using freopen with filename as NULL
 */
extern void filetrack_entry_update (FILE* stream, const char* filename, const char* mode, const char* file, int line);

/*
 * filetrack_entry_close
 * @param stream: the file stream to close
 * @param closed_type: the type of file closing (e.g., FILE_CLOSED_FCLOSE, FILE_CLOSED_FREOPEN)
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function is called when a file is closed, either by fclose or freopen
 */
extern void filetrack_entry_close (FILE* stream, FileClosedType closed_type, const char* file, int line);


#endif


MHT_CPP_C_END


#endif
