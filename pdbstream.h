#ifndef PDBSTREAM_H
#define PDBSTREAM_H

#include <stdint.h>
#include <memory>
#include <vector>

template <typename T>
std::shared_ptr<T> make_unshared(T * t)
{
	return std::shared_ptr<T>(std::shared_ptr<T>(), t);
}

namespace pdb {

template <typename T>
struct sequence_span
{
	T const * ptr;
	size_t size;
	std::shared_ptr<void> buf;

	sequence_span()
		: ptr(nullptr), size(0)
	{
	}

	sequence_span(T const * first, T const * last)
		: ptr(first), size(last - first)
	{
	}
};

typedef sequence_span<uint8_t> buffer_span;

struct file
{
	virtual buffer_span read(uint64_t offs, size_t size) = 0;
};

struct pdb_stream_index
{
	void open(std::shared_ptr<pdb::file> file);

	std::shared_ptr<pdb::file> open_stream(size_t index);
	uint32_t stream_size(size_t index) const;
	size_t stream_count() const;
	bool stream_valid(size_t index) const;

private:
	std::shared_ptr<pdb::file> m_file;
	uint32_t m_page_size;

	struct pdb_stream;
	std::vector<std::shared_ptr<pdb_stream>> m_streams;
};

}

#endif // PDBSTREAM_H
