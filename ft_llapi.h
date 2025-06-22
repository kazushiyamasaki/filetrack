/*
 * ft_llapi.h -- interface for filetrack's low-level API group
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

#pragma once

#ifndef FT_LLAPI_H
#define FT_LLAPI_H


#include "filetrack.h"


#ifndef FILETRACK_DISABLE


MUTILS_CPP_C_BEGIN


#include <stdio.h>
#include <stdlib.h>


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
 * @param filename_len_max: maximum length of the filename, usually specified with FT_FILENAME_LEN_MAX
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function is called when a file is opened, such as with fopen, tmpfile, or freopen
 */
extern void filetrack_entry_add (FILE* stream, FileOpenType open_type, const char* filename, const char* mode, size_t filename_len_max, const char* file, int line);

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


MUTILS_CPP_C_END


#endif


#endif
