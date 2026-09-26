#include "engine/access_decision_engine.h"

#include <gtest/gtest.h>

#include <ostream>

#include "unit/test_time.h"

namespace acs::engine {

// Lets GoogleTest print Decisions readably when an assertion fails.
void PrintTo(const Decision& decision, std::ostream* os) {
    if (decision.isGranted()) {
        *os << "grant";
    } else {
        *os << "deny(" << core::toString(*decision.denyReason()) << ")";
    }
}

namespace {

using namespace std::chrono_literals;
using core::DenyReason;
using std::chrono::Monday;
using std::chrono::Saturday;
using test::at;

// Baseline world: cardholder "CH-1" has card 12345 (facility 42) and is in the
// Staff group. Staff can enter the Lobby during business hours (Mon-Fri 07-18).
// Reader R-101 guards the Lobby and R-201 guards the Server Room.
class AccessDecisionEngineTest : public ::testing::Test {
protected:
    AccessDecisionEngineTest() {
        m_snapshot.readers["R-101"] = {.id = "R-101", .zoneId = "Z-LOBBY"};
        m_snapshot.readers["R-201"] = {.id = "R-201", .zoneId = "Z-SERVER"};

        core::Schedule business{.id = "S-BUS", .name = "Business Hours", .windows = {}};
        for (unsigned day = Monday.c_encoding(); day <= std::chrono::Friday.c_encoding(); ++day) {
            business.windows.push_back({std::chrono::weekday{day}, 7h, 18h});
        }
        m_snapshot.schedules["S-BUS"] = business;

        m_snapshot.accessGroups["G-STAFF"] = {
            .id = "G-STAFF", .name = "Staff", .zones = {"Z-LOBBY"}, .scheduleId = "S-BUS"};

        m_snapshot.cardholders["CH-1"] = {.id = "CH-1",
                                          .name = "Alice",
                                          .status = core::CardholderStatus::Active,
                                          .expiresAt = std::nullopt,
                                          .accessGroups = {"G-STAFF"}};

        m_snapshot.cards[kCard] = {.credential = kCard, .cardholderId = "CH-1"};
    }

    [[nodiscard]] Decision decide(const std::string& readerId, const core::CardCredential& card,
                                  core::TimePoint now) const {
        return m_engine.decide(AccessRequest{.readerId = readerId, .card = card}, m_snapshot, now);
    }

    core::Cardholder& alice() { return m_snapshot.cardholders.at("CH-1"); }

    static inline const core::CardCredential kCard{.number = "12345", .facilityCode = 42};
    static inline const core::TimePoint kWorkingHours = at(Monday, 9h);

    AccessDecisionEngine m_engine;
    AccessSnapshot m_snapshot;
};

// --- Grant path ------------------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_ActiveCardholderInZoneDuringSchedule_Grants) {
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::grant());
}

// --- MalformedRequest ------------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_EmptyCardNumber_DeniesMalformedRequest) {
    EXPECT_EQ(decide("R-101", {.number = "", .facilityCode = 42}, kWorkingHours),
              Decision::deny(DenyReason::MalformedRequest));
}

TEST_F(AccessDecisionEngineTest, Decide_EmptyReaderId_DeniesMalformedRequest) {
    EXPECT_EQ(decide("", kCard, kWorkingHours), Decision::deny(DenyReason::MalformedRequest));
}

// --- UnknownCard -----------------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_UnregisteredCardNumber_DeniesUnknownCard) {
    EXPECT_EQ(decide("R-101", {.number = "99999", .facilityCode = 42}, kWorkingHours),
              Decision::deny(DenyReason::UnknownCard));
}

TEST_F(AccessDecisionEngineTest, Decide_KnownNumberWrongFacilityCode_DeniesUnknownCard) {
    EXPECT_EQ(decide("R-101", {.number = "12345", .facilityCode = 7}, kWorkingHours),
              Decision::deny(DenyReason::UnknownCard));
}

// --- CardholderSuspended ---------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_SuspendedCardholder_DeniesCardholderSuspended) {
    alice().status = core::CardholderStatus::Suspended;
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours),
              Decision::deny(DenyReason::CardholderSuspended));
}

TEST_F(AccessDecisionEngineTest, Decide_SuspendedAndPastExpiry_ReportsSuspendedFirst) {
    alice().status = core::CardholderStatus::Suspended;
    alice().expiresAt = kWorkingHours - 1h;
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours),
              Decision::deny(DenyReason::CardholderSuspended));
}

// --- CardholderExpired -----------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_ExpiredStatus_DeniesCardholderExpired) {
    alice().status = core::CardholderStatus::Expired;
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::CardholderExpired));
}

TEST_F(AccessDecisionEngineTest, Decide_ExpiryDateInPast_DeniesCardholderExpired) {
    alice().expiresAt = kWorkingHours - 24h;
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::CardholderExpired));
}

TEST_F(AccessDecisionEngineTest, Decide_ExactlyAtExpiryInstant_DeniesCardholderExpired) {
    alice().expiresAt = kWorkingHours;
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::CardholderExpired));
}

TEST_F(AccessDecisionEngineTest, Decide_ExpiryDateInFuture_Grants) {
    alice().expiresAt = kWorkingHours + 1s;
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::grant());
}

// --- NoZoneAccess ----------------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_NoGroupCoversReaderZone_DeniesNoZoneAccess) {
    EXPECT_EQ(decide("R-201", kCard, kWorkingHours), Decision::deny(DenyReason::NoZoneAccess));
}

TEST_F(AccessDecisionEngineTest, Decide_CardholderInNoGroups_DeniesNoZoneAccess) {
    alice().accessGroups.clear();
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::NoZoneAccess));
}

TEST_F(AccessDecisionEngineTest, Decide_NoZoneAccessOutsideSchedule_ReportsNoZoneAccess) {
    // Having no access to the zone matters more than the time of day.
    EXPECT_EQ(decide("R-201", kCard, at(Saturday, 3h)), Decision::deny(DenyReason::NoZoneAccess));
}

// --- OutsideSchedule -------------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_ZoneAllowedButOutsideSchedule_DeniesOutsideSchedule) {
    EXPECT_EQ(decide("R-101", kCard, at(Saturday, 12h)),
              Decision::deny(DenyReason::OutsideSchedule));
}

TEST_F(AccessDecisionEngineTest, Decide_AtScheduleEndBoundary_DeniesOutsideSchedule) {
    EXPECT_EQ(decide("R-101", kCard, at(Monday, 18h)), Decision::deny(DenyReason::OutsideSchedule));
}

TEST_F(AccessDecisionEngineTest, Decide_AtScheduleStartBoundary_Grants) {
    EXPECT_EQ(decide("R-101", kCard, at(Monday, 7h)), Decision::grant());
}

// --- Multiple groups -------------------------------------------------------

TEST_F(AccessDecisionEngineTest, Decide_SecondGroupActiveWhenFirstIsNot_Grants) {
    m_snapshot.schedules["S-WEEKEND"] = {
        .id = "S-WEEKEND", .name = "Weekend", .windows = {{Saturday, 0h, 24h}}};
    m_snapshot.accessGroups["G-WEEKEND"] = {
        .id = "G-WEEKEND", .name = "Weekend", .zones = {"Z-LOBBY"}, .scheduleId = "S-WEEKEND"};
    alice().accessGroups.push_back("G-WEEKEND");

    EXPECT_EQ(decide("R-101", kCard, at(Saturday, 12h)), Decision::grant());
}

TEST_F(AccessDecisionEngineTest, Decide_OtherGroupCoversDifferentZone_DoesNotGrantThisZone) {
    m_snapshot.accessGroups["G-IT"] = {
        .id = "G-IT", .name = "IT", .zones = {"Z-SERVER"}, .scheduleId = "S-BUS"};
    alice().accessGroups.push_back("G-IT");

    EXPECT_EQ(decide("R-201", kCard, kWorkingHours), Decision::grant());
    EXPECT_EQ(decide("R-201", kCard, at(Saturday, 12h)),
              Decision::deny(DenyReason::OutsideSchedule));
}

// --- InternalError (inconsistent snapshot) ---------------------------------

TEST_F(AccessDecisionEngineTest, Decide_UnknownReader_DeniesInternalError) {
    EXPECT_EQ(decide("R-999", kCard, kWorkingHours), Decision::deny(DenyReason::InternalError));
}

TEST_F(AccessDecisionEngineTest, Decide_CardPointsToMissingCardholder_DeniesInternalError) {
    m_snapshot.cardholders.erase("CH-1");
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::InternalError));
}

TEST_F(AccessDecisionEngineTest, Decide_CardholderReferencesMissingGroup_DeniesInternalError) {
    alice().accessGroups.push_back("G-DELETED");
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::InternalError));
}

TEST_F(AccessDecisionEngineTest, Decide_MissingGroupListedFirst_StillDeniesInternalError) {
    alice().accessGroups.insert(alice().accessGroups.begin(), "G-DELETED");
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::InternalError));
}

TEST_F(AccessDecisionEngineTest, Decide_GroupReferencesMissingSchedule_DeniesInternalError) {
    m_snapshot.schedules.erase("S-BUS");
    EXPECT_EQ(decide("R-101", kCard, kWorkingHours), Decision::deny(DenyReason::InternalError));
}

}  // namespace
}  // namespace acs::engine
