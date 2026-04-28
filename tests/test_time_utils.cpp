#include <gtest/gtest.h>
#include "../src/core/datetime.hpp"

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

// ============================================================
// localDayUtcWindow (IANA timezone, DST-aware)
// ============================================================

TEST(LocalDayUtcWindow, NyWinter) {
    // NY in January is EST (UTC-5): local midnight = 05:00Z, 24h window
    auto w = localDayUtcWindow("2025-01-15", "America/New_York");
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->start.toIsoString(), "2025-01-15T05:00:00Z");
    EXPECT_EQ(w->end.toIsoString(), "2025-01-16T05:00:00Z");
}

TEST(LocalDayUtcWindow, NySummer) {
    // NY in July is EDT (UTC-4): local midnight = 04:00Z
    auto w = localDayUtcWindow("2025-07-15", "America/New_York");
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->start.toIsoString(), "2025-07-15T04:00:00Z");
    EXPECT_EQ(w->end.toIsoString(), "2025-07-16T04:00:00Z");
}

TEST(LocalDayUtcWindow, NySpringForward) {
    // March 9, 2025: spring-forward day, only 23 hours long
    auto w = localDayUtcWindow("2025-03-09", "America/New_York");
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->start.toIsoString(), "2025-03-09T05:00:00Z");
    EXPECT_EQ(w->end.toIsoString(), "2025-03-10T04:00:00Z");
    EXPECT_EQ(w->end.epoch() - w->start.epoch(), 23 * 3600);
}

TEST(LocalDayUtcWindow, NyFallBack) {
    // November 2, 2025: fall-back day, 25 hours long
    auto w = localDayUtcWindow("2025-11-02", "America/New_York");
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->start.toIsoString(), "2025-11-02T04:00:00Z");
    EXPECT_EQ(w->end.toIsoString(), "2025-11-03T05:00:00Z");
    EXPECT_EQ(w->end.epoch() - w->start.epoch(), 25 * 3600);
}

TEST(LocalDayUtcWindow, PhoenixNoDst) {
    // Phoenix doesn't observe DST: UTC-7 year-round
    auto winter = localDayUtcWindow("2025-01-15", "America/Phoenix");
    auto summer = localDayUtcWindow("2025-07-15", "America/Phoenix");
    ASSERT_TRUE(winter.has_value());
    ASSERT_TRUE(summer.has_value());
    EXPECT_EQ(winter->start.toIsoString(), "2025-01-15T07:00:00Z");
    EXPECT_EQ(summer->start.toIsoString(), "2025-07-15T07:00:00Z");
}

TEST(LocalDayUtcWindow, LosAngelesSummer) {
    // LA in July is PDT (UTC-7)
    auto w = localDayUtcWindow("2025-07-15", "America/Los_Angeles");
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->start.toIsoString(), "2025-07-15T07:00:00Z");
}

TEST(LocalDayUtcWindow, InvalidTimezone) {
    auto w = localDayUtcWindow("2025-07-15", "Mars/Olympus");
    EXPECT_FALSE(w.has_value());
}

TEST(LocalDayUtcWindow, InvalidDate) {
    auto w = localDayUtcWindow("not-a-date", "America/New_York");
    EXPECT_FALSE(w.has_value());
}

// ============================================================
// formatLocalHourMinute
// ============================================================

TEST(FormatLocalHourMinute, NySummer) {
    // 18:00Z in July → 14:00 local in EDT
    auto utc = DateTime::parseIso("2025-07-15T18:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalHourMinute(*utc, "America/New_York"), "14:00");
}

TEST(FormatLocalHourMinute, NyWinter) {
    // 18:00Z in January → 13:00 local in EST
    auto utc = DateTime::parseIso("2025-01-15T18:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalHourMinute(*utc, "America/New_York"), "13:00");
}

TEST(FormatLocalHourMinute, LaSummer) {
    // 22:00Z in July → 15:00 local in PDT
    auto utc = DateTime::parseIso("2025-07-15T22:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalHourMinute(*utc, "America/Los_Angeles"), "15:00");
}

TEST(FormatLocalHourMinute, InvalidTimezone) {
    auto utc = DateTime::parseIso("2025-07-15T18:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalHourMinute(*utc, "Mars/Olympus"), "");
}

// ============================================================
// parseLocalDatetime
// ============================================================

TEST(ParseLocalDatetime, NySummerNoon) {
    // 12:00 EDT on 2025-07-15 = 16:00Z
    auto dt = parseLocalDatetime("2025-07-15", "12:00", "America/New_York");
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(dt->toIsoString(), "2025-07-15T16:00:00Z");
}

TEST(ParseLocalDatetime, NyWinterNoon) {
    // 12:00 EST on 2025-01-15 = 17:00Z
    auto dt = parseLocalDatetime("2025-01-15", "12:00", "America/New_York");
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(dt->toIsoString(), "2025-01-15T17:00:00Z");
}

TEST(ParseLocalDatetime, PhoenixNoon) {
    // 12:00 MST (no DST) on 2025-07-15 = 19:00Z
    auto dt = parseLocalDatetime("2025-07-15", "12:00", "America/Phoenix");
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(dt->toIsoString(), "2025-07-15T19:00:00Z");
}

TEST(ParseLocalDatetime, RoundTripWithFormatLocalHourMinute) {
    auto dt = parseLocalDatetime("2025-07-15", "14:30", "America/New_York");
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(formatLocalHourMinute(*dt, "America/New_York"), "14:30");
}

TEST(ParseLocalDatetime, InvalidTimezone) {
    EXPECT_FALSE(parseLocalDatetime("2025-07-15", "12:00", "Mars/Olympus").has_value());
}

TEST(ParseLocalDatetime, InvalidHhmm) {
    EXPECT_FALSE(parseLocalDatetime("2025-07-15", "garbage", "America/New_York").has_value());
}

// ============================================================
// formatLocalAmPm
// ============================================================

TEST(FormatLocalAmPm, NyEdt) {
    auto utc = DateTime::parseIso("2025-07-15T20:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalAmPm(*utc, "America/New_York"), "4pm EDT");
}

TEST(FormatLocalAmPm, NyEst) {
    auto utc = DateTime::parseIso("2025-01-15T17:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalAmPm(*utc, "America/New_York"), "12pm EST");
}

TEST(FormatLocalAmPm, LaPdt) {
    auto utc = DateTime::parseIso("2025-07-15T22:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalAmPm(*utc, "America/Los_Angeles"), "3pm PDT");
}

TEST(FormatLocalAmPm, PhoenixMst) {
    auto utc = DateTime::parseIso("2025-07-15T19:00:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalAmPm(*utc, "America/Phoenix"), "12pm MST");
}

TEST(FormatLocalAmPm, IncludesMinutesWhenNonZero) {
    auto utc = DateTime::parseIso("2025-07-15T20:30:00Z");
    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(formatLocalAmPm(*utc, "America/New_York"), "4:30pm EDT");
}

TEST(FormatLocalAmPm, MidnightAndNoon) {
    auto midnight = DateTime::parseIso("2025-07-15T04:00:00Z");
    ASSERT_TRUE(midnight.has_value());
    EXPECT_EQ(formatLocalAmPm(*midnight, "America/New_York"), "12am EDT");

    auto noon = DateTime::parseIso("2025-07-15T16:00:00Z");
    ASSERT_TRUE(noon.has_value());
    EXPECT_EQ(formatLocalAmPm(*noon, "America/New_York"), "12pm EDT");
}

// ============================================================
// formatUtcAsPtWithAge
// ============================================================

TEST(FormatUtcAsPtWithAge, ValidTimestamp) {
    // 2026-04-23T18:00:00Z = 11am PT (PDT, UTC-7)
    std::string result = formatUtcAsPtWithAge("2026-04-23T18:00:00Z");
    // Should contain "Apr 23" and "11:00am PT"
    EXPECT_NE(result.find("Apr 23"), std::string::npos);
    EXPECT_NE(result.find("11:00am PT"), std::string::npos);
    // Should contain "hours ago" or "hour ago"
    EXPECT_NE(result.find("ago"), std::string::npos);
}

TEST(FormatUtcAsPtWithAge, InvalidTimestamp) {
    EXPECT_EQ(formatUtcAsPtWithAge("bad"), "");
    EXPECT_EQ(formatUtcAsPtWithAge(""), "");
    EXPECT_EQ(formatUtcAsPtWithAge("2026-04-23"), "");  // too short
}
