// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#ifndef QUICK_LINT_JS_LOGGING_TRACE_WRITER_H
#define QUICK_LINT_JS_LOGGING_TRACE_WRITER_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <quick-lint-js/assert.h>
#include <quick-lint-js/container/async-byte-queue.h>
#include <quick-lint-js/port/char8.h>
#include <quick-lint-js/port/span.h>
#include <quick-lint-js/util/binary-writer.h>
#include <string_view>

namespace quick_lint_js {
struct Trace_Context {
  std::uint64_t thread_id;
};

struct Trace_Event_Init {
  static constexpr std::uint8_t id = 0x01;

  std::uint64_t timestamp;
  String8_View version;
};

struct Trace_Event_VSCode_Document_Opened {
  static constexpr std::uint8_t id = 0x02;

  std::uint64_t timestamp;
  std::uint64_t document_id;
  void* uri;
  void* language_id;
  void* content;
};

struct Trace_Event_VSCode_Document_Closed {
  static constexpr std::uint8_t id = 0x03;

  std::uint64_t timestamp;
  std::uint64_t document_id;
  void* uri;
  void* language_id;
};

struct Trace_VSCode_Document_Position {
  std::uint64_t line;
  std::uint64_t character;
};

struct Trace_VSCode_Document_Range {
  Trace_VSCode_Document_Position start;
  Trace_VSCode_Document_Position end;
};

struct Trace_VSCode_Document_Change {
  Trace_VSCode_Document_Range range;
  std::uint64_t range_offset;
  std::uint64_t range_length;
  void* text;
};

struct Trace_Event_VSCode_Document_Changed {
  static constexpr std::uint8_t id = 0x04;

  std::uint64_t timestamp;
  std::uint64_t document_id;
  const Trace_VSCode_Document_Change* changes;
  std::uint64_t change_count;
};

struct Trace_Event_VSCode_Document_Sync {
  static constexpr std::uint8_t id = 0x05;

  std::uint64_t timestamp;
  std::uint64_t document_id;
  void* uri;
  void* language_id;
  void* content;
};

struct Trace_Event_LSP_Client_To_Server_Message {
  static constexpr std::uint8_t id = 0x06;

  std::uint64_t timestamp;
  String8_View body;
};

struct Trace_Event_Vector_Max_Size_Histogram_By_Owner {
  static constexpr std::uint8_t id = 0x07;

  std::uint64_t timestamp;
  std::map<std::string_view, std::map<std::size_t, int>>* histogram;
};

struct Trace_Event_Process_ID {
  static constexpr std::uint8_t id = 0x08;

  std::uint64_t timestamp;
  std::uint64_t process_id;
};

enum class Trace_LSP_Document_Type : std::uint8_t {
  unknown = 0,
  config = 1,
  lintable = 2,
};

struct Trace_LSP_Document_State {
  Trace_LSP_Document_Type type;
  String8_View uri;
  String8_View text;
  String8_View language_id;
};

struct Trace_Event_LSP_Documents {
  static constexpr std::uint8_t id = 0x09;

  std::uint64_t timestamp;
  Span<const Trace_LSP_Document_State> documents;
};

class Trace_Writer {
 public:
  explicit Trace_Writer(Async_Byte_Queue*);

  // Calls async_byte_queue::commit.
  void commit();

  void write_header(const Trace_Context&);

  void write_event_init(const Trace_Event_Init&);

  template <class String_Writer>
  void write_event_vscode_document_opened(
      const Trace_Event_VSCode_Document_Opened&, String_Writer&&);

  template <class String_Writer>
  void write_event_vscode_document_closed(
      const Trace_Event_VSCode_Document_Closed&, String_Writer&&);

  template <class String_Writer>
  void write_event_vscode_document_changed(
      const Trace_Event_VSCode_Document_Changed&, String_Writer&&);

  template <class String_Writer>
  void write_event_vscode_document_sync(const Trace_Event_VSCode_Document_Sync&,
                                        String_Writer&&);

  void write_event_lsp_client_to_server_message(
      const Trace_Event_LSP_Client_To_Server_Message&);

  void write_event_vector_max_size_histogram_by_owner(
      const Trace_Event_Vector_Max_Size_Histogram_By_Owner&);

  void write_event_process_id(const Trace_Event_Process_ID&);

  void write_event_lsp_documents(const Trace_Event_LSP_Documents&);

 private:
  template <class Func>
  void append_binary(Async_Byte_Queue::Size_Type size, Func&& callback);

  template <class String_Writer>
  void write_utf16le_string(void* string, String_Writer&);

  void write_utf8_string(String8_View);

  Async_Byte_Queue* out_;
};

template <class String_Writer>
void Trace_Writer::write_event_vscode_document_opened(
    const Trace_Event_VSCode_Document_Opened& event,
    String_Writer&& string_writer) {
  this->append_binary(8 + 1 + 8, [&](Binary_Writer& w) {
    w.u64_le(event.timestamp);
    w.u8(event.id);
    w.u64_le(event.document_id);
  });
  this->write_utf16le_string(event.uri, string_writer);
  this->write_utf16le_string(event.language_id, string_writer);
  this->write_utf16le_string(event.content, string_writer);
}

template <class String_Writer>
void Trace_Writer::write_event_vscode_document_closed(
    const Trace_Event_VSCode_Document_Closed& event,
    String_Writer&& string_writer) {
  this->append_binary(8 + 1 + 8, [&](Binary_Writer& w) {
    w.u64_le(event.timestamp);
    w.u8(event.id);
    w.u64_le(event.document_id);
  });
  this->write_utf16le_string(event.uri, string_writer);
  this->write_utf16le_string(event.language_id, string_writer);
}

template <class String_Writer>
void Trace_Writer::write_event_vscode_document_changed(
    const Trace_Event_VSCode_Document_Changed& event,
    String_Writer&& string_writer) {
  this->append_binary(8 + 1 + 8 + 8, [&](Binary_Writer& w) {
    w.u64_le(event.timestamp);
    w.u8(event.id);
    w.u64_le(event.document_id);
    w.u64_le(event.change_count);
  });
  for (std::uint64_t i = 0; i < event.change_count; ++i) {
    const Trace_VSCode_Document_Change* change = &event.changes[i];
    this->append_binary(8 * 6, [&](Binary_Writer& w) {
      w.u64_le(change->range.start.line);
      w.u64_le(change->range.start.character);
      w.u64_le(change->range.end.line);
      w.u64_le(change->range.end.character);
      w.u64_le(change->range_offset);
      w.u64_le(change->range_length);
    });
    this->write_utf16le_string(change->text, string_writer);
  }
}

template <class Func>
void Trace_Writer::append_binary(Async_Byte_Queue::Size_Type size,
                                 Func&& callback) {
  std::uint8_t* data_begin =
      reinterpret_cast<std::uint8_t*>(this->out_->append(size));
  Binary_Writer w(data_begin);
  callback(w);
  QLJS_ASSERT(w.bytes_written_since(data_begin) == size);
}

template <class String_Writer>
void Trace_Writer::write_utf16le_string(void* string,
                                        String_Writer& string_writer) {
  std::size_t code_unit_count = string_writer.string_size(string);
  // HACK(strager): Reserve an extra code unit for a null terminator. This is
  // required when interacting with N-API in the Visual Studio Code extension.
  std::size_t capacity = code_unit_count + 1;
  this->append_binary(8, [&](Binary_Writer& w) { w.u64_le(code_unit_count); });
  this->out_->append_aligned(
      capacity * sizeof(char16_t), alignof(char16_t), [&](void* data) {
        string_writer.copy_string(string, reinterpret_cast<char16_t*>(data),
                                  capacity);
        return code_unit_count * sizeof(char16_t);
      });
}

template <class String_Writer>
void Trace_Writer::write_event_vscode_document_sync(
    const Trace_Event_VSCode_Document_Sync& event,
    String_Writer&& string_writer) {
  this->append_binary(8 + 1 + 8, [&](Binary_Writer& w) {
    w.u64_le(event.timestamp);
    w.u8(event.id);
    w.u64_le(event.document_id);
  });
  this->write_utf16le_string(event.uri, string_writer);
  this->write_utf16le_string(event.language_id, string_writer);
  this->write_utf16le_string(event.content, string_writer);
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
