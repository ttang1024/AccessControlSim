#pragma once

#include "core/card.h"
#include "core/ids.h"

namespace acs::engine {

// A badge swipe as the engine sees it. Protocol details such as `seq` belong
// to the network layer and are not part of the decision.
struct AccessRequest {
    core::ReaderId readerId;
    core::CardCredential card;
};

}  // namespace acs::engine
