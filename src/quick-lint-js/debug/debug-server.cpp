// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <quick-lint-js/feature.h>

#if QLJS_FEATURE_DEBUG_SERVER

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <mongoose.h>
#include <optional>
#include <quick-lint-js/assert.h>
#include <quick-lint-js/container/async-byte-queue.h>
#include <quick-lint-js/container/byte-buffer.h>
#include <quick-lint-js/container/vector-profiler.h>
#include <quick-lint-js/debug/debug-server-fs.h>
#include <quick-lint-js/debug/debug-server.h>
#include <quick-lint-js/debug/mongoose.h>
#include <quick-lint-js/json.h>
#include <quick-lint-js/logging/trace-flusher.h>
#include <quick-lint-js/port/thread.h>
#include <quick-lint-js/util/binary-writer.h>
#include <quick-lint-js/util/narrow-cast.h>
#include <string>
#include <string_view>

using namespace std::literals::string_view_literals;

namespace quick_lint_js {
namespace {
void write_vector_profiler_stats(byte_buffer &out_json);
}

class trace_flusher_websocket_backend final : public trace_flusher_backend {
 public:
  explicit trace_flusher_websocket_backend(::mg_connection *connection,
                                           debug_server *server)
      : connection_(connection), server_(server) {}

  void trace_thread_begin(trace_flusher_thread_index) override {}

  void trace_thread_end(trace_flusher_thread_index) override {}

  void trace_thread_write_data(trace_flusher_thread_index thread_index,
                               const std::byte *data,
                               std::size_t size) override {
    std::lock_guard<mutex> lock(this->mutex_);

    async_byte_queue &queue = this->thread_queues_[thread_index];
    queue.append_copy(data, size);
    queue.commit();
    server_->wake_up_server_thread();
  }

  // Called on the server thread.
  void flush_if_needed() {
    std::lock_guard<mutex> lock(this->mutex_);

    for (auto &[thread_index, queue] : this->thread_queues_) {
      std::size_t total_message_size = 0;

      {
        std::uint8_t header[sizeof(std::uint64_t)];
        binary_writer writer(header);
        writer.u64_le(thread_index);
        int ok = ::mg_send(this->connection_, header, sizeof(header));
        QLJS_ASSERT(ok);
        total_message_size += sizeof(header);
      }

      queue.take_committed(
          [&](const std::byte *data, std::size_t size) {
            // FIXME(strager): ::mg_send fails if size is 0. We shouldn't need
            // this size check, but async_byte_queue gives us empty chunks for
            // some reason. We should make async_byte_queue not give us empty
            // chunks.
            if (size > 0) {
              int ok = ::mg_send(this->connection_, data, size);
              QLJS_ASSERT(ok);
              total_message_size += size;
            }
          },
          [] {});

      ::mg_ws_wrap(this->connection_, total_message_size, WEBSOCKET_OP_BINARY);
    }
  }

 private:
  ::mg_connection *const connection_;
  debug_server *const server_;

  // Protected by mutex_:
  hash_map<trace_flusher_thread_index, async_byte_queue> thread_queues_;

  mutex mutex_;

  friend class debug_server;
};

debug_server::debug_server(trace_flusher *tracer) : tracer_(tracer) {}

debug_server::~debug_server() {
  if (this->server_thread_.joinable()) {
    this->stop_server_thread();
    this->server_thread_.join();
  }

  for (auto &backend : this->tracer_backends_) {
    this->tracer_->disable_backend(backend.get());
  }
}

void debug_server::set_listen_address(std::string_view address) {
  QLJS_ASSERT(!this->server_thread_.joinable());

  this->requested_listen_address_ = address;
}

void debug_server::start_server_thread() {
  QLJS_ASSERT(!this->server_thread_.joinable());

  this->init_data_.reset();
  this->init_error_.clear();

  this->server_thread_ = thread([this] { this->run_on_current_thread(); });
}

void debug_server::stop_server_thread() {
  std::unique_lock<mutex> lock(this->mutex_);
  this->stop_server_thread_ = true;
  this->wake_up_server_thread(lock);

  this->did_wait_for_server_start_ = false;
}

result<void, debug_server_io_error> debug_server::wait_for_server_start() {
  std::unique_lock<mutex> lock(this->mutex_);
  this->initialized_.wait(lock, [&] {
    return this->init_data_.has_value() || !this->init_error_.empty();
  });

  if (!this->init_error_.empty()) {
    return failed_result(debug_server_io_error{
        .error_message = this->init_error_,
    });
  }

  this->did_wait_for_server_start_ = true;
  return {};
}

std::string debug_server::url() const { return this->url("/"sv); }

std::string debug_server::url(std::string_view path) const {
  QLJS_ASSERT(this->did_wait_for_server_start_);

  std::string result;
  result.reserve(path.size() + 100);
  result += "http://"sv;
  {
    std::lock_guard<mutex> lock(this->mutex_);
    result += this->init_data_->actual_listen_address;
  }
  result += path;
  return result;
}

std::string debug_server::websocket_url(std::string_view path) const {
  QLJS_ASSERT(this->did_wait_for_server_start_);

  std::string result;
  result.reserve(path.size() + 100);
  result += "ws://"sv;
  {
    std::lock_guard<mutex> lock(this->mutex_);
    result += this->init_data_->actual_listen_address;
  }
  result += path;
  return result;
}

void debug_server::wake_up_server_thread() {
  std::unique_lock<mutex> lock(this->mutex_);
  this->wake_up_server_thread(lock);
}

void debug_server::wake_up_server_thread(std::unique_lock<mutex> &) {
  if (this->init_data_.has_value()) {
    char wakeup_signal[] = {0};
    ::ssize_t rc = ::send(this->init_data_->wakeup_pipe, wakeup_signal,
                          sizeof(wakeup_signal), /*flags=*/0);
    QLJS_ALWAYS_ASSERT(rc == 1);
  }
}

void debug_server::run_on_current_thread() {
  mongoose_mgr mgr;

  std::string connect_logs;
  mongoose_begin_capturing_logs_on_current_thread(&connect_logs);
  ::mg_connection *server_connection = ::mg_http_listen(
      mgr.get(), this->requested_listen_address_.c_str(),
      mongoose_callback<&debug_server::http_server_callback>(), this);
  mongoose_stop_capturing_logs_on_current_thread();
  if (!server_connection) {
    std::lock_guard<mutex> lock(this->mutex_);
    if (connect_logs.empty()) {
      this->init_error_ = "unknown error in mg_http_listen";
    } else {
      this->init_error_ = std::move(connect_logs);
    }
    this->initialized_.notify_all();
    return;
  }

  {
    std::lock_guard<mutex> lock(this->mutex_);
    QLJS_ASSERT(!this->init_data_.has_value());
    this->init_data_.emplace();

    // server_connection->loc is initialized synchronously, so we should be able
    // to use c->loc now.
    std::string &address = this->init_data_->actual_listen_address;
    address.resize(100);
    ::mg_straddr(&server_connection->loc, address.data(), address.size());
    address.resize(std::strlen(address.c_str()));

    this->init_data_->wakeup_pipe = ::mg_mkpipe(
        mgr.get(), mongoose_callback<&debug_server::wakeup_pipe_callback>(),
        this,
        /*udp=*/false);
    QLJS_ALWAYS_ASSERT(this->init_data_->wakeup_pipe != -1);

    this->initialized_.notify_all();
  }

  while (!this->stop_server_thread_.load()) {
    ::mg_mgr_poll(mgr.get(), /*timeout_ms=*/-1);
  }
}

void debug_server::begin_closing_all_connections(::mg_mgr *mgr) {
  ::mg_connection *c = mgr->conns;
  while (c) {
    c->is_closing = true;
    c = c->next;
  }
}

void debug_server::http_server_callback(::mg_connection *c, int ev,
                                        void *ev_data) noexcept {
  switch (ev) {
  case ::MG_EV_HTTP_MSG: {
    ::mg_http_message *hm = static_cast<::mg_http_message *>(ev_data);
    if (::mg_http_match_uri(hm, "/vector-profiler-stats")) {
      byte_buffer json;
      write_vector_profiler_stats(json);

      // TODO(strager): Optimize.
      std::string json_copy;
      json_copy.resize(json.size());
      json.copy_to(json_copy.data());

      ::mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%.*s",
                      narrow_cast<int>(json_copy.size()), json_copy.data());
    } else if (this->tracer_ && ::mg_http_match_uri(hm, "/api/trace")) {
      ::mg_ws_upgrade(c, hm, nullptr);
    } else {
      std::string public_directory = get_debug_server_public_directory();
      ::mg_http_serve_opts options = {
          .root_dir = public_directory.c_str(),
          .ssi_pattern = nullptr,
          .extra_headers = nullptr,
          .mime_types = "mjs=text/javascript",
          .page404 = nullptr,
          .fs = nullptr,
      };
      ::mg_http_serve_dir(c, hm, &options);
    }
    break;
  }

  case ::MG_EV_WS_OPEN: {
    this->tracer_backends_.emplace_back(
        std::make_unique<trace_flusher_websocket_backend>(c, this));
    trace_flusher_websocket_backend *backend =
        this->tracer_backends_.back().get();
    this->tracer_->enable_backend(backend);
    break;
  }

  case ::MG_EV_CLOSE: {
    auto backend_it = std::find_if(
        this->tracer_backends_.begin(), this->tracer_backends_.end(),
        [&](auto &backend) { return backend->connection_ == c; });
    if (backend_it != this->tracer_backends_.end()) {
      this->tracer_->disable_backend(backend_it->get());
      this->tracer_backends_.erase(backend_it);
    }
    break;
  }

  default:
    break;
  }
}

void debug_server::wakeup_pipe_callback(::mg_connection *c, int ev,
                                        void *) noexcept {
  switch (ev) {
  case ::MG_EV_READ:
    // wake_up_server_thread was called.
    if (this->stop_server_thread_.load()) {
      this->begin_closing_all_connections(c->mgr);
    }

    for (auto &backend : this->tracer_backends_) {
      backend->flush_if_needed();
    }
    break;

  default:
    break;
  }
}

namespace {
void write_vector_profiler_stats(byte_buffer &out_json) {
  out_json.append_copy(u8R"--({"maxSizeHistogramByOwner":{)--"_sv);

#if QLJS_FEATURE_VECTOR_PROFILING
  std::map<std::string_view, std::map<std::size_t, int>> histograms_by_owner =
      vector_instrumentation::instance.max_size_histogram_by_owner();
  bool need_comma = false;
  for (auto &[owner, histogram] : histograms_by_owner) {
    if (need_comma) {
      out_json.append_copy(u8',');
    }
    out_json.append_copy(u8'"');
    write_json_escaped_string(out_json, to_string8_view(owner));
    out_json.append_copy(u8"\":["_sv);

    bool need_array_comma = false;
    std::size_t last_size = static_cast<std::size_t>(-1);
    for (auto it = histogram.begin(); it != histogram.end(); ++it) {
      std::size_t cur_size = it->first;
      int count = it->second;

      // Fill in zeroes for empty spans in the histogram map.
      for (std::size_t size = last_size + 1; size < cur_size; ++size) {
        if (need_array_comma) {
          out_json.append_copy(u8',');
        }
        out_json.append_copy(u8'0');
        need_array_comma = true;
      }

      if (need_array_comma) {
        out_json.append_copy(u8',');
      }
      out_json.append_decimal_integer(count);
      need_array_comma = true;

      last_size = cur_size;
    }

    out_json.append_copy(u8"]"_sv);
    need_comma = true;
  }
#endif

  out_json.append_copy(u8"}}"_sv);
}
}
}

#endif

// quick-lint-js finds bugs in JavaScript programs.
// Copyright (C) 2020  Matthew "strager" Glazar
//
// This file is part of quick-lint-js.
//
// quick-lint-js is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// quick-lint-js is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with quick-lint-js.  If not, see <https://www.gnu.org/licenses/>.
