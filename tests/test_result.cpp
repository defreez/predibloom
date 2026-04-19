#include <gtest/gtest.h>
#include "api/result.hpp"

using namespace predibloom::api;

TEST(ResultTest, ConstructWithValue_IsOk) {
    Result<int> r(42);
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(r.is_error());
}

TEST(ResultTest, ConstructWithValue_ValueAccessible) {
    Result<int> r(42);
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ConstructWithString) {
    Result<std::string> r(std::string("hello"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), "hello");
}

TEST(ResultTest, ArrowOperator) {
    Result<std::string> r(std::string("hello"));
    EXPECT_EQ(r->size(), 5u);
}

TEST(ResultTest, ConstructWithError_IsError) {
    Result<int> r(Error(ApiError::NetworkError, "connection failed"));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.is_error());
}

TEST(ResultTest, ErrorFields) {
    Result<int> r(Error(ApiError::HttpError, "not found", 404));
    EXPECT_EQ(r.error().type, ApiError::HttpError);
    EXPECT_EQ(r.error().message, "not found");
    EXPECT_EQ(r.error().http_status, 404);
}

TEST(ResultTest, ErrorDefaultHttpStatus) {
    Result<int> r(Error(ApiError::ParseError, "bad json"));
    EXPECT_EQ(r.error().http_status, 0);
}

TEST(ResultTest, AllErrorTypes) {
    Result<int> r1(Error(ApiError::NetworkError, "net"));
    EXPECT_EQ(r1.error().type, ApiError::NetworkError);

    Result<int> r2(Error(ApiError::HttpError, "http"));
    EXPECT_EQ(r2.error().type, ApiError::HttpError);

    Result<int> r3(Error(ApiError::ParseError, "parse"));
    EXPECT_EQ(r3.error().type, ApiError::ParseError);

    Result<int> r4(Error(ApiError::RateLimitError, "rate"));
    EXPECT_EQ(r4.error().type, ApiError::RateLimitError);
}

TEST(ResultTest, MutableValueAccess) {
    Result<std::string> r(std::string("hello"));
    r.value() = "world";
    EXPECT_EQ(r.value(), "world");
}

TEST(ResultTest, ResultWithVector) {
    std::vector<int> v = {1, 2, 3};
    Result<std::vector<int>> r(v);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[1], 2);
}
