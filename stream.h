/*
 * Voxels is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Voxels is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Voxels; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */
#ifndef STREAM_H
#define STREAM_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstring>
#include <memory>
#include <list>
#include <cassert>
#include <iostream>

using namespace std;

class IOException : public runtime_error
{
public:
    explicit IOException(const string & msg)
        : runtime_error(msg)
    {
    }
    explicit IOException(exception * e, bool deleteIt = true)
        : runtime_error((dynamic_cast<IOException *>(e) == nullptr) ? string("IO Error : ") + e->what() : string(e->what()))
    {
        if(deleteIt)
            delete e;
    }
    explicit IOException(exception & e, bool deleteIt = false)
        : IOException(&e, deleteIt)
    {
    }
};

class EOFException final : public IOException
{
public:
    explicit EOFException()
        : IOException("IO Error : reached end of file")
    {
    }
};

class NoStreamsLeftException final : public IOException
{
public:
    explicit NoStreamsLeftException()
        : IOException("IO Error : no streams left")
    {
    }
};

class UTFDataFormatException final : public IOException
{
public:
    explicit UTFDataFormatException()
        : IOException("IO Error : invalid UTF data")
    {
    }
};

class InvalidDataValueException final : public IOException
{
public:
    explicit InvalidDataValueException(string msg)
        : IOException(msg)
    {
    }
};

class Reader
{
private:
    template <typename T>
    static T limitAfterRead(T v, T min, T max)
    {
        if(v < min || v > max)
        {
            throw InvalidDataValueException("read value out of range : " + to_string(v));
        }
        return v;
    }
public:
    Reader()
    {
    }
    Reader(const Reader &) = delete;
    const Reader & operator =(const Reader &) = delete;
    virtual ~Reader()
    {
    }
    virtual uint8_t readByte() = 0;
};

class Writer
{
public:
    Writer()
    {
    }
    Writer(const Writer &) = delete;
    const Writer & operator =(const Writer &) = delete;
    virtual ~Writer()
    {
    }
    virtual void writeByte(uint8_t) = 0;
    virtual void flush()
    {
    }
};

class FileReader final : public Reader
{
private:
    FILE * f;
public:
    FileReader(string fileName)
    {
        string str = fileName;
        f = fopen(str.c_str(), "rb");
        if(f == nullptr)
            throw IOException(string("IO Error : ") + strerror(errno));
    }
    explicit FileReader(FILE * f)
        : f(f)
    {
        assert(f != nullptr);
    }
    virtual ~FileReader()
    {
        fclose(f);
    }
    virtual uint8_t readByte() override
    {
        int ch = fgetc(f);
        if(ch == EOF)
        {
            if(ferror(f))
                throw IOException("IO Error : can't read from file");
            throw EOFException();
        }
        return ch;
    }
};

class FileWriter final : public Writer
{
private:
    FILE * f;
public:
    FileWriter(string fileName)
    {
        string str = fileName;
        f = fopen(str.c_str(), "wb");
        if(f == nullptr)
            throw IOException(string("IO Error : ") + strerror(errno));
    }
    explicit FileWriter(FILE * f)
        : f(f)
    {
        assert(f != nullptr);
    }
    virtual ~FileWriter()
    {
        fclose(f);
    }
    virtual void writeByte(uint8_t v) override
    {
        if(fputc(v, f) == EOF)
            throw IOException("IO Error : can't write to file");
    }
    virtual void flush() override
    {
        if(EOF == fflush(f))
            throw IOException("IO Error : can't write to file");
    }
};

class MemoryReader final : public Reader
{
private:
    const shared_ptr<const uint8_t> mem;
    size_t offset;
    const size_t length;
public:
    explicit MemoryReader(shared_ptr<const uint8_t> mem, size_t length)
        : mem(mem), offset(0), length(length)
    {
    }
    template <size_t length>
    explicit MemoryReader(const uint8_t a[length])
        : MemoryReader(shared_ptr<const uint8_t>(&a[0], [](const uint8_t *){}))
    {
    }
    virtual uint8_t readByte() override
    {
        if(offset >= length)
            throw EOFException();
        return mem.get()[offset++];
    }
};

class StreamPipe final
{
    StreamPipe(const StreamPipe &) = delete;
    const StreamPipe & operator =(const StreamPipe &) = delete;
private:
    shared_ptr<Reader> readerInternal;
    shared_ptr<Writer> writerInternal;
public:
    StreamPipe(bool useOSPipe = false);
    Reader & reader()
    {
        return *readerInternal;
    }
    Writer & writer()
    {
        return *writerInternal;
    }
    shared_ptr<Reader> preader()
    {
        return readerInternal;
    }
    shared_ptr<Writer> pwriter()
    {
        return writerInternal;
    }
};

class DumpingReader final : public Reader
{
private:
    Reader &reader;
public:
    DumpingReader(Reader& reader)
        : reader(reader)
    {
    }
    virtual uint8_t readByte() override;
};

struct StreamRW
{
    StreamRW()
    {
    }
    StreamRW(const StreamRW &) = delete;
    const StreamRW & operator =(const StreamRW &) = delete;
    virtual ~StreamRW()
    {
    }
    Reader & reader()
    {
        return *preader();
    }
    Writer & writer()
    {
        return *pwriter();
    }
    virtual shared_ptr<Reader> preader() = 0;
    virtual shared_ptr<Writer> pwriter() = 0;
};

class StreamRWWrapper final : public StreamRW
{
private:
    shared_ptr<Reader> preaderInternal;
    shared_ptr<Writer> pwriterInternal;
public:
    StreamRWWrapper(shared_ptr<Reader> preaderInternal, shared_ptr<Writer> pwriterInternal)
        : preaderInternal(preaderInternal), pwriterInternal(pwriterInternal)
    {
    }

    virtual shared_ptr<Reader> preader() override
    {
        return preaderInternal;
    }

    virtual shared_ptr<Writer> pwriter() override
    {
        return pwriterInternal;
    }
};

class StreamBidirectionalPipe final
{
private:
    StreamPipe pipe1, pipe2;
    shared_ptr<StreamRW> port1Internal, port2Internal;
public:
    StreamBidirectionalPipe()
    {
        port1Internal = shared_ptr<StreamRW>(new StreamRWWrapper(pipe1.preader(), pipe2.pwriter()));
        port2Internal = shared_ptr<StreamRW>(new StreamRWWrapper(pipe2.preader(), pipe1.pwriter()));
    }
    shared_ptr<StreamRW> pport1()
    {
        return port1Internal;
    }
    shared_ptr<StreamRW> pport2()
    {
        return port2Internal;
    }
    StreamRW & port1()
    {
        return *port1Internal;
    }
    StreamRW & port2()
    {
        return *port2Internal;
    }
};

struct StreamServer
{
    StreamServer()
    {
    }
    StreamServer(const StreamServer &) = delete;
    const StreamServer & operator =(const StreamServer &) = delete;
    virtual ~StreamServer()
    {
    }
    virtual shared_ptr<StreamRW> accept() = 0;
};

class StreamServerWrapper final : public StreamServer
{
private:
    list<shared_ptr<StreamRW>> streams;
    shared_ptr<StreamServer> nextServer;
public:
    StreamServerWrapper(list<shared_ptr<StreamRW>> streams, shared_ptr<StreamServer> nextServer = nullptr)
        : streams(streams), nextServer(nextServer)
    {
    }
    virtual shared_ptr<StreamRW> accept() override
    {
        if(streams.empty())
        {
            if(nextServer == nullptr)
                throw NoStreamsLeftException();
            return nextServer->accept();
        }
        shared_ptr<StreamRW> retval = streams.front();
        streams.pop_front();
        return retval;
    }
};

class ReaderStreamBuf : public streambuf
{
    shared_ptr<Reader> reader;
    char buffer;
public:
    ReaderStreamBuf(shared_ptr<Reader> reader)
        : reader(reader)
    {
    }
    void close()
    {
        reader = nullptr;
    }
private:
    int getByteInternal()
    {
        if(!reader)
            return EOF;
        try
        {
            return reader->readByte();
        }
        catch(EOFException & e)
        {
            return EOF;
        }
        catch(IOException & e)
        {
            return EOF;
            //TODO: signal error
        }
    }
protected:
    virtual int underflow() override
    {
        int ch = getByteInternal();
        if(ch == EOF)
        {
            return char_traits<char>::eof();
        }
        buffer = ch;
        setg(&buffer, &buffer, &buffer + 1);
        return buffer;
    }
};

class WriterStreamBuf : public streambuf
{
    shared_ptr<Writer> writer;
public:
    WriterStreamBuf(shared_ptr<Writer> writer)
        : writer(writer)
    {
    }
    void close()
    {
        writer = nullptr;
    }
private:
    int putByteInternal(int byte)
    {
        if(!writer)
            return -1;
        try
        {
            writer->writeByte((uint8_t)byte);
            return 0;
        }
        catch(IOException & e)
        {
            return -1;
        }
    }
protected:
    virtual int overflow(int ch) override
    {
        if(ch == EOF)
            return sync();
        return putByteInternal(ch);
    }
    virtual int sync() override
    {
        if(!writer)
            return -1;
        try
        {
            writer->flush();
            return 0;
        }
        catch(IOException & e)
        {
            return -1;
        }
    }
};

class ReaderIStream : public istream
{
private:
    ReaderStreamBuf sb;
public:
    ReaderIStream(shared_ptr<Reader> reader)
        : istream(nullptr), sb(reader)
    {
        rdbuf(&sb);
    }
    void close()
    {
        sb.close();
    }
};

class WriterOStream : public ostream
{
private:
    WriterStreamBuf sb;
public:
    WriterOStream(shared_ptr<Writer> writer)
        : ostream(nullptr), sb(writer)
    {
        rdbuf(&sb);
    }
    void close()
    {
        sb.close();
    }
};

#endif // STREAM_H
