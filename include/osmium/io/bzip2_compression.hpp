#ifndef OSMIUM_IO_BZIP2_COMPRESSION_HPP
#define OSMIUM_IO_BZIP2_COMPRESSION_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2025 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

/**
 * @file
 *
 * Include this file if you want to read or write bzip2-compressed OSM
 * files.
 *
 * @attention If you include this file, you'll need to link with `libbz2`.
 */

#include <osmium/io/compression.hpp>
#include <osmium/io/detail/read_write.hpp>
#include <osmium/io/error.hpp>
#include <osmium/io/file_compression.hpp>
#include <osmium/io/writer_options.hpp>
#include <osmium/util/file.hpp>

#include <bzlib.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <string>
#include <system_error>

#ifndef _MSC_VER
# include <unistd.h>
#endif

namespace osmium {

    /**
     * Exception thrown when there are problems compressing or
     * decompressing bzip2 files.
     */
    struct bzip2_error : public io_error {

        int bzip2_error_code = 0;
        int system_errno = 0;

        bzip2_error(const std::string& what, const int error_code) :
            io_error(what),
            bzip2_error_code(error_code) {
            if (error_code == BZ_IO_ERROR) {
                system_errno = errno;
            }
        }

    }; // struct bzip2_error

    namespace io {

        namespace detail {

            [[noreturn]] inline void throw_bzip2_error(BZFILE* bzfile, const char* msg, const int bzlib_error) {
                std::string error{"bzip2 error: "};
                error += msg;
                error += ": ";
                int errnum = bzlib_error;
                if (bzlib_error) {
                    error += std::to_string(bzlib_error);
                } else if (bzfile) {
                    error += ::BZ2_bzerror(bzfile, &errnum);
                }
                throw osmium::bzip2_error{error, errnum};
            }

            class file_wrapper {

                FILE* m_file = nullptr;

            public:

                file_wrapper() noexcept = default;

                file_wrapper(const int fd, const char* mode) {
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    m_file = fdopen(fd, mode); // NOLINT(cppcoreguidelines-prefer-member-initializer)
                    if (!m_file) {

                        // Do not close stdout
                        if (fd != 1) {
                            ::close(fd);
                        }
                        throw std::system_error{errno, std::system_category(), "fdopen failed"};
                    }
                }

                file_wrapper(const file_wrapper&) = delete;
                file_wrapper& operator=(const file_wrapper&) = delete;

                file_wrapper(file_wrapper&&) = delete;
                file_wrapper& operator=(file_wrapper&&) = delete;

                ~file_wrapper() noexcept {
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    if (m_file) {
                        (void)fclose(m_file);
                    }
                }

                FILE* file() const noexcept {
                    return m_file;
                }

                void close() {
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    if (m_file) {
                        FILE* wrapped_file = m_file;
                        m_file = nullptr;

                        // Do not close stdout
                        if (fileno(wrapped_file) == 1) {
                            return;
                        }

                        if (fclose(wrapped_file) != 0) {
                            throw std::system_error{errno, std::system_category(), "fclose failed"};
                        }
                    }
                }

            }; // class file_wrapper

        } // namespace detail

        class Bzip2Compressor final : public Compressor {

            std::size_t m_file_size = 0;
            detail::file_wrapper m_file;
            BZFILE* m_bzfile = nullptr;

        public:

            explicit Bzip2Compressor(const int fd, const fsync sync) :
                Compressor(sync),
                m_file(fd, "wb") {
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                int bzerror = BZ_OK;
                m_bzfile = ::BZ2_bzWriteOpen(&bzerror, m_file.file(), 6, 0, 0);
                if (!m_bzfile) {
                    throw bzip2_error{"bzip2 error: write open failed", bzerror};
                }
            }

            Bzip2Compressor(const Bzip2Compressor&) = delete;
            Bzip2Compressor& operator=(const Bzip2Compressor&) = delete;

            Bzip2Compressor(Bzip2Compressor&&) = delete;
            Bzip2Compressor& operator=(Bzip2Compressor&&) = delete;

            ~Bzip2Compressor() noexcept override {
                try {
                    close();
                } catch (...) { // NOLINT(bugprone-empty-catch)
                    // Ignore any exceptions because destructor must not throw.
                }
            }

            void write(const std::string& data) override {
                assert(data.size() < std::numeric_limits<int>::max());
                assert(m_bzfile);
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                int bzerror = BZ_OK;
                ::BZ2_bzWrite(&bzerror, m_bzfile, const_cast<char*>(data.data()), static_cast<int>(data.size()));
                if (bzerror != BZ_OK && bzerror != BZ_STREAM_END) {
                    detail::throw_bzip2_error(m_bzfile, "write failed", bzerror);
                }
            }

            void close() override {
                if (m_bzfile) {
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    int bzerror = BZ_OK;
                    unsigned int nbytes_out_lo32 = 0;
                    unsigned int nbytes_out_hi32 = 0;
                    ::BZ2_bzWriteClose64(&bzerror, m_bzfile, 0, nullptr, nullptr, &nbytes_out_lo32, &nbytes_out_hi32);
                    m_bzfile = nullptr;
                    if (do_fsync() && m_file.file()) {
                        osmium::io::detail::reliable_fsync(fileno(m_file.file()));
                    }
                    m_file.close();
                    if (bzerror != BZ_OK) {
                        throw bzip2_error{"bzip2 error: write close failed", bzerror};
                    }
                    m_file_size = static_cast<std::size_t>(static_cast<uint64_t>(nbytes_out_hi32) << 32U | nbytes_out_lo32);
                }
            }

            std::size_t file_size() const override {
                return m_file_size;
            }

        }; // class Bzip2Compressor

        class Bzip2Decompressor final : public Decompressor {

            detail::file_wrapper m_file;
            BZFILE* m_bzfile = nullptr;
            bool m_stream_end = false;

        public:

            explicit Bzip2Decompressor(const int fd) :
                m_file(fd, "rb") {
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                int bzerror = BZ_OK;
                m_bzfile = ::BZ2_bzReadOpen(&bzerror, m_file.file(), 0, 0, nullptr, 0);
                if (!m_bzfile) {
                    throw bzip2_error{"bzip2 error: read open failed", bzerror};
                }
            }

            Bzip2Decompressor(const Bzip2Decompressor&) = delete;
            Bzip2Decompressor& operator=(const Bzip2Decompressor&) = delete;

            Bzip2Decompressor(Bzip2Decompressor&&) = delete;
            Bzip2Decompressor& operator=(Bzip2Decompressor&&) = delete;

            ~Bzip2Decompressor() noexcept override {
                try {
                    close();
                } catch (...) { // NOLINT(bugprone-empty-catch)
                    // Ignore any exceptions because destructor must not throw.
                }
            }

            std::string read() override {
                const auto offset = ftell(m_file.file());
                if (offset > 0 && want_buffered_pages_removed()) {
                    osmium::io::detail::remove_buffered_pages(fileno(m_file.file()), static_cast<std::size_t>(offset));
                }
#ifdef _MSC_VER
                osmium::detail::disable_invalid_parameter_handler diph;
#endif
                assert(m_bzfile);
                std::string buffer;

                if (!m_stream_end) {
                    buffer.resize(osmium::io::Decompressor::input_buffer_size);
                    int bzerror = BZ_OK;
                    assert(buffer.size() < std::numeric_limits<int>::max());
                    const int nread = ::BZ2_bzRead(&bzerror, m_bzfile, &*buffer.begin(), static_cast<int>(buffer.size()));
                    if (bzerror != BZ_OK && bzerror != BZ_STREAM_END) {
                        detail::throw_bzip2_error(m_bzfile, "read failed", bzerror);
                    }
                    if (bzerror == BZ_STREAM_END) {
                        if (!feof(m_file.file())) {
                            void* unused = nullptr;
                            int num_unused = 0;
                            ::BZ2_bzReadGetUnused(&bzerror, m_bzfile, &unused, &num_unused);
                            if (bzerror != BZ_OK) {
                                detail::throw_bzip2_error(m_bzfile, "get unused failed", bzerror);
                            }
                            if (num_unused != 0) {
                                std::string unused_data{static_cast<const char*>(unused), static_cast<std::string::size_type>(num_unused)};
                                ::BZ2_bzReadClose(&bzerror, m_bzfile);
                                if (bzerror != BZ_OK) {
                                    throw bzip2_error{"bzip2 error: read close failed", bzerror};
                                }
                                assert(unused_data.size() < std::numeric_limits<int>::max());
                                m_bzfile = ::BZ2_bzReadOpen(&bzerror, m_file.file(), 0, 0, &*unused_data.begin(), static_cast<int>(unused_data.size()));
                                if (!m_bzfile) {
                                    throw bzip2_error{"bzip2 error: read open failed", bzerror};
                                }
                            } else {
                                // Close current stream and try to open a new one for multi-stream files
                                ::BZ2_bzReadClose(&bzerror, m_bzfile);
                                if (bzerror != BZ_OK) {
                                    throw bzip2_error{"bzip2 error: read close failed", bzerror};
                                }
                                // Try to open a new stream - there might be more bzip2 streams concatenated
                                m_bzfile = ::BZ2_bzReadOpen(&bzerror, m_file.file(), 0, 0, nullptr, 0);
                                if (!m_bzfile || bzerror != BZ_OK) {
                                    // If we can't open a new stream, we've truly reached the end
                                    m_stream_end = true;
                                    m_bzfile = nullptr;
                                }
                            }
                        } else {
                            m_stream_end = true;
                        }
                    }
                    buffer.resize(static_cast<std::string::size_type>(nread));
                }

                set_offset(static_cast<std::size_t>(ftell(m_file.file())));

                return buffer;
            }

            void close() override {
                if (m_bzfile) {
                    if (want_buffered_pages_removed()) {
                        osmium::io::detail::remove_buffered_pages(fileno(m_file.file()));
                    }
#ifdef _MSC_VER
                    osmium::detail::disable_invalid_parameter_handler diph;
#endif
                    int bzerror = BZ_OK;
                    ::BZ2_bzReadClose(&bzerror, m_bzfile);
                    m_bzfile = nullptr;
                    m_file.close();
                    if (bzerror != BZ_OK) {
                        throw bzip2_error{"bzip2 error: read close failed", bzerror};
                    }
                }
            }

        }; // class Bzip2Decompressor

        class Bzip2BufferDecompressor final : public Decompressor {

            const char* m_buffer;
            std::size_t m_buffer_size;
            bz_stream m_bzstream;

        public:

            Bzip2BufferDecompressor(const char* buffer, const std::size_t size) :
                m_buffer(buffer),
                m_buffer_size(size),
                m_bzstream() {
                m_bzstream.next_in = const_cast<char*>(buffer);
                assert(size < std::numeric_limits<unsigned int>::max());
                m_bzstream.avail_in = static_cast<unsigned int>(size);
                const int result = BZ2_bzDecompressInit(&m_bzstream, 0, 0);
                if (result != BZ_OK) {
                    throw bzip2_error{"bzip2 error: decompression init failed: ", result};
                }
            }

            Bzip2BufferDecompressor(const Bzip2BufferDecompressor&) = delete;
            Bzip2BufferDecompressor& operator=(const Bzip2BufferDecompressor&) = delete;

            Bzip2BufferDecompressor(Bzip2BufferDecompressor&&) = delete;
            Bzip2BufferDecompressor& operator=(Bzip2BufferDecompressor&&) = delete;

            ~Bzip2BufferDecompressor() noexcept override {
                try {
                    close();
                } catch (...) { // NOLINT(bugprone-empty-catch)
                    // Ignore any exceptions because destructor must not throw.
                }
            }

            std::string read() override {
                std::string output;

                if (m_buffer) {
                    const std::size_t buffer_size = 10240;
                    output.resize(buffer_size);
                    m_bzstream.next_out = &*output.begin();
                    m_bzstream.avail_out = buffer_size;
                    const int result = BZ2_bzDecompress(&m_bzstream);

                    if (result != BZ_OK) {
                        m_buffer = nullptr;
                        m_buffer_size = 0;
                    }

                    if (result != BZ_OK && result != BZ_STREAM_END) {
                        throw bzip2_error{"bzip2 error: decompress failed: ", result};
                    }

                    output.resize(static_cast<std::size_t>(m_bzstream.next_out - output.data()));
                }

                return output;
            }

            void close() override {
                BZ2_bzDecompressEnd(&m_bzstream);
            }

        }; // class Bzip2BufferDecompressor

        namespace detail {

            // we want the register_compression() function to run, setting
            // the variable is only a side-effect, it will never be used
            const bool registered_bzip2_compression = osmium::io::CompressionFactory::instance().register_compression(osmium::io::file_compression::bzip2,
                [](const int fd, const fsync sync) { return new osmium::io::Bzip2Compressor{fd, sync}; },
                [](const int fd) { return new osmium::io::Bzip2Decompressor{fd}; },
                [](const char* buffer, const std::size_t size) { return new osmium::io::Bzip2BufferDecompressor{buffer, size}; }
            );

            // dummy function to silence the unused variable warning from above
            inline bool get_registered_bzip2_compression() noexcept {
                return registered_bzip2_compression;
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_BZIP2_COMPRESSION_HPP
