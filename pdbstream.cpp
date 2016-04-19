#include "pdbstream.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
using namespace pdb;

struct pdb_stream_index::pdb_stream
	: pdb::file
{
	uint32_t size;
	std::vector<uint32_t> page_index;
	uint32_t page_size;
	std::shared_ptr<pdb::file> source;

	buffer_span read(uint64_t offs, size_t size) override
	{
		if (size == 0 || offs >= this->size)
			return buffer_span();

		if (offs + size > this->size)
			size = this->size - offs;

		size_t first_page = offs / page_size;
		size_t first_page_offset = offs % page_size;
		size_t last_page = (offs + size) / page_size + 1;

		bool contiguous = true;
		for (size_t i = first_page + 1; i < last_page; ++i)
		{
			if (page_index[i-1] + 1 != page_index[i])
			{
				contiguous = false;
				break;
			}
		}

		if (contiguous)
			return source->read(page_index[first_page] * page_size + first_page_offset, size);

		auto buf = std::make_shared<std::vector<uint8_t>>();
		buf->resize(size);

		uint8_t * ptr = buf->data();
		while (size != 0)
		{
			size_t chunk_size = (std::min)(page_size - first_page_offset, size);

			buffer_span buf = source->read(page_index[first_page] * page_size + first_page_offset, chunk_size);
			memcpy(ptr, buf.ptr, chunk_size);
			++first_page;
			first_page_offset = 0;
			size -= chunk_size;
			ptr += chunk_size;
		}

		buffer_span res;
		res.ptr = buf->data();
		res.size = buf->size();
		res.buf = std::move(buf);
		return res;
	}
};

template <typename T>
T load_le(uint8_t const * buf)
{
	T res;
	memcpy(&res, buf, sizeof res);
	return res;
}

template <typename T>
void load_array_le(pdb::file & file, uint64_t offs, T * p, size_t count)
{
	static size_t const max_chunk_size = 65536 / sizeof(T) * sizeof(T);

	size_t read_size = count * sizeof(T);
	while (read_size != 0)
	{
		size_t chunk = (std::min)(max_chunk_size, read_size);

		buffer_span buf = file.read(offs, chunk);
		if (buf.size != chunk)
			throw std::runtime_error("failed to read");
		read_size -= chunk;
		offs += chunk;

		while (buf.size != 0)
		{
			*p++ = load_le<T>(buf.ptr);
			buf.ptr += sizeof(T);
			buf.size -= sizeof(T);
		}
	}
}

void pdb_stream_index::open(std::shared_ptr<pdb::file> file)
{
	m_file = std::move(file);
	buffer_span header = m_file->read(0, 0x30);
	if (memcmp(header.ptr, "Microsoft C/C++ MSF 7.00\r\n\x1A""DS\0\0\0", 0x20) != 0)
		throw std::runtime_error("invalid header");

	m_page_size = load_le<uint32_t>(header.ptr + 0x20);

	std::shared_ptr<pdb_stream> root_stream = std::make_shared<pdb_stream>();
	root_stream->size = load_le<uint32_t>(header.ptr + 0x2c);
	root_stream->page_size = m_page_size;
	root_stream->source = m_file;

	std::shared_ptr<pdb_stream> root_stream_index = std::make_shared<pdb_stream>();
	root_stream_index->size = (root_stream->size + m_page_size - 1) / m_page_size * 4;
	root_stream_index->page_size = m_page_size;
	root_stream_index->source = m_file;
	root_stream_index->page_index.resize((root_stream_index->size + m_page_size - 1) / m_page_size);
	load_array_le(*m_file, 0x34, root_stream_index->page_index.data(), root_stream_index->page_index.size());

	root_stream->page_index.resize((root_stream->size + m_page_size - 1) / m_page_size);
	load_array_le(*root_stream_index, 0, root_stream->page_index.data(), root_stream->page_index.size());

	uint32_t stream_count;
	load_array_le(*root_stream, 0, &stream_count, 1);
	std::vector<uint32_t> stream_sizes(stream_count);
	load_array_le(*root_stream, 4, stream_sizes.data(), stream_sizes.size());
	uint64_t offs = 4 + stream_sizes.size() * 4;

	for (size_t i = 0; i < stream_count; ++i)
	{
		if (stream_sizes[i] == 0xffffffff)
		{
			m_streams.push_back(nullptr);
			continue;
		}

		std::shared_ptr<pdb_stream> stream = std::make_shared<pdb_stream>();
		stream->page_size = m_page_size;
		stream->size = stream_sizes[i];
		stream->source = m_file;
		stream->page_index.resize((stream_sizes[i] + m_page_size - 1) / m_page_size);
		load_array_le(*root_stream, offs, stream->page_index.data(), stream->page_index.size());
		offs += stream->page_index.size() * 4;
		m_streams.push_back(stream);
	}
}

size_t pdb_stream_index::stream_count() const
{
	return m_streams.size();
}

std::shared_ptr<pdb::file> pdb_stream_index::open_stream(size_t index)
{
	return m_streams[index];
}

uint32_t pdb_stream_index::stream_size(size_t index) const
{
	return m_streams[index]->size;
}

bool pdb_stream_index::stream_valid(size_t index) const
{
	return m_streams[index] != nullptr;
}
