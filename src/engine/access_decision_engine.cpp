#include "engine/access_decision_engine.h"

namespace acs::engine {

namespace {

bool isExpired(const core::Cardholder& cardholder, core::TimePoint now) {
    return cardholder.status == core::CardholderStatus::Expired ||
           (cardholder.expiresAt.has_value() && now >= *cardholder.expiresAt);
}

}  // namespace

// Checks run from "who is asking" to "may they enter here, now". The first
// failing check decides the reason. The rules behind this order are in ADR-002.
Decision AccessDecisionEngine::decide(const AccessRequest& request, const AccessSnapshot& snapshot,
                                      core::TimePoint now) const {
    using core::DenyReason;

    if (request.readerId.empty() || request.card.number.empty()) {
        return Decision::deny(DenyReason::MalformedRequest);
    }

    const auto reader = snapshot.readers.find(request.readerId);
    if (reader == snapshot.readers.end()) {
        return Decision::deny(DenyReason::InternalError);
    }

    const auto card = snapshot.cards.find(request.card);
    if (card == snapshot.cards.end()) {
        return Decision::deny(DenyReason::UnknownCard);
    }

    const auto holder = snapshot.cardholders.find(card->second.cardholderId);
    if (holder == snapshot.cardholders.end()) {
        return Decision::deny(DenyReason::InternalError);
    }
    const core::Cardholder& cardholder = holder->second;

    if (cardholder.status == core::CardholderStatus::Suspended) {
        return Decision::deny(DenyReason::CardholderSuspended);
    }
    if (isExpired(cardholder, now)) {
        return Decision::deny(DenyReason::CardholderExpired);
    }

    // Look at every group, not just until the first grant, so a broken
    // reference always gives InternalError whatever order the groups are in.
    const core::ZoneId& zoneId = reader->second.zoneId;
    bool zoneCovered = false;
    bool scheduleActive = false;
    for (const core::AccessGroupId& groupId : cardholder.accessGroups) {
        const auto group = snapshot.accessGroups.find(groupId);
        if (group == snapshot.accessGroups.end()) {
            return Decision::deny(DenyReason::InternalError);
        }
        if (!group->second.zones.contains(zoneId)) {
            continue;
        }
        zoneCovered = true;

        const auto schedule = snapshot.schedules.find(group->second.scheduleId);
        if (schedule == snapshot.schedules.end()) {
            return Decision::deny(DenyReason::InternalError);
        }
        scheduleActive = scheduleActive || schedule->second.isActiveAt(now);
    }

    if (!zoneCovered) {
        return Decision::deny(DenyReason::NoZoneAccess);
    }
    if (!scheduleActive) {
        return Decision::deny(DenyReason::OutsideSchedule);
    }
    return Decision::grant();
}

}  // namespace acs::engine
