#ifndef WCX_H
#define WCX_H

#include <stdint.h>

typedef uintptr_t wcx_handle_t;

enum class wcx_status_t: uint32_t
{
	success,

	end_archive = 10,
	no_memory = 11,
	bad_data = 12,
	bad_archive = 13,
	unknown_format = 14,
	eopen = 15,
	ecreate = 16,
	eclose = 17,
	eread = 18,
	ewrite = 19,
	small_buf = 20,
	eaborted = 21,
	no_files = 22,
	too_many_files = 23,
	not_supported = 24,
};

enum class wcx_process_op_t: uint32_t
{
	skip = 0,
	test = 1,
	extract = 2,
};

typedef wcx_status_t (__stdcall change_volume_proc_t)(char const * name, uint32_t mode);
typedef wcx_status_t (__stdcall process_data_proc_t)(char const * filename, uint32_t size);

struct open_archive_data
{
	char const * archive_name;
	int open_mode;
	wcx_status_t open_result;

	char * CmtBuf;
	int CmtBufSize;
	int CmtSize;
	int CmtState;
};

struct header_data
{
	char archive_name[260];
	char file_name[260];
	int flags;
	int packed_size;
	int unpacked_size;

	int HostOS;
	int FileCRC;
	int FileTime;
	int UnpVer;
	int Method;
	int FileAttr;
	char* CmtBuf;
	int CmtBufSize;
	int CmtSize;
	int CmtState;
};

#endif // WCX_H
