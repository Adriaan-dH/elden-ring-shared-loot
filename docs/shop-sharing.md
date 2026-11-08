# Shop sharing and independent logging: 0.5.0-test.2

Local candidate, not committed or publicly released. Builds on the buffered delivery work in test.1.

## Settings

SharedLoot.ini is the commented, Notepad-editable settings file. Release defaults are logging.enabled=0 and sharing.shop_purchases=1. Shop sharing controls both sending and accepting shop gifts. Gathering retains its existing option. Restart after editing. The local test PC explicitly uses logging.enabled=1; the override is never packaged.

The worker no longer exits when logging is disabled or a log cannot be opened. Logging off returns a null sink before creating any directory/file. Hook installation, Steam, queues, grants and reports continue. Existing logs are not deleted. The old diagnostics.enabled value is a logging fallback only when the new setting is absent. File errors are non-fatal.

## Shop transaction adapter

Purchases bypass the old world-award hook. A separate guarded transaction hook is used instead of guessing from inventory increases while a shop happens to be open.

In the locked executable, method RVA 0x79ac40 creates the selected shop item, updates stock/event state via 0x787610, and increments committed-purchase statistics through 0x86eb00. The last function increments a 16-bit counter at MenuMan +8 -> +0x94. Failed item-add branches skip this path. Storage and preview operations do not call this purchase commit method.

The context holds the resolved raw item ID at +0x4c, ShopLineupParam row at +0x60, optional raw gem at +0x7c, and purchased units at +0x80. The row's set quantity at +0x1a multiplies units; equip type +0x17 == 5 selects gem-equipped weapons. These relationships were inspected in the transaction and helpers at 0x73e900, 0x849b10, 0x7851d0 and 0x7852b0. The latter helpers handle normal inventory and storage splits. The mod does not call them independently.

The original transaction executes once, preserving its context and raw return. Before/after inventory+storage counts and the purchase counter are compared. Sharing requires one committed-purchase increment and a positive actual item delta no greater than requested. Missing probes, zero/invalid deltas and uncommitted actions are not broadcast. Native payment and stock behavior are unchanged. The award recursion guard suppresses a second capture inside a purchase.

The entire executable hash and a separate 32-byte purchase prologue must match. Changed/already-hooked shop code disables only this adapter. Static analysis and synthetic tests do not prove live merchant/randomizer compatibility; that remains the next test.

## Protocol and diagnostics

Protocol 3 adds kind 6 for shop purchases, preserving kind 3 for ordinary awards and completion/failure reports. Receiver opt-out reports shop_sharing_disabled_on_receiver without a grant. Normal pickups are unaffected. Both PCs must update together. No automatic resend is added.

## Checks and live test

Portable tests cover set quantities, gems, counter wrap, positive/partial/zero changes, failed/unreadable purchases and exactly one native call. Router tests cover both opt-out directions, ordinary pickups while shops are off, buffering and completion. Native Windows tests exercise real INI defaults/overrides/migration, logging-off with no filesystem output, enabled logging, file errors and the actual detour against a synthetic purchase function.

Next: buy one inexpensive item and a stack from each PC. Check that only the buyer pays and recipients get the actual quantity once. Test cancellation, failed payment, storage withdrawal and selling. Restart with shop_purchases=0 on one PC: its purchases should not broadcast and it should not receive shop gifts, while floor loot still shares. Repeat with a paused/dead/loading recipient and with weapon/gem shop entries under the identical randomizer. Collect both PCs' logs if anything is missing.
