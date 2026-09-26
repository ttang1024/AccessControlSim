#pragma once

namespace acs::core {

// Lets std::visit take one lambda per alternative:
//   std::visit(Overloaded{[](const A&) {...}, [](const B&) {...}}, variant);
// Leaving out an alternative is a compile error, not a runtime surprise.
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace acs::core
