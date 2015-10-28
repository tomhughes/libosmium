#ifndef OSMIUM_IO_DETAIL_READ_THREAD_HPP
#define OSMIUM_IO_DETAIL_READ_THREAD_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2015 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <atomic>
#include <exception>
#include <string>
#include <thread>
#include <utility>

#include <osmium/io/compression.hpp>
#include <osmium/io/detail/queue_util.hpp>
#include <osmium/thread/util.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            /**
             * This code uses an internally managed thread to read data from
             * the input file and (optionally) decompress it. The result is
             * sent to the given queue. Any exceptions will also be send to
             * the queue.
             */
            class ReadThreadManager {

                // only used in the sub-thread
                osmium::io::Decompressor* m_decompressor;
                future_string_queue_type& m_queue;

                // used in both threads
                std::atomic<bool> m_done;

                // only used in the main thread
                std::thread m_thread;

                void run_in_thread() {
                    osmium::thread::set_thread_name("_osmium_read");

                    try {
                        while (!m_done) {
                            std::string data {m_decompressor->read()};
                            if (data.empty()) { // end of file
                                break;
                            }
                            add_to_queue(m_queue, std::move(data));
                        }

                        m_decompressor->close();
                    } catch (...) {
                        add_to_queue(m_queue, std::current_exception());
                    }

                    add_to_queue(m_queue, std::string{});
                }

            public:

                ReadThreadManager(osmium::io::Decompressor* decompressor,
                                  future_string_queue_type& queue) :
                    m_decompressor(decompressor),
                    m_queue(queue),
                    m_done(false),
                    m_thread() {
                    m_thread = std::thread(&ReadThreadManager::run_in_thread, this);
                }

                ReadThreadManager(const ReadThreadManager&) = delete;
                ReadThreadManager& operator=(const ReadThreadManager&) = delete;

                ReadThreadManager(ReadThreadManager&&) = delete;
                ReadThreadManager& operator=(ReadThreadManager&&) = delete;

                ~ReadThreadManager() noexcept {
                    try {
                        close();
                    } catch (...) {
                        // Ignore any exceptions because destructor must not throw.
                    }
                }

                void stop() noexcept {
                    m_done = true;
                }

                void close() {
                    stop();
                    if (m_thread.joinable()) {
                        m_thread.join();
                    }
                }

            }; // class ReadThreadManager

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_READ_THREAD_HPP
