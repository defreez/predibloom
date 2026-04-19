#include <gtest/gtest.h>
#include "cli/formatters.hpp"

using namespace predibloom::cli;

// --- formatPrice ---

TEST(FormatPriceTest, RegularValue) {
    EXPECT_EQ(formatPrice(52.0), "52c");
}

TEST(FormatPriceTest, Zero) {
    EXPECT_EQ(formatPrice(0.0), "0c");
}

TEST(FormatPriceTest, Hundred) {
    EXPECT_EQ(formatPrice(100.0), "100c");
}

TEST(FormatPriceTest, FractionalRounds) {
    // std::fixed with precision 0 rounds
    EXPECT_EQ(formatPrice(52.7), "53c");
    EXPECT_EQ(formatPrice(52.3), "52c");
}

// --- truncate ---

TEST(TruncateTest, ShortString_Unchanged) {
    EXPECT_EQ(predibloom::cli::truncate("hello", 10), "hello");
}

TEST(TruncateTest, ExactLength_Unchanged) {
    EXPECT_EQ(predibloom::cli::truncate("hello", 5), "hello");
}

TEST(TruncateTest, LongString_TruncatesWithEllipsis) {
    EXPECT_EQ(predibloom::cli::truncate("hello world", 8), "hello...");
}

TEST(TruncateTest, MaxLen3_NoEllipsis) {
    EXPECT_EQ(predibloom::cli::truncate("hello", 3), "hel");
}

TEST(TruncateTest, MaxLen2_NoEllipsis) {
    EXPECT_EQ(predibloom::cli::truncate("hello", 2), "he");
}

TEST(TruncateTest, EmptyString) {
    EXPECT_EQ(predibloom::cli::truncate("", 5), "");
}

TEST(TruncateTest, MaxLen4_OneCharPlusEllipsis) {
    EXPECT_EQ(predibloom::cli::truncate("hello", 4), "h...");
}

// --- parseFormat ---

TEST(ParseFormatTest, Json) {
    EXPECT_EQ(parseFormat("json"), OutputFormat::Json);
}

TEST(ParseFormatTest, Csv) {
    EXPECT_EQ(parseFormat("csv"), OutputFormat::Csv);
}

TEST(ParseFormatTest, Table) {
    EXPECT_EQ(parseFormat("table"), OutputFormat::Table);
}

TEST(ParseFormatTest, InvalidDefaultsToTable) {
    EXPECT_EQ(parseFormat("xml"), OutputFormat::Table);
    EXPECT_EQ(parseFormat(""), OutputFormat::Table);
}

// --- formatTemp ---

TEST(FormatTempTest, ValidTemp) {
    EXPECT_EQ(formatTemp(85.5), "85.5F");
}

TEST(FormatTempTest, WholeNumber) {
    EXPECT_EQ(formatTemp(90.0), "90.0F");
}

TEST(FormatTempTest, Nullopt) {
    EXPECT_EQ(formatTemp(std::nullopt), "-");
}

TEST(FormatTempTest, NegativeTemp) {
    EXPECT_EQ(formatTemp(-5.0), "-5.0F");
}

// --- formatStrikeRange ---

TEST(FormatStrikeRangeTest, FloorAndCap) {
    EXPECT_EQ(formatStrikeRange(80, 90), "80-90");
}

TEST(FormatStrikeRangeTest, FloorOnly) {
    EXPECT_EQ(formatStrikeRange(90, std::nullopt), "90+");
}

TEST(FormatStrikeRangeTest, CapOnly) {
    EXPECT_EQ(formatStrikeRange(std::nullopt, 90), "<90");
}

TEST(FormatStrikeRangeTest, Neither) {
    EXPECT_EQ(formatStrikeRange(std::nullopt, std::nullopt), "-");
}
