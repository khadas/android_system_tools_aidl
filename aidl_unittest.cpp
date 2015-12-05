/*
 * Copyright (C) 2015, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "aidl.h"
#include "aidl_language.h"
#include "tests/fake_io_delegate.h"
#include "type_cpp.h"
#include "type_java.h"
#include "type_namespace.h"

using android::aidl::test::FakeIoDelegate;
using std::string;
using std::unique_ptr;
using android::aidl::internals::parse_preprocessed_file;

namespace android {
namespace aidl {

class AidlTest : public ::testing::Test {
 protected:
  void SetUp() override {
    java_types_.Init();
    cpp_types_.Init();
  }

  unique_ptr<AidlInterface> Parse(const string& path,
                                  const string& contents,
                                  TypeNamespace* types) {
    io_delegate_.SetFileContents(path, contents);
    unique_ptr<AidlInterface> ret;
    std::vector<std::unique_ptr<AidlImport>> imports;
    ::android::aidl::internals::load_and_validate_aidl(
        preprocessed_files_,
        import_paths_,
        path,
        io_delegate_,
        types,
        &ret,
        &imports);
    return ret;
  }

  FakeIoDelegate io_delegate_;
  vector<string> preprocessed_files_;
  vector<string> import_paths_;
  java::JavaTypeNamespace java_types_;
  cpp::TypeNamespace cpp_types_;
};

TEST_F(AidlTest, JavaAcceptsMissingPackage) {
  EXPECT_NE(nullptr, Parse("IFoo.aidl", "interface IFoo { }", &java_types_));
}

TEST_F(AidlTest, RejectsArraysOfBinders) {
  import_paths_.push_back("");
  io_delegate_.SetFileContents("bar/IBar.aidl",
                               "package bar; interface IBar {}");
  string path = "foo/IFoo.aidl";
  string contents = "package foo;\n"
                    "import bar.IBar;\n"
                    "interface IFoo { void f(in IBar[] input); }";
  EXPECT_EQ(nullptr, Parse(path, contents, &java_types_));
  EXPECT_EQ(nullptr, Parse(path, contents, &cpp_types_));
}

TEST_F(AidlTest, CppRejectsMissingPackage) {
  EXPECT_EQ(nullptr, Parse("IFoo.aidl", "interface IFoo { }", &cpp_types_));
  EXPECT_NE(nullptr,
            Parse("a/IFoo.aidl", "package a; interface IFoo { }", &cpp_types_));
}

TEST_F(AidlTest, RejectsOnewayOutParameters) {
  string oneway_interface =
      "package a; oneway interface IFoo { void f(out int bar); }";
  string oneway_method =
      "package a; interface IBar { oneway void f(out int bar); }";
  EXPECT_EQ(nullptr, Parse("a/IFoo.aidl", oneway_interface, &cpp_types_));
  EXPECT_EQ(nullptr, Parse("a/IFoo.aidl", oneway_interface, &java_types_));
  EXPECT_EQ(nullptr, Parse("a/IBar.aidl", oneway_method, &cpp_types_));
  EXPECT_EQ(nullptr, Parse("a/IBar.aidl", oneway_method, &java_types_));
}

TEST_F(AidlTest, RejectsOnewayNonVoidReturn) {
  string oneway_method = "package a; interface IFoo { oneway int f(); }";
  EXPECT_EQ(nullptr, Parse("a/IFoo.aidl", oneway_method, &cpp_types_));
  EXPECT_EQ(nullptr, Parse("a/IFoo.aidl", oneway_method, &java_types_));
}

TEST_F(AidlTest, AcceptsOneway) {
  string oneway_method = "package a; interface IFoo { oneway void f(int a); }";
  string oneway_interface =
      "package a; oneway interface IBar { void f(int a); }";
  EXPECT_NE(nullptr, Parse("a/IFoo.aidl", oneway_method, &cpp_types_));
  EXPECT_NE(nullptr, Parse("a/IFoo.aidl", oneway_method, &java_types_));
  EXPECT_NE(nullptr, Parse("a/IBar.aidl", oneway_interface, &cpp_types_));
  EXPECT_NE(nullptr, Parse("a/IBar.aidl", oneway_interface, &java_types_));
}

TEST_F(AidlTest, ParsesPreprocessedFile) {
  string simple_content = "parcelable a.Foo;\ninterface b.IBar;";
  io_delegate_.SetFileContents("path", simple_content);
  EXPECT_FALSE(java_types_.HasType("a.Foo"));
  EXPECT_TRUE(parse_preprocessed_file(io_delegate_, "path", &java_types_));
  EXPECT_TRUE(java_types_.HasType("Foo"));
  EXPECT_TRUE(java_types_.HasType("a.Foo"));
  EXPECT_TRUE(java_types_.HasType("b.IBar"));
}

TEST_F(AidlTest, ParsesPreprocessedFileWithWhitespace) {
  string simple_content = "parcelable    a.Foo;\n  interface b.IBar  ;\t";
  io_delegate_.SetFileContents("path", simple_content);
  EXPECT_FALSE(java_types_.HasType("a.Foo"));
  EXPECT_TRUE(parse_preprocessed_file(io_delegate_, "path", &java_types_));
  EXPECT_TRUE(java_types_.HasType("Foo"));
  EXPECT_TRUE(java_types_.HasType("a.Foo"));
  EXPECT_TRUE(java_types_.HasType("b.IBar"));
}

TEST_F(AidlTest, PreferImportToPreprocessed) {
  io_delegate_.SetFileContents("preprocessed", "interface another.IBar;");
  io_delegate_.SetFileContents("one/IBar.aidl", "package one; "
                                                "interface IBar {}");
  preprocessed_files_.push_back("preprocessed");
  import_paths_.push_back("");
  auto parse_result = Parse(
      "p/IFoo.aidl", "package p; import one.IBar; interface IFoo {}",
      &java_types_);
  EXPECT_NE(nullptr, parse_result);
  // We expect to know about both kinds of IBar
  EXPECT_TRUE(java_types_.HasType("one.IBar"));
  EXPECT_TRUE(java_types_.HasType("another.IBar"));
  // But if we request just "IBar" we should get our imported one.
  const java::Type* type = java_types_.Find("IBar");
  ASSERT_TRUE(type);
  EXPECT_EQ("one.IBar", type->QualifiedName());
}

TEST_F(AidlTest, WritePreprocessedFile) {
  io_delegate_.SetFileContents("p/Outer.aidl",
                               "package p; parcelable Outer.Inner;");
  io_delegate_.SetFileContents("one/IBar.aidl", "package one; import p.Outer;"
                                                "interface IBar {}");

  JavaOptions options;
  options.output_file_name_ = "preprocessed";
  options.files_to_preprocess_.resize(2);
  options.files_to_preprocess_[0] = "p/Outer.aidl";
  options.files_to_preprocess_[1] = "one/IBar.aidl";
  EXPECT_TRUE(::android::aidl::preprocess_aidl(options, io_delegate_));

  string output;
  EXPECT_TRUE(io_delegate_.GetWrittenContents("preprocessed", &output));
  EXPECT_EQ("parcelable p.Outer.Inner;\ninterface one.IBar;\n", output);
}

TEST_F(AidlTest, RequireOuterClass) {
  io_delegate_.SetFileContents("p/Outer.aidl",
                               "package p; parcelable Outer.Inner;");
  import_paths_.push_back("");
  auto parse_result = Parse(
      "p/IFoo.aidl",
      "package p; import p.Outer; interface IFoo { void f(in Inner c); }",
      &java_types_);
  EXPECT_EQ(nullptr, parse_result);
}

TEST_F(AidlTest, ParseCompoundParcelableFromPreprocess) {
  io_delegate_.SetFileContents("preprocessed",
                               "parcelable p.Outer.Inner;");
  preprocessed_files_.push_back("preprocessed");
  auto parse_result = Parse(
      "p/IFoo.aidl",
      "package p; interface IFoo { void f(in Inner c); }",
      &java_types_);
  // TODO(wiley): This should actually return nullptr because we require
  //              the outer class name.  However, for legacy reasons,
  //              this behavior must be maintained.  b/17415692
  EXPECT_NE(nullptr, parse_result);
}

TEST_F(AidlTest, FailOnParcelable) {
  JavaOptions options;
  options.input_file_name_ = "p/IFoo.aidl";
  io_delegate_.SetFileContents(options.input_file_name_,
                               "package p; parcelable IFoo;");
  // By default, we shouldn't fail on parcelable.
  EXPECT_EQ(0, ::android::aidl::compile_aidl_to_java(options, io_delegate_));
  options.fail_on_parcelable_ = true;
  EXPECT_NE(0, ::android::aidl::compile_aidl_to_java(options, io_delegate_));
}

TEST_F(AidlTest, UnderstandsNativeParcelables) {
  io_delegate_.SetFileContents(
      "p/Bar.aidl",
      "package p; parcelable Bar from \"baz/header\";");
  import_paths_.push_back("");
  const string input_path = "p/IFoo.aidl";
  const string input = "package p; import p.Bar; interface IFoo { }";

  // C++ understands C++ specific stuff
  auto cpp_parse_result = Parse(input_path, input, &cpp_types_);
  EXPECT_NE(nullptr, cpp_parse_result);
  auto cpp_type = cpp_types_.Find("Bar");
  ASSERT_NE(nullptr, cpp_type);
  EXPECT_EQ("::p::Bar", cpp_type->CppType());
  set<string> headers;
  cpp_type->GetHeaders(&headers);
  EXPECT_EQ(1u, headers.size());
  EXPECT_EQ(1u, headers.count("baz/header"));

  // Java ignores C++ specific stuff
  auto java_parse_result = Parse(input_path, input, &java_types_);
  EXPECT_NE(nullptr, java_parse_result);
  auto java_type = java_types_.Find("Bar");
  ASSERT_NE(nullptr, java_type);
  EXPECT_EQ("p.Bar", java_type->InstantiableName());
}

}  // namespace aidl
}  // namespace android
