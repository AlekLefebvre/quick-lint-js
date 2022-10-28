// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <quick-lint-js/feature.h>

#if QLJS_FEATURE_DEBUG_SERVER

#include <cctype>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mongoose.h>
#include <optional>
#include <quick-lint-js/container/vector-profiler.h>
#include <quick-lint-js/debug/debug-server.h>
#include <quick-lint-js/debug/mongoose.h>
#include <quick-lint-js/logging/trace-flusher.h>
#include <quick-lint-js/parse-json.h>
#include <quick-lint-js/port/thread.h>
#include <quick-lint-js/trace-stream-reader-mock.h>
#include <quick-lint-js/util/algorithm.h>
#include <quick-lint-js/util/binary-reader.h>
#include <quick-lint-js/util/narrow-cast.h>
#include <quick-lint-js/version.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::literals::string_view_literals;

namespace quick_lint_js {
namespace {
struct http_response {
  std::optional<int> status;
  std::string data;
  std::vector<std::pair<std::string, std::string>> headers;

  std::string_view get_last_header_value_or_empty(
      std::string_view header_name) const;

  ::boost::json::value json() { return parse_boost_json(this->data); }

  static http_response failed([[maybe_unused]] std::string_view error_message) {
    return http_response();
  }

  // Returns false if there was a failure when making the request.
  /*implicit*/ operator bool() const noexcept {
    return this->status.has_value();
  }
};

http_response http_fetch(const char *url);
http_response http_fetch(const std::string &url);

class http_websocket_client;

class http_websocket_client_delegate {
 public:
  virtual ~http_websocket_client_delegate() = default;

  virtual void on_message_binary(http_websocket_client *, const void *message,
                                 std::size_t message_size) = 0;
};

class http_websocket_client {
 public:
  static void connect_and_run(const char *url,
                              http_websocket_client_delegate *);

  void stop();

 private:
  void callback(::mg_connection *c, int ev, void *ev_data);

  explicit http_websocket_client(http_websocket_client_delegate *);

  bool done_ = false;
  http_websocket_client_delegate *delegate_;
  mongoose_mgr mgr_;

  ::mg_connection *current_connection_;
};

class test_debug_server : public ::testing::Test {};

TEST_F(test_debug_server, start_thread_then_immediately_stop) {
  debug_server server(/*tracer=*/nullptr);
  server.start_server_thread();
  server.stop_server_thread();
}

TEST_F(test_debug_server, serves_html_at_index) {
  debug_server server(/*tracer=*/nullptr);
  server.start_server_thread();
  auto wait_result = server.wait_for_server_start();
  ASSERT_TRUE(wait_result.ok()) << wait_result.error_to_string();

  http_response response = http_fetch(server.url("/"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response.status, 200);  // OK
  EXPECT_THAT(response.data, ::testing::StartsWith("<!DOCTYPE html>"));
  EXPECT_THAT(response.get_last_header_value_or_empty("content-type"),
              ::testing::AnyOf(::testing::StrEq("text/html"),
                               ::testing::StartsWith("text/html;")));
}

TEST_F(test_debug_server, serves_javascript) {
  debug_server server(/*tracer=*/nullptr);
  server.start_server_thread();
  auto wait_result = server.wait_for_server_start();
  ASSERT_TRUE(wait_result.ok()) << wait_result.error_to_string();

  http_response response = http_fetch(server.url("/index.mjs"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response.status, 200);  // OK
  EXPECT_THAT(response.data, ::testing::StartsWith("//"));
  EXPECT_THAT(response.get_last_header_value_or_empty("content-type"),
              ::testing::AnyOf(::testing::StrEq("text/javascript"),
                               ::testing::StartsWith("text/javascript;")));
}

TEST_F(test_debug_server, serves_not_found_page) {
  debug_server server(/*tracer=*/nullptr);
  server.start_server_thread();
  auto wait_result = server.wait_for_server_start();
  ASSERT_TRUE(wait_result.ok()) << wait_result.error_to_string();

  http_response response = http_fetch(server.url("/route-does-not-exist"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response.status, 404);  // Not Found
}

TEST_F(test_debug_server, two_servers_listening_on_same_port_fails) {
  debug_server server_a(/*tracer=*/nullptr);
  server_a.start_server_thread();
  auto a_wait_result = server_a.wait_for_server_start();
  ASSERT_TRUE(a_wait_result.ok()) << a_wait_result.error_to_string();

  std::string server_a_address = server_a.url("");
  SCOPED_TRACE("server_a address: " + server_a_address);

  debug_server server_b(/*tracer=*/nullptr);
  server_b.set_listen_address(server_a_address);
  server_b.start_server_thread();
  auto b_wait_result = server_b.wait_for_server_start();
  EXPECT_FALSE(b_wait_result.ok());
}

#if QLJS_FEATURE_VECTOR_PROFILING
TEST_F(test_debug_server, vector_profiler_stats) {
  debug_server server(/*tracer=*/nullptr);
  server.start_server_thread();
  auto wait_result = server.wait_for_server_start();
  ASSERT_TRUE(wait_result.ok()) << wait_result.error_to_string();

  vector_instrumentation::instance.clear();
  {
    instrumented_vector<std::vector<int>> v1("debug server test vector", {});
    v1.push_back(100);
    v1.push_back(200);
    ASSERT_EQ(v1.size(), 2);

    instrumented_vector<std::vector<int>> v2("debug server test vector", {});
    v2.push_back(100);
    v2.push_back(200);
    v2.push_back(300);
    v2.push_back(400);
    ASSERT_EQ(v2.size(), 4);
  }

  http_response response = http_fetch(server.url("/vector-profiler-stats"));
  ASSERT_TRUE(response);
  SCOPED_TRACE(response.data);
  EXPECT_EQ(response.status, 200);  // OK
  EXPECT_EQ(response.get_last_header_value_or_empty("content-type"),
            "application/json");

  ::boost::json::value data = response.json();
  ::boost::json::array histogram =
      look_up(data, "maxSizeHistogramByOwner", "debug server test vector")
          .as_array();
  EXPECT_THAT(histogram, ::testing::ElementsAre(
                             /*[0]=*/0,
                             /*[1]=*/0,
                             /*[2]=*/1,
                             /*[3]=*/0,
                             /*[4]=*/1));
}
#else
TEST_F(test_debug_server, vector_profiler_stats_yields_no_data_if_disabled) {
  debug_server server(/*tracer=*/nullptr);
  server.start_server_thread();
  auto wait_result = server.wait_for_server_start();
  ASSERT_TRUE(wait_result.ok()) << wait_result.error_to_string();

  http_response response = http_fetch(server.url("/vector-profiler-stats"));
  ASSERT_TRUE(response);
  SCOPED_TRACE(response.data);
  EXPECT_EQ(response.status, 200);  // OK
  EXPECT_EQ(response.get_last_header_value_or_empty("content-type"),
            "application/json");

  ::boost::json::value data = response.json();
  EXPECT_THAT(look_up(data, "maxSizeHistogramByOwner").as_object(),
              ::testing::IsEmpty());
}
#endif

TEST_F(test_debug_server, trace_websocket_sends_trace_data) {
  trace_flusher tracer;

  mutex test_mutex;
  condition_variable cond;
  bool registered_other_thread = false;
  bool finished_test = false;

  // Register two threads. The WebSocket server should send data for each thread
  // in separate messages.
  tracer.register_current_thread();
  thread other_thread([&]() {
    tracer.register_current_thread();

    {
      std::unique_lock<mutex> lock(test_mutex);
      registered_other_thread = true;
      cond.notify_all();
      cond.wait(lock, [&] { return finished_test; });
    }

    tracer.unregister_current_thread();
  });

  {
    std::unique_lock<mutex> lock(test_mutex);
    cond.wait(lock, [&] { return registered_other_thread; });
  }

  debug_server server(&tracer);
  server.start_server_thread();
  auto wait_result = server.wait_for_server_start();
  ASSERT_TRUE(wait_result.ok()) << wait_result.error_to_string();

  class test_delegate : public http_websocket_client_delegate {
   public:
    void on_message_binary(http_websocket_client *client, const void *message,
                           std::size_t message_size) override {
      if (message_size < 8) {
        ADD_FAILURE() << "unexpected tiny message (size=" << message_size
                      << ")";
        client->stop();
        return;
      }

      checked_binary_reader reader(
          reinterpret_cast<const std::uint8_t *>(message), message_size);
      std::uint64_t thread_index = reader.u64_le();
      this->received_thread_indexes.push_back(thread_index);

      // FIXME(strager): This test assumes that we receive both the header and
      // the init event in the same message, which is not guaranteed.
      strict_mock_trace_stream_event_visitor v;
      EXPECT_CALL(v, visit_packet_header(::testing::_));
      EXPECT_CALL(v, visit_init_event(::testing::Field(
                         &trace_stream_event_visitor::init_event::version,
                         ::testing::StrEq(QUICK_LINT_JS_VERSION_STRING))));
      read_trace_stream(reader.cursor(), reader.remaining(), v);

      // We expect messages for only two threads (main and other).
      if (this->received_thread_indexes.size() >= 2) {
        client->stop();
      }
    }

    std::vector<trace_flusher_thread_index> received_thread_indexes;
  };
  test_delegate delegate;
  http_websocket_client::connect_and_run(
      server.websocket_url("/api/trace").c_str(), &delegate);

  EXPECT_THAT(delegate.received_thread_indexes,
              ::testing::UnorderedElementsAre(1, 2));

  {
    std::lock_guard<mutex> lock(test_mutex);
    finished_test = true;
    cond.notify_all();
  }
  other_thread.join();
}

bool strings_equal_case_insensitive(std::string_view a, std::string_view b) {
  return ranges_equal(
      a, b, [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}

std::string_view http_response::get_last_header_value_or_empty(
    std::string_view header_name) const {
  for (const auto &[name, value] : this->headers) {
    if (strings_equal_case_insensitive(name, header_name)) {
      return value;
    }
  }
  return ""sv;
}

http_response http_fetch(const char *url) {
  mongoose_mgr mgr;

  struct state {
    const char *request_url;
    http_response response;
    bool done = false;
    std::string protocol_error;

    void callback(::mg_connection *c, int ev, void *ev_data) {
      switch (ev) {
      case ::MG_EV_CONNECT: {
        ::mg_str host = ::mg_url_host(this->request_url);
        ::mg_printf(c,
                    "GET %s HTTP/1.1\r\n"
                    "Host: %.*s\r\n"
                    "\r\n",
                    ::mg_url_uri(this->request_url), narrow_cast<int>(host.len),
                    host.ptr);
        break;
      }

      case ::MG_EV_HTTP_MSG: {
        ::mg_http_message *hm = static_cast<::mg_http_message *>(ev_data);
        this->response.data.assign(hm->body.ptr, hm->body.len);
        this->response.status = ::mg_http_status(hm);

        for (int i = 0; i < MG_MAX_HTTP_HEADERS; ++i) {
          const ::mg_http_header &h = hm->headers[i];
          if (h.name.len == 0) {
            break;
          }
          this->response.headers.emplace_back(
              std::string(h.name.ptr, h.name.len),
              std::string(h.value.ptr, h.value.len));
        }

        this->done = true;
        c->is_closing = true;
        break;
      }

      case ::MG_EV_ERROR:
        this->protocol_error = static_cast<const char *>(ev_data);
        this->done = true;
        c->is_closing = true;
        break;

      default:
        break;
      }
    }
  };

  state s;
  s.request_url = url;
  ::mg_connection *connection = ::mg_http_connect(
      mgr.get(), s.request_url, mongoose_callback<&state::callback>(), &s);
  if (!connection) {
    ADD_FAILURE() << "mg_http_connect failed";
    return http_response::failed("mg_http_connect failed");
  }

  while (!s.done) {
    int timeout_ms = 1000;
    ::mg_mgr_poll(mgr.get(), timeout_ms);
  }

  if (!s.protocol_error.empty()) {
    ADD_FAILURE() << "protocol error: " << s.protocol_error;
    return http_response::failed(s.protocol_error);
  }

  return std::move(s.response);
}

http_response http_fetch(const std::string &url) {
  return http_fetch(url.c_str());
}

void http_websocket_client::connect_and_run(
    const char *url, http_websocket_client_delegate *delegate) {
  http_websocket_client client(delegate);
  ::mg_connection *connection = ::mg_ws_connect(
      client.mgr_.get(), url,
      mongoose_callback<&http_websocket_client::callback>(), &client, nullptr);
  if (!connection) {
    ADD_FAILURE() << "mg_ws_connect failed";
    return;
  }

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!client.done_) {
    if (std::chrono::steady_clock::now() >= deadline) {
      ADD_FAILURE() << "timed out";
      return;
    }
    int timeout_ms = 100;
    ::mg_mgr_poll(client.mgr_.get(), timeout_ms);
  }
}

void http_websocket_client::stop() {
  this->done_ = true;
  this->current_connection_->is_closing = true;
}

void http_websocket_client::callback(::mg_connection *c, int ev,
                                     void *ev_data) {
  this->current_connection_ = c;

  switch (ev) {
  case ::MG_EV_WS_MSG: {
    ::mg_ws_message *wm = static_cast<::mg_ws_message *>(ev_data);
    int websocket_operation = wm->flags & 0xf;
    switch (websocket_operation) {
    case WEBSOCKET_OP_BINARY:
      this->delegate_->on_message_binary(this, wm->data.ptr, wm->data.len);
      break;

    default:
      QLJS_UNIMPLEMENTED();
      break;
    }
    break;
  }

  case ::MG_EV_ERROR:
    ADD_FAILURE() << "protocol error: " << static_cast<const char *>(ev_data);
    this->stop();
    break;

  default:
    break;
  }

  this->current_connection_ = nullptr;
}

http_websocket_client::http_websocket_client(
    http_websocket_client_delegate *delegate)
    : delegate_(delegate) {}
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
