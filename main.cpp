#include "wcx.h"
#include "pdbstream.h"
#include <vector>
#include <algorithm>
#include <fstream>

#include <Windows.h>

struct mapped_file
	: pdb::file
{
	mapped_file()
		: m_hFile(0), m_hMapping(0), m_view(0)
	{
	}

	~mapped_file()
	{
		if (m_view)
			UnmapViewOfFile(m_view);

		if (m_hMapping)
			CloseHandle(m_hMapping);

		if (m_hFile)
			CloseHandle(m_hFile);
	}

	void open(char const * path)
	{
		m_hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, 0);
		if (!m_hFile)
			throw std::runtime_error("failed to open file");

		LARGE_INTEGER li;
		if (!GetFileSizeEx(m_hFile, &li))
			throw std::runtime_error("failed to get the file size");
		if (li.HighPart != 0)
			throw std::runtime_error("file's too large");
		m_file_size = li.LowPart;

		m_hMapping = CreateFileMapping(m_hFile, nullptr, PAGE_READONLY, 0, m_file_size, nullptr);
		if (!m_hMapping)
			throw std::runtime_error("failed to create mapping");

		m_view = MapViewOfFile(m_hMapping, FILE_MAP_READ, 0, 0, m_file_size);
		if (!m_view)
			throw std::runtime_error("failed to map a view");
	}

	pdb::buffer_span read(uint64_t offs, size_t size) override
	{
		pdb::buffer_span res;

		if (offs >= 0x100000000 || offs > m_file_size)
		{
			res.size = 0;
			res.ptr = 0;
			return res;
		}

		res.ptr = reinterpret_cast<uint8_t const *>(m_view) + offs;
		res.size = (std::min)(size, m_file_size - (size_t)offs);
		return res;
	}

	HANDLE m_hFile;
	size_t m_file_size;
	HANDLE m_hMapping;
	void * m_view;
};

struct state_t
{
	mapped_file file;
	pdb::pdb_stream_index pdb_idx;
	size_t cur;
	uint32_t file_time;
};

wcx_handle_t __stdcall OpenArchive(open_archive_data * data)
{
	try
	{
		data->open_result = wcx_status_t::success;

		auto state = std::make_unique<state_t>();
		state->file.open(data->archive_name);
		state->pdb_idx.open(make_unshared(&state->file));
		state->cur = 0;
		state->file_time = (uint32_t)-1;

		FILETIME write_time;
		if (GetFileTime(state->file.m_hFile, 0, 0, &write_time))
		{
			FileTimeToLocalFileTime(&write_time, &write_time);
			SYSTEMTIME st;
			if (FileTimeToSystemTime(&write_time, &st))
				state->file_time = (st.wYear - 1980) << 25 | st.wMonth << 21 | st.wDay << 16 | st.wHour << 11 | st.wMinute << 5 | st.wSecond / 2;
		}

		return reinterpret_cast<wcx_handle_t>(state.release());
	}
	catch (...)
	{
		data->open_result = wcx_status_t::bad_archive;
		return 0;
	}
}

wcx_status_t __stdcall ReadHeader(wcx_handle_t handle, header_data * data)
{
	auto state = reinterpret_cast<state_t *>(handle);
	if (state->cur >= state->pdb_idx.stream_count())
		return wcx_status_t::end_archive;

	_itoa(state->cur, data->file_name, 10);
	data->packed_size = data->unpacked_size = state->pdb_idx.stream_size(state->cur);
	data->FileTime = state->file_time;
	return wcx_status_t::success;
}

wcx_status_t __stdcall ProcessFile(wcx_handle_t handle, wcx_process_op_t op, char const * destination_path, char const * destination_name)
{
	auto state = reinterpret_cast<state_t *>(handle);
	size_t cur = state->cur;
	++state->cur;
	while (state->cur < state->pdb_idx.stream_count() && !state->pdb_idx.stream_valid(state->cur))
		++state->cur;

	if (op == wcx_process_op_t::extract)
	{
		try
		{
			std::shared_ptr<pdb::file> stream = state->pdb_idx.open_stream(cur);

			std::string dest;
			if (destination_path)
			{
				dest.assign(destination_path);
				dest += "\\";
				dest.append(destination_name);
				destination_name = dest.c_str();
			}

			std::ofstream fout(destination_name, std::ios::binary);
			if (!fout)
				return wcx_status_t::ecreate;

			uint64_t offs = 0;
			static size_t const max_chunk_size = 65536;
			for (;;)
			{
				pdb::buffer_span buf = stream->read(offs, max_chunk_size);
				if (!fout.write((char const *)buf.ptr, buf.size))
					return wcx_status_t::ewrite;
				if (buf.size < max_chunk_size)
					break;
				offs += buf.size;
			}
		}
		catch (...)
		{
			return wcx_status_t::no_memory;
		}
	}

	return wcx_status_t::success;
}

wcx_status_t __stdcall CloseArchive(wcx_handle_t handle)
{
	auto state = reinterpret_cast<state_t *>(handle);
	delete state;

	return wcx_status_t::success;
}

void __stdcall SetChangeVolProc(wcx_handle_t handle, change_volume_proc_t * fn)
{
	(void)handle;
	(void)fn;
}

void __stdcall SetProcessDataProc(wcx_handle_t handle, process_data_proc_t * fn)
{
	(void)handle;
	(void)fn;
}
