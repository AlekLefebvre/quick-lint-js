// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#ifndef QUICK_LINT_JS_PORT_MEMORY_RESOURCE_H
#define QUICK_LINT_JS_PORT_MEMORY_RESOURCE_H

#include <boost/container/pmr/memory_resource.hpp>

namespace quick_lint_js {
using Memory_Resource = ::boost::container::pmr::memory_resource;

// Like boost::container::pmr::new_delete_resource, but without a dependency on
// dlmalloc.
Memory_Resource *new_delete_resource();
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
