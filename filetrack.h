/*
 * filetrack.h -- interface of a library to assist with file-related debugging
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


#ifndef FILETRACK_DISABLE


#include "mhashtable.h"


MUTILS_CPP_C_BEGIN


#include <stdio.h>


/*
 * filetrack_errfunc is a global variable that stores the name of the function
 * where the most recent error occurred within this library.
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


#ifndef FT_FILENAME_LEN_MAX
	#define FT_FILENAME_LEN_MAX 1024
#endif


/*
 * Replaces fopen, tmpfile, freopen, fclose and remove with filetrack_ versions.
 */
#ifndef FILETRACK_DISABLE_REPLACE_STANDARD_FUNC
	#define fopen(filename, mode) filetrack_fopen((filename), (mode), FT_FILENAME_LEN_MAX, __FILE__, __LINE__)
	#define tmpfile() filetrack_tmpfile(__FILE__, __LINE__)
	#define freopen(filename, mode, stream) filetrack_freopen((filename), (mode), (stream), FT_FILENAME_LEN_MAX, __FILE__, __LINE__)
	#define fclose(stream) filetrack_fclose((stream), __FILE__, __LINE__)

	#ifdef DEBUG
		#define remove(filename) filetrack_remove((filename), FT_FILENAME_LEN_MAX, __FILE__, __LINE__)
	#endif
#endif



/*
 * filetrack_fopen
 * @param filename: name of the file to open
 * @param mode: mode in which to open the file (e.g., "r", "w", "a")
 * @param filename_len_max: maximum length of the filename, usually specified with FT_FILENAME_LEN_MAX
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the opened file stream, or NULL on failure
 */
extern FILE* filetrack_fopen (const char* filename, const char* mode, size_t filename_len_max, const char* file, int line);

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
 * @param filename_len_max: maximum length of the filename, usually specified with FT_FILENAME_LEN_MAX
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the reopened file stream, or NULL on failure
 */
extern FILE* filetrack_freopen (const char* filename, const char* mode, FILE* stream, size_t filename_len_max, const char* file, int line);

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
 * @param filename_len_max: maximum length of the filename, usually specified with FT_FILENAME_LEN_MAX
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: 0 on success, non-zero on failure
 */
extern int filetrack_remove (const char* filename, size_t filename_len_max, const char* file, int line);
#endif


/*
 * filetrack_all_check
 * @note: use the printf function to output all information stored in the file management hashtable during runtime
 */
extern void filetrack_all_check (void);


MUTILS_CPP_C_END


#endif


#endif
