#pragma once
#include <cstdint>
// Supplemental addresses verified against the exact hash in installed_game_profile.h.
// Do not apply these addresses to another executable without disassembly review.
namespace shared_loot::profile {
inline constexpr std::uint32_t register_task_rva = 0xeb5540;
inline constexpr std::uint32_t task_storage_rva = 0x4592f28;
inline constexpr unsigned char register_task_prologue[32] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8d, 0x4c, 0x24, 0x20, 0x49,
    0x8b, 0xd8, 0x8b, 0xfa, 0xe8, 0x97, 0x43, 0x7c, 0x01, 0x48, 0x8b, 0x0d, 0xc8, 0xd9, 0x6d, 0x03};
inline constexpr std::uint32_t map_item_manager_rva = 0x3d6eae0;
inline constexpr std::uint32_t world_chr_manager_rva = 0x3d6d018;
inline constexpr std::uint32_t local_player_offset = 0x1e508;
// GameDataMan + 8 and PlayerGameData equipment/storage references are visible
// in the locked award function and its 0x55ff90 / 0x2488c0 callees.
inline constexpr std::uint32_t game_data_manager_rva = 0x3d64fb8;
inline constexpr std::uint32_t player_inventory_offset = 0x408; // 0x2b0 + 0x158
inline constexpr std::uint32_t player_storage_offset = 0x8d0;
// Read-only ChrIns/CSChrDataModule fields. Validate owner, vtable and HP range.
inline constexpr std::uint32_t chr_modules_offset = 0x190;
inline constexpr std::uint32_t chr_hp_offset = 0x138;
inline constexpr std::uint32_t chr_death_flags_offset = 0x1c5;
} // namespace shared_loot::profile
