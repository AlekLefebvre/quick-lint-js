// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <quick-lint-js/diag-collector.h>
#include <quick-lint-js/diag-matcher.h>
#include <quick-lint-js/fe/global-declared-variable-set.h>
#include <quick-lint-js/fe/language.h>
#include <quick-lint-js/fe/variable-analyzer.h>
#include <quick-lint-js/identifier-support.h>
#include <quick-lint-js/port/char8.h>
#include <quick-lint-js/variable-analyzer-support.h>

using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

namespace quick_lint_js {
namespace {
TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_initialized_var_without_use_in_block_scope_is_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";

  struct Test_Case {
    Variable_Kind outer_declaration_kind;
    Variable_Kind inner_declaration_kind;
  };
  for (Test_Case tc : {
           Test_Case{Variable_Kind::_const, Variable_Kind::_const},
           Test_Case{Variable_Kind::_const, Variable_Kind::_let},
           Test_Case{Variable_Kind::_let, Variable_Kind::_const},
           Test_Case{Variable_Kind::_let, Variable_Kind::_let},
           Test_Case{Variable_Kind::_var, Variable_Kind::_const},
           Test_Case{Variable_Kind::_var, Variable_Kind::_let},
       }) {
    SCOPED_TRACE(tc.outer_declaration_kind);
    SCOPED_TRACE(tc.inner_declaration_kind);

    {
      // // const/let/etc.
      // let x = 5;
      // {
      //   // const/let/etc.
      //   let x = 6;  // WARNING
      // }
      Diag_Collector v;
      Variable_Analyzer l(&v, &default_globals, javascript_var_options);
      l.visit_variable_declaration(
          identifier_of(outer_declaration), tc.outer_declaration_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_enter_block_scope();
      l.visit_variable_declaration(
          identifier_of(inner_declaration), tc.inner_declaration_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_exit_block_scope();
      l.visit_end_of_module();

      EXPECT_THAT(v.errors,
                  ElementsAreArray({
                      DIAG_TYPE_2_SPANS(
                          Diag_Unused_Variable_Shadows,                       //
                          shadowing_declaration, span_of(inner_declaration),  //
                          shadowed_declaration, span_of(outer_declaration)),
                  }));
    }

    // TODO(strager): See NOTE[unused-var-shadows-nested-block].
    if ((false)) {
      // // const/let/etc.
      // let x = 5;
      // {
      //   {
      //     // const/let/etc.
      //     let x = 6;  // WARNING
      //   }
      // }
      Diag_Collector v;
      Variable_Analyzer l(&v, &default_globals, javascript_var_options);
      l.visit_variable_declaration(
          identifier_of(outer_declaration), tc.outer_declaration_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_enter_block_scope();
      l.visit_enter_block_scope();
      l.visit_variable_declaration(
          identifier_of(inner_declaration), tc.inner_declaration_kind,
          Variable_Declaration_Flags::initialized_with_equals);
      l.visit_exit_block_scope();
      l.visit_exit_block_scope();
      l.visit_end_of_module();

      EXPECT_THAT(v.errors,
                  ElementsAreArray({
                      DIAG_TYPE_2_SPANS(
                          Diag_Unused_Variable_Shadows,                       //
                          shadowing_declaration, span_of(inner_declaration),  //
                          shadowed_declaration, span_of(outer_declaration)),
                  }));
    }
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_function_scope_var_without_use_in_block_scope_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";

  {
    // var x = 5;
    // {
    //   var x = 6;  // no warning
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_var,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_var,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_unassigned_var_in_block_scope_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";

  {
    // let x = 5;
    // {
    //   let x;  // no warning
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(identifier_of(inner_declaration),
                                 Variable_Kind::_let,
                                 Variable_Declaration_Flags::none);
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_var_without_use_in_function_scope_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";

  {
    // let x = 5;
    // (function() {
    //   let x = 6;  // no warning
    // });
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_function_scope();
    l.visit_enter_function_scope_body();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_exit_function_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_parameter_is_not_a_warning) {
  const Char8 parameter[] = u8"x";
  const Char8 let[] = u8"x";

  // (function(x) {
  //   {
  //     let x = 6;  // no warning
  //   }
  // });
  Diag_Collector v;
  Variable_Analyzer l(&v, &default_globals, javascript_var_options);
  l.visit_enter_function_scope();
  l.visit_variable_declaration(identifier_of(parameter),
                               Variable_Kind::_function_parameter,
                               Variable_Declaration_Flags::none);
  l.visit_enter_function_scope_body();
  l.visit_enter_block_scope();
  l.visit_variable_declaration(
      identifier_of(let), Variable_Kind::_let,
      Variable_Declaration_Flags::initialized_with_equals);
  l.visit_exit_block_scope();
  l.visit_exit_function_scope();
  l.visit_end_of_module();

  EXPECT_THAT(v.errors, IsEmpty());
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_class_or_function_or_import_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"C";
  const Char8 inner_declaration[] = u8"C";

  for (Variable_Kind outer_kind :
       {Variable_Kind::_class, Variable_Kind::_function,
        Variable_Kind::_import}) {
    SCOPED_TRACE(outer_kind);

    // class C {}
    // // or
    // function C() {}
    // // or
    // import {C} from "module";
    //
    // {
    //   let C = 6;  // no warning
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(identifier_of(outer_declaration), outer_kind,
                                 Variable_Declaration_Flags::none);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     shadowing_catch_variable_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"e";
  const Char8 inner_declaration[] = u8"e";

  // try {
  // } catch (e) {
  //   {
  //     let e = 6;  // no warning
  //   }
  // }
  Diag_Collector v;
  Variable_Analyzer l(&v, &default_globals, javascript_var_options);
  l.visit_enter_block_scope();  // try
  l.visit_exit_block_scope();
  l.visit_enter_block_scope();  // catch
  l.visit_variable_declaration(identifier_of(outer_declaration),
                               Variable_Kind::_catch,
                               Variable_Declaration_Flags::none);
  l.visit_enter_block_scope();
  l.visit_variable_declaration(
      identifier_of(inner_declaration), Variable_Kind::_let,
      Variable_Declaration_Flags::initialized_with_equals);
  l.visit_exit_block_scope();
  l.visit_exit_block_scope();
  l.visit_end_of_module();

  EXPECT_THAT(v.errors, IsEmpty());
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     using_shadowing_variable_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";
  const Char8 use[] = u8"x";

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   x;
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_variable_use(identifier_of(use));
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   {
    //     x;
    //   }
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_use(identifier_of(use));
    l.visit_exit_block_scope();
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }

  {
    // let x = 5;
    // {
    //   let x = 6;    // no warning
    //   (function() {
    //     x;
    //   });
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_function_scope();
    l.visit_enter_function_scope_body();
    l.visit_variable_use(identifier_of(use));
    l.visit_exit_function_scope();
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     using_shadowing_variable_before_its_declaration_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";
  const Char8 use[] = u8"x";

  {
    // let x = 5;
    // {
    //   x;          // ERROR
    //   let x = 6;  // no warning
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_use(identifier_of(use));
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, ElementsAreArray({
                              DIAG_TYPE(Diag_Variable_Used_Before_Declaration),
                          }));
  }

  {
    // let x = 5;
    // {
    //   {
    //     x;        // ERROR
    //   }
    //   let x = 6;  // no warning
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_enter_block_scope();
    l.visit_variable_use(identifier_of(use));
    l.visit_exit_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, ElementsAreArray({
                              DIAG_TYPE(Diag_Variable_Used_Before_Declaration),
                          }));
  }

  {
    // let x = 5;
    // {
    //   (function() {
    //     x;          // no error
    //   });
    //   let x = 6;    // no warning
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_enter_function_scope();
    l.visit_enter_function_scope_body();
    l.visit_variable_use(identifier_of(use));
    l.visit_exit_function_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     using_shadowing_variable_with_eval_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";
  const Char8 use_eval[] = u8"eval";

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   eval("x");
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_variable_use(identifier_of(use_eval));
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   {
    //     eval("x");
    //   }
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_use(identifier_of(use_eval));
    l.visit_exit_block_scope();
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   {
    //     {
    //       eval("x");
    //     }
    //   }
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_enter_block_scope();
    l.visit_variable_use(identifier_of(use_eval));
    l.visit_exit_block_scope();
    l.visit_exit_block_scope();
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   (function() {
    //     eval("x");
    //   });
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_function_scope();
    l.visit_enter_function_scope_body();
    l.visit_variable_use(identifier_of(use_eval));
    l.visit_exit_function_scope();
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   (function() {
    //     (function() {
    //       eval("x");
    //     });
    //   });
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_function_scope();
    l.visit_enter_function_scope_body();
    l.visit_enter_function_scope();
    l.visit_enter_function_scope_body();
    l.visit_variable_use(identifier_of(use_eval));
    l.visit_exit_function_scope();
    l.visit_exit_function_scope();
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}

TEST(Test_Variable_Analyzer_Unused_Shadow,
     assigning_to_shadowing_variable_is_not_a_warning) {
  const Char8 outer_declaration[] = u8"x";
  const Char8 inner_declaration[] = u8"x";
  const Char8 assignment[] = u8"x";

  {
    // let x = 5;
    // {
    //   let x = 6;  // no warning
    //   x = 7;
    // }
    Diag_Collector v;
    Variable_Analyzer l(&v, &default_globals, javascript_var_options);
    l.visit_variable_declaration(
        identifier_of(outer_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_enter_block_scope();
    l.visit_variable_declaration(
        identifier_of(inner_declaration), Variable_Kind::_let,
        Variable_Declaration_Flags::initialized_with_equals);
    l.visit_variable_assignment(identifier_of(assignment));
    l.visit_exit_block_scope();
    l.visit_end_of_module();

    EXPECT_THAT(v.errors, IsEmpty());
  }
}
}
}

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
