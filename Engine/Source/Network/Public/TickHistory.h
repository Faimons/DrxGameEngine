#pragma once

/**
 * DRX Engine - Tick history
 * Fixed-size ring buffer of states indexed by simulation tick. Used for snapshot
 * interpolation (clients render slightly in the past), client-side prediction
 * (inputs/states per tick for reconciliation) and server lag compensation
 * (rewinding hitboxes to the tick a client saw).
 */

#include "Core/Public/CoreTypes.h"

#include <array>

namespace DRX::Network {

template<typename T, size_t Capacity = 128>
class TickHistory {
    static_assert(Capacity > 0, "TickHistory needs capacity");

public:
    void Store(uint32 tick, const T& value) {
        Slot& slot = slots_[tick % Capacity];
        slot.tick = tick;
        slot.value = value;
        slot.valid = true;
        if (!has_latest_ || tick > latest_tick_) {
            latest_tick_ = tick;
            has_latest_ = true;
        }
    }

    const T* Find(uint32 tick) const {
        const Slot& slot = slots_[tick % Capacity];
        return slot.valid && slot.tick == tick ? &slot.value : nullptr;
    }

    T* Find(uint32 tick) {
        Slot& slot = slots_[tick % Capacity];
        return slot.valid && slot.tick == tick ? &slot.value : nullptr;
    }

    bool HasLatest() const { return has_latest_; }
    uint32 GetLatestTick() const { return latest_tick_; }

    /**
     * Finds the two stored states around a fractional tick: returns false when the time is not
     * covered. `alpha` is the blend factor from `from` to `to` (0..1).
     */
    bool FindBracket(float64 tick, const T*& from, const T*& to, float32& alpha) const {
        if (!has_latest_ || tick < 0.0) return false;
        const auto base = static_cast<uint32>(tick);
        if (base > latest_tick_) return false;

        // Search backwards for the closest state at or before `tick`
        for (uint32 back = 0; back < Capacity && back <= base; ++back) {
            const uint32 a = base - back;
            const T* state_a = Find(a);
            if (!state_a) continue;
            for (uint32 b = base + 1; b <= latest_tick_ && b - a < Capacity; ++b) {
                if (const T* state_b = Find(b)) {
                    from = state_a;
                    to = state_b;
                    alpha = static_cast<float32>((tick - static_cast<float64>(a)) / static_cast<float64>(b - a));
                    return true;
                }
            }
            if (tick == static_cast<float64>(a)) {
                from = to = state_a;
                alpha = 0.0f;
                return true;
            }
            return false;
        }
        return false;
    }

    // Drops everything older than `tick`
    void DiscardBefore(uint32 tick) {
        for (Slot& slot : slots_) {
            if (slot.valid && slot.tick < tick) slot.valid = false;
        }
    }

    void Clear() {
        for (Slot& slot : slots_) slot.valid = false;
        has_latest_ = false;
        latest_tick_ = 0;
    }

    static constexpr size_t GetCapacity() { return Capacity; }

private:
    struct Slot {
        uint32 tick = 0;
        T value{};
        bool valid = false;
    };
    std::array<Slot, Capacity> slots_{};
    uint32 latest_tick_ = 0;
    bool has_latest_ = false;
};

} // namespace DRX::Network
