#include <gtest/gtest.h>
#include "../src/core/time_utils.hpp"

using namespace predibloom::core;

// ============================================================
// parseDateString + formatDate
// ============================================================

TEST(ParseDateString, ValidDate) {
    std::tm tm;
    ASSERT_TRUE(parseDateString("2026-04-18", tm));
    EXPECT_EQ(tm.tm_year, 126);  // 2026 - 1900
    EXPECT_EQ(tm.tm_mon, 3);     // April = 3 (0-indexed)
    EXPECT_EQ(tm.tm_mday, 18);
}

TEST(ParseDateString, JanuaryFirst) {
    std::tm tm;
    ASSERT_TRUE(parseDateString("2026-01-01", tm));
    EXPECT_EQ(tm.tm_year, 126);
    EXPECT_EQ(tm.tm_mon, 0);
    EXPECT_EQ(tm.tm_mday, 1);
}

TEST(ParseDateString, December31) {
    std::tm tm;
    ASSERT_TRUE(parseDateString("2025-12-31", tm));
    EXPECT_EQ(tm.tm_year, 125);
    EXPECT_EQ(tm.tm_mon, 11);
    EXPECT_EQ(tm.tm_mday, 31);
}

TEST(ParseDateString, InvalidTooShort) {
    std::tm tm;
    EXPECT_FALSE(parseDateString("2026-04", tm));
}

TEST(ParseDateString, InvalidEmpty) {
    std::tm tm;
    EXPECT_FALSE(parseDateString("", tm));
}

TEST(ParseDateString, InvalidNoDashes) {
    std::tm tm;
    EXPECT_FALSE(parseDateString("2026/04/18", tm));
}

TEST(ParseDateString, InvalidLetters) {
    std::tm tm;
    EXPECT_FALSE(parseDateString("abcd-ef-gh", tm));
}

TEST(FormatDate, BasicRoundTrip) {
    std::tm tm;
    parseDateString("2026-04-18", tm);
    EXPECT_EQ(formatDate(tm), "2026-04-18");
}

TEST(FormatDate, SingleDigitMonthDay) {
    std::tm tm;
    parseDateString("2026-01-05", tm);
    EXPECT_EQ(formatDate(tm), "2026-01-05");
}

// ============================================================
// addDaysToDate
// ============================================================

TEST(AddDaysToDate, NoChange) {
    EXPECT_EQ(addDaysToDate("2026-04-18", 0), "2026-04-18");
}

TEST(AddDaysToDate, AddOneDay) {
    EXPECT_EQ(addDaysToDate("2026-04-18", 1), "2026-04-19");
}

TEST(AddDaysToDate, SubtractOneDay) {
    EXPECT_EQ(addDaysToDate("2026-04-18", -1), "2026-04-17");
}

TEST(AddDaysToDate, MonthBoundaryForward) {
    EXPECT_EQ(addDaysToDate("2026-04-30", 1), "2026-05-01");
}

TEST(AddDaysToDate, MonthBoundaryBackward) {
    EXPECT_EQ(addDaysToDate("2026-05-01", -1), "2026-04-30");
}

TEST(AddDaysToDate, YearBoundaryForward) {
    EXPECT_EQ(addDaysToDate("2026-12-31", 1), "2027-01-01");
}

TEST(AddDaysToDate, YearBoundaryBackward) {
    EXPECT_EQ(addDaysToDate("2027-01-01", -1), "2026-12-31");
}

TEST(AddDaysToDate, LeapYearFeb28To29) {
    EXPECT_EQ(addDaysToDate("2024-02-28", 1), "2024-02-29");
}

TEST(AddDaysToDate, LeapYearFeb29ToMar1) {
    EXPECT_EQ(addDaysToDate("2024-02-29", 1), "2024-03-01");
}

TEST(AddDaysToDate, NonLeapYearFeb28ToMar1) {
    EXPECT_EQ(addDaysToDate("2025-02-28", 1), "2025-03-01");
}

TEST(AddDaysToDate, BackwardAcrossYearStart) {
    EXPECT_EQ(addDaysToDate("2026-01-01", -1), "2025-12-31");
}

TEST(AddDaysToDate, LargeOffset) {
    EXPECT_EQ(addDaysToDate("2026-04-18", 30), "2026-05-18");
}

TEST(AddDaysToDate, LargeNegativeOffset) {
    EXPECT_EQ(addDaysToDate("2026-04-18", -30), "2026-03-19");
}

TEST(AddDaysToDate, InvalidInput) {
    EXPECT_EQ(addDaysToDate("invalid", 1), "");
}

TEST(AddDaysToDate, March31ToApril) {
    EXPECT_EQ(addDaysToDate("2026-03-31", 1), "2026-04-01");
}

TEST(AddDaysToDate, Jan31ToFeb) {
    EXPECT_EQ(addDaysToDate("2026-01-31", 1), "2026-02-01");
}

// ============================================================
// computeEntryDatetime
// ============================================================

TEST(ComputeEntryDatetime, SameDayHour5) {
    EXPECT_EQ(computeEntryDatetime("2026-04-18", 0, 5), "2026-04-18T05");
}

TEST(ComputeEntryDatetime, SameDayHour0) {
    EXPECT_EQ(computeEntryDatetime("2026-04-18", 0, 0), "2026-04-18T00");
}

TEST(ComputeEntryDatetime, SameDayHour23) {
    EXPECT_EQ(computeEntryDatetime("2026-04-18", 0, 23), "2026-04-18T23");
}

TEST(ComputeEntryDatetime, PrevDayOffset) {
    EXPECT_EQ(computeEntryDatetime("2026-04-18", -1, 22), "2026-04-17T22");
}

TEST(ComputeEntryDatetime, PrevDayCrossMonth) {
    EXPECT_EQ(computeEntryDatetime("2026-05-01", -1, 23), "2026-04-30T23");
}

TEST(ComputeEntryDatetime, PrevDayCrossYear) {
    EXPECT_EQ(computeEntryDatetime("2026-01-01", -1, 20), "2025-12-31T20");
}

TEST(ComputeEntryDatetime, NextDayOffset) {
    EXPECT_EQ(computeEntryDatetime("2026-04-18", 1, 3), "2026-04-19T03");
}

// ============================================================
// computeEntryDatetimeWithJitter
// ============================================================

TEST(ComputeEntryDatetimeWithJitter, ZeroJitter) {
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 5, 0), "2026-04-18T05");
}

TEST(ComputeEntryDatetimeWithJitter, PositiveJitterSameDay) {
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 5, 3), "2026-04-18T08");
}

TEST(ComputeEntryDatetimeWithJitter, NegativeJitterSameDay) {
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 5, -3), "2026-04-18T02");
}

TEST(ComputeEntryDatetimeWithJitter, NegativeJitterCrossDayBackward) {
    // hour 1 - 3 = hour 22 of previous day
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 1, -3), "2026-04-17T22");
}

TEST(ComputeEntryDatetimeWithJitter, PositiveJitterCrossDayForward) {
    // hour 22 + 3 = hour 1 of next day
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 22, 3), "2026-04-19T01");
}

TEST(ComputeEntryDatetimeWithJitter, NegativeJitterFromMidnight) {
    // hour 0 - 3 = hour 21 of previous day
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 0, -3), "2026-04-17T21");
}

TEST(ComputeEntryDatetimeWithJitter, PositiveJitterFromHour23) {
    // hour 23 + 3 = hour 2 of next day
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 23, 3), "2026-04-19T02");
}

TEST(ComputeEntryDatetimeWithJitter, CombinedDayOffsetAndJitter) {
    // day_offset=-1, hour 1, jitter -3 -> hour 22, two days before settlement
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", -1, 1, -3), "2026-04-16T22");
}

TEST(ComputeEntryDatetimeWithJitter, JitterCrossMonthBoundary) {
    // May 1 settlement, day_offset=0, hour 1, jitter -3 -> April 30 at 22
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-05-01", 0, 1, -3), "2026-04-30T22");
}

TEST(ComputeEntryDatetimeWithJitter, JitterCrossYearBoundary) {
    // Jan 1 settlement, hour 1, jitter -3 -> Dec 31 prev year at 22
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-01-01", 0, 1, -3), "2025-12-31T22");
}

TEST(ComputeEntryDatetimeWithJitter, LargePositiveJitter) {
    // hour 20 + 5 = hour 1 next day
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 20, 5), "2026-04-19T01");
}

TEST(ComputeEntryDatetimeWithJitter, LargeNegativeJitter) {
    // hour 3 - 5 = hour 22 prev day
    EXPECT_EQ(computeEntryDatetimeWithJitter("2026-04-18", 0, 3, -5), "2026-04-17T22");
}

// ============================================================
// parseHourDatetime
// ============================================================

TEST(ParseHourDatetime, ValidDatetime) {
    time_t t = parseHourDatetime("2026-04-18T05");
    EXPECT_NE(t, -1);
    std::tm result = {};
    gmtime_r(&t, &result);
    EXPECT_EQ(result.tm_year, 126);
    EXPECT_EQ(result.tm_mon, 3);
    EXPECT_EQ(result.tm_mday, 18);
    EXPECT_EQ(result.tm_hour, 5);
}

TEST(ParseHourDatetime, Midnight) {
    time_t t = parseHourDatetime("2026-04-18T00");
    EXPECT_NE(t, -1);
    std::tm result = {};
    gmtime_r(&t, &result);
    EXPECT_EQ(result.tm_hour, 0);
}

TEST(ParseHourDatetime, Hour23) {
    time_t t = parseHourDatetime("2026-04-18T23");
    EXPECT_NE(t, -1);
    std::tm result = {};
    gmtime_r(&t, &result);
    EXPECT_EQ(result.tm_hour, 23);
}

TEST(ParseHourDatetime, InvalidTooShort) {
    EXPECT_EQ(parseHourDatetime("2026-04-18"), -1);
}

TEST(ParseHourDatetime, InvalidNoT) {
    EXPECT_EQ(parseHourDatetime("2026-04-18 05"), -1);
}

TEST(ParseHourDatetime, InvalidEmpty) {
    EXPECT_EQ(parseHourDatetime(""), -1);
}

// ============================================================
// isWithinEntryWindow
// ============================================================

TEST(IsWithinEntryWindow, ExactlyAtEntry) {
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T05"));
}

TEST(IsWithinEntryWindow, OneHourBefore) {
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T04"));
}

TEST(IsWithinEntryWindow, TwoHoursBefore) {
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T03"));
}

TEST(IsWithinEntryWindow, ThreeHoursBefore) {
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T02"));
}

TEST(IsWithinEntryWindow, FourHoursBefore) {
    EXPECT_FALSE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T01"));
}

TEST(IsWithinEntryWindow, ThreeHoursAfter) {
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T08"));
}

TEST(IsWithinEntryWindow, FourHoursAfter) {
    EXPECT_FALSE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-18T09"));
}

TEST(IsWithinEntryWindow, CrossMidnightBefore) {
    // Entry at hour 1, 3 hours before = hour 22 previous day
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T01", 3, "2026-04-17T22"));
}

TEST(IsWithinEntryWindow, CrossMidnightAfter) {
    // Entry at hour 22, 3 hours after = hour 1 next day
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T22", 3, "2026-04-19T01"));
}

TEST(IsWithinEntryWindow, CrossMonthBoundary) {
    // Entry at May 1 hour 1, 3 hours before = April 30 hour 22
    EXPECT_TRUE(isWithinEntryWindow("2026-05-01T01", 3, "2026-04-30T22"));
}

TEST(IsWithinEntryWindow, FarAway) {
    EXPECT_FALSE(isWithinEntryWindow("2026-04-18T05", 3, "2026-04-20T05"));
}

TEST(IsWithinEntryWindow, WindowZero) {
    EXPECT_TRUE(isWithinEntryWindow("2026-04-18T05", 0, "2026-04-18T05"));
    EXPECT_FALSE(isWithinEntryWindow("2026-04-18T05", 0, "2026-04-18T06"));
}

TEST(IsWithinEntryWindow, InvalidEntry) {
    EXPECT_FALSE(isWithinEntryWindow("bad", 3, "2026-04-18T05"));
}

TEST(IsWithinEntryWindow, InvalidCurrent) {
    EXPECT_FALSE(isWithinEntryWindow("2026-04-18T05", 3, "bad"));
}

// ============================================================
// computeAsOfIso
// ============================================================

TEST(ComputeAsOfIso, SameDayHour5) {
    EXPECT_EQ(computeAsOfIso("2026-04-18", 0, 5), "2026-04-18T05:00:00Z");
}

TEST(ComputeAsOfIso, PrevDayHour22) {
    EXPECT_EQ(computeAsOfIso("2026-04-18", -1, 22), "2026-04-17T22:00:00Z");
}

TEST(ComputeAsOfIso, PrevDayCrossYear) {
    EXPECT_EQ(computeAsOfIso("2026-01-01", -1, 20), "2025-12-31T20:00:00Z");
}

TEST(ComputeAsOfIso, InvalidInput) {
    EXPECT_EQ(computeAsOfIso("bogus", 0, 5), "");
}

// ============================================================
// nyMidnightToUtcIso (DST handling)
// ============================================================

TEST(NyMidnightToUtcIso, WinterIsEst) {
    // January: EST (UTC-5) — midnight local = 05:00Z
    EXPECT_EQ(nyMidnightToUtcIso("2025-01-15"), "2025-01-15T05:00:00Z");
}

TEST(NyMidnightToUtcIso, SummerIsEdt) {
    // July: EDT (UTC-4) — midnight local = 04:00Z
    EXPECT_EQ(nyMidnightToUtcIso("2025-07-15"), "2025-07-15T04:00:00Z");
}

TEST(NyMidnightToUtcIso, DayOfSpringForward) {
    // March 9, 2025 is the 2nd Sunday of March; day begins at midnight EST (05:00Z),
    // and springs forward at 02:00 local to 03:00 local.
    EXPECT_EQ(nyMidnightToUtcIso("2025-03-09"), "2025-03-09T05:00:00Z");
}

TEST(NyMidnightToUtcIso, DayAfterSpringForward) {
    // March 10, 2025 is already EDT — midnight local = 04:00Z
    EXPECT_EQ(nyMidnightToUtcIso("2025-03-10"), "2025-03-10T04:00:00Z");
}

TEST(NyMidnightToUtcIso, DayOfFallBack) {
    // November 2, 2025 is the 1st Sunday of November; day begins at midnight EDT (04:00Z),
    // and falls back at 02:00 local to 01:00 local.
    EXPECT_EQ(nyMidnightToUtcIso("2025-11-02"), "2025-11-02T04:00:00Z");
}

TEST(NyMidnightToUtcIso, DayAfterFallBack) {
    // November 3, 2025 is EST — midnight local = 05:00Z
    EXPECT_EQ(nyMidnightToUtcIso("2025-11-03"), "2025-11-03T05:00:00Z");
}

TEST(NyMidnightToUtcIso, InvalidDate) {
    EXPECT_EQ(nyMidnightToUtcIso("bad"), "");
}

// ============================================================
// currentUtcDatetimeHour (smoke test)
// ============================================================

TEST(CurrentUtcDatetimeHour, Format) {
    std::string now = currentUtcDatetimeHour();
    // Should be "YYYY-MM-DDTHH" = 13 chars
    EXPECT_EQ(now.size(), 13u);
    EXPECT_EQ(now[4], '-');
    EXPECT_EQ(now[7], '-');
    EXPECT_EQ(now[10], 'T');
}
