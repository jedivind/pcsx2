/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "GSDump.h"

GSDumpBase::GSDumpBase(const string& fn)
	: m_frames(0)
	, m_extra_frames(2)
{
	m_gs = fopen(fn.c_str(), "wb");
}

GSDumpBase::~GSDumpBase()
{
	if(m_gs)
		fclose(m_gs);
}

void GSDumpBase::AddHeader(uint32 crc, const GSFreezeData& fd, const GSPrivRegSet* regs)
{
	AppendRawData(&crc, 4);
	AppendRawData(&fd.size, 4);
	AppendRawData(fd.data, fd.size);
	AppendRawData(regs, sizeof(*regs));
}

void GSDumpBase::Transfer(int index, const uint8* mem, size_t size)
{
	if (size == 0)
		return;

	AppendRawData(0);
	AppendRawData(index);
	AppendRawData(&size, 4);
	AppendRawData(mem, size);
}

void GSDumpBase::ReadFIFO(uint32 size)
{
	if (size == 0)
		return;

	AppendRawData(2);
	AppendRawData(&size, 4);
}

bool GSDumpBase::VSync(int field, bool last, const GSPrivRegSet* regs)
{
	AppendRawData(3);
	AppendRawData(regs, sizeof(*regs));

	AppendRawData(1);
	AppendRawData(field);

	if (last)
		m_extra_frames--;

	return ((++m_frames & 1) == 0 && last && (m_extra_frames < 0));
}

//////////////////////////////////////////////////////////////////////
// GSDump implementation
//////////////////////////////////////////////////////////////////////

GSDump::GSDump(const string& fn, uint32 crc, const GSFreezeData& fd, const GSPrivRegSet* regs) : GSDumpBase(fn + ".gs")
{
	AddHeader(crc, fd, regs);
}

void GSDump::AppendRawData(const void *data, size_t size)
{
	size_t written = fwrite(data, size, 1, m_gs);
	if (written != size)
		fprintf(stderr, "GSDump: Error failed to write data\n");
}

void GSDump::AppendRawData(uint8 c)
{
	if (fputc(c, m_gs) == EOF)
		fprintf(stderr, "GSDump: Error failed to write data\n");
}

//////////////////////////////////////////////////////////////////////
// GSDumpXz implementation
//////////////////////////////////////////////////////////////////////

#ifdef LZMA_SUPPORTED

GSDumpXz::GSDumpXz(const string& fn, uint32 crc, const GSFreezeData& fd, const GSPrivRegSet* regs) : GSDumpBase(fn + ".gs.xz")
{
	m_in_buff.clear();

	m_strm = LZMA_STREAM_INIT;
	lzma_ret ret = lzma_easy_encoder(&m_strm, 6 /*level*/, LZMA_CHECK_CRC64);
	if (ret != LZMA_OK) {
		fprintf(stderr, "GSDumpXz: Error initializing LZMA encoder ! (error code %u)\n", ret);
		return;
	}

	AddHeader(crc, fd, regs);
}

GSDumpXz::~GSDumpXz()
{
	Flush(true);
}

void GSDumpXz::AppendRawData(const void *data, size_t size)
{
	size_t old_size = m_in_buff.size();
	m_in_buff.resize(old_size + size);
	memcpy(&m_in_buff[old_size], data, size);

	// Enough data was accumulated, time to write/compress it.  If compression
	// is enabled, it will freeze PCSX2. 1GB should be enough for long dump.
	if (m_in_buff.size() > 1024*1024*1024)
		Flush(false);
}

void GSDumpXz::AppendRawData(uint8 c)
{
	m_in_buff.push_back(c);
}


void GSDumpXz::Flush(bool close)
{
	if (!m_gs || m_in_buff.empty())
	{
		m_in_buff.clear(); // output file isn't open we can drop current data
		return;
	}

	lzma_action action = close ? LZMA_FINISH : LZMA_RUN;

	m_strm.next_in = &m_in_buff[0];
	m_strm.avail_in = m_in_buff.size();

	std::vector<uint8> out_buff(1024*1024);
	do {
		m_strm.next_out = &out_buff[0];
		m_strm.avail_out = out_buff.size();

		lzma_ret ret = lzma_code(&m_strm, action);

		if ((ret != LZMA_OK) && (ret != LZMA_STREAM_END)) {
			fprintf (stderr, "GSDumpXz: Error %d\n", (int) ret);
			m_in_buff.clear();
		}

		size_t write_size = out_buff.size() - m_strm.avail_out;
		if (write_size)
		{
			size_t written = fwrite(&out_buff[0], write_size, 1, m_gs);
			if (written != write_size)
				fprintf(stderr, "GSDumpXz: Error failed to write data\n");
		}

	} while (m_strm.avail_out == 0);

	m_in_buff.clear();

	if (close)
		lzma_end(&m_strm);
}

#endif
