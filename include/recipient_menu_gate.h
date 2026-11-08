#pragma once
#include <cstdint>
namespace shared_loot {
enum class MenuGateState { Unavailable, Busy, Settling, Ready };

// A short quiet period lets the game finish closing its menu. This is only
// consulted by the recipient before leasing a delivery from the queue.
class RecipientMenuGate {
  public:
    MenuGateState update(bool readable, bool busy, std::uint64_t now) {
        if (!readable || busy) {
            clear_after_ = 0;
            return readable ? MenuGateState::Busy : MenuGateState::Unavailable;
        }
        if (!clear_after_)
            clear_after_ = now + 250;
        return now >= clear_after_ ? MenuGateState::Ready : MenuGateState::Settling;
    }

  private:
    std::uint64_t clear_after_ = 0;
};
} // namespace shared_loot
