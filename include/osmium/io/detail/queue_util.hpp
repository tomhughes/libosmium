#ifndef OSMIUM_IO_DETAIL_QUEUE_UTIL_HPP
#define OSMIUM_IO_DETAIL_QUEUE_UTIL_HPP

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

#include <algorithm>
#include <exception>
#include <future>
#include <string>

#include <osmium/memory/buffer.hpp>
#include <osmium/thread/queue.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            /**
             * This type of queue contains buffers with OSM data in them.
             * The "end of file" is marked by an invalid Buffer.
             * The buffers are wrapped in a std::future so that they can also
             * transport exceptions. The future also helps with keeping the
             * data in order.
             */
            using future_buffer_queue_type = osmium::thread::Queue<std::future<osmium::memory::Buffer>>;

            /**
             * This type of queue contains OSM file data in the form it is
             * stored on disk, ie encoded as XML, PBF, etc.
             * The "end of file" is marked by an empty string.
             */
            using string_queue_type = osmium::thread::Queue<std::string>;

            /**
             * This type of queue contains OSM file data in the form it is
             * stored on disk, ie encoded as XML, PBF, etc.
             * The "end of file" is marked by an empty string.
             * The strings are wrapped in a std::future so that they can also
             * transport exceptions. The future also helps with keeping the
             * data in order.
             */
            using future_string_queue_type = osmium::thread::Queue<std::future<std::string>>;

            template <class T>
            inline void add_to_queue(osmium::thread::Queue<std::future<T>>& queue, T&& data) {
                std::promise<T> promise;
                queue.push(promise.get_future());
                promise.set_value(std::forward<T>(data));
            }

            template <class T>
            inline void add_to_queue(osmium::thread::Queue<std::future<T>>& queue, std::exception_ptr&& exception) {
                std::promise<T> promise;
                queue.push(promise.get_future());
                promise.set_exception(std::move(exception));
            }

            template <class T>
            class queue_wrapper {

                using queue_type = osmium::thread::Queue<std::future<T>>;

                queue_type& m_queue;
                bool m_done;

            public:

                queue_wrapper(queue_type& queue) :
                    m_queue(queue),
                    m_done(false) {
                }

                ~queue_wrapper() noexcept {
                    drain();
                }

                void drain() {
                    while (!m_done) {
                        try {
                            pop();
                        } catch (...) {
                            // Ignore any exceptions.
                        }
                    }
                }

                bool is_done() const noexcept {
                    return m_done;
                }

                void check_is_done(const T& data) noexcept;

                T pop() {
                    if (m_done) {
                        return T{};
                    }
                    std::future<T> data_future;
                    m_queue.wait_and_pop(data_future);
                    T data = data_future.get();
                    check_is_done(data);
                    return data;
                }

            }; // class queue_wrapper

            template<>
            inline void queue_wrapper<osmium::memory::Buffer>::check_is_done(const osmium::memory::Buffer& data) noexcept {
                m_done = !data;
            }

            template<>
            inline void queue_wrapper<std::string>::check_is_done(const std::string& data) noexcept {
                m_done = data.empty();
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_QUEUE_UTIL_HPP
