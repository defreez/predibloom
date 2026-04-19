#include <gtest/gtest.h>
#include "core/weather_comparison.hpp"

using namespace predibloom::core;

// --- parseDateFromEventTicker ---

TEST(ParseDateTest, ValidHighTempTicker) {
    EXPECT_EQ(parseDateFromEventTicker("KXHIGHNY-26APR18"), "2026-04-18");
}

TEST(ParseDateTest, ValidLowTempTicker) {
    EXPECT_EQ(parseDateFromEventTicker("KXLOWTCHI-26MAR15"), "2026-03-15");
}

TEST(ParseDateTest, AllMonths) {
    EXPECT_EQ(parseDateFromEventTicker("X-26JAN01"), "2026-01-01");
    EXPECT_EQ(parseDateFromEventTicker("X-26FEB14"), "2026-02-14");
    EXPECT_EQ(parseDateFromEventTicker("X-26MAR15"), "2026-03-15");
    EXPECT_EQ(parseDateFromEventTicker("X-26APR18"), "2026-04-18");
    EXPECT_EQ(parseDateFromEventTicker("X-26MAY01"), "2026-05-01");
    EXPECT_EQ(parseDateFromEventTicker("X-26JUN30"), "2026-06-30");
    EXPECT_EQ(parseDateFromEventTicker("X-26JUL04"), "2026-07-04");
    EXPECT_EQ(parseDateFromEventTicker("X-26AUG15"), "2026-08-15");
    EXPECT_EQ(parseDateFromEventTicker("X-26SEP01"), "2026-09-01");
    EXPECT_EQ(parseDateFromEventTicker("X-26OCT31"), "2026-10-31");
    EXPECT_EQ(parseDateFromEventTicker("X-26NOV25"), "2026-11-25");
    EXPECT_EQ(parseDateFromEventTicker("X-26DEC31"), "2026-12-31");
}

TEST(ParseDateTest, NoDash_ReturnsEmpty) {
    EXPECT_EQ(parseDateFromEventTicker("KXHIGHNY26APR18"), "");
}

TEST(ParseDateTest, TooShortAfterDash_ReturnsEmpty) {
    EXPECT_EQ(parseDateFromEventTicker("X-26AP"), "");
}

TEST(ParseDateTest, InvalidMonth_ReturnsEmpty) {
    EXPECT_EQ(parseDateFromEventTicker("X-26XXX18"), "");
}

TEST(ParseDateTest, EmptyString_ReturnsEmpty) {
    EXPECT_EQ(parseDateFromEventTicker(""), "");
}

TEST(ParseDateTest, MultipleDashes_UsesLast) {
    // rfind('-') finds the last dash
    EXPECT_EQ(parseDateFromEventTicker("KXHIGHNY-26APR18-T85"), "");
    // "T85" is only 3 chars after the last dash, so < 7 → empty
}

// --- wouldSettleYes ---

TEST(SettlementTest, BetweenFloorAndCap) {
    EXPECT_TRUE(wouldSettleYes(85.0, 80, 90));
}

TEST(SettlementTest, BelowFloor) {
    EXPECT_FALSE(wouldSettleYes(75.0, 80, 90));
}

TEST(SettlementTest, AboveCap) {
    EXPECT_FALSE(wouldSettleYes(95.0, 80, 90));
}

TEST(SettlementTest, ExactFloor_Inclusive) {
    EXPECT_TRUE(wouldSettleYes(80.0, 80, 90));
}

TEST(SettlementTest, ExactCap_Inclusive) {
    EXPECT_TRUE(wouldSettleYes(90.0, 80, 90));
}

TEST(SettlementTest, FloorOnly_AboveFloor) {
    EXPECT_TRUE(wouldSettleYes(95.0, 90, std::nullopt));
}

TEST(SettlementTest, FloorOnly_BelowFloor) {
    EXPECT_FALSE(wouldSettleYes(85.0, 90, std::nullopt));
}

TEST(SettlementTest, FloorOnly_ExactFloor) {
    EXPECT_TRUE(wouldSettleYes(90.0, 90, std::nullopt));
}

TEST(SettlementTest, CapOnly_BelowCap) {
    EXPECT_TRUE(wouldSettleYes(85.0, std::nullopt, 90));
}

TEST(SettlementTest, CapOnly_AboveCap) {
    EXPECT_FALSE(wouldSettleYes(95.0, std::nullopt, 90));
}

TEST(SettlementTest, CapOnly_ExactCap) {
    EXPECT_TRUE(wouldSettleYes(90.0, std::nullopt, 90));
}

TEST(SettlementTest, NoStrikes_ReturnsFalse) {
    EXPECT_FALSE(wouldSettleYes(85.0, std::nullopt, std::nullopt));
}

TEST(SettlementTest, FractionalTemperature) {
    EXPECT_TRUE(wouldSettleYes(89.5, 89, 90));
    EXPECT_FALSE(wouldSettleYes(88.9, 89, 90));
}

TEST(SettlementTest, NegativeTemperature) {
    EXPECT_TRUE(wouldSettleYes(-5.0, -10, 0));
    EXPECT_FALSE(wouldSettleYes(-15.0, -10, 0));
}
