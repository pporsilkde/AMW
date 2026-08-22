# ArenaMW Refined Alchemy + Weapon Poisons

This is a native C++ ArenaMW implementation inspired by the workflow and design goals of
Refined Alchemy by kapitansen (MIT licensed):
https://github.com/kapitansen/RefinedAlchemy

The ArenaMW implementation does not require OpenMW Lua. It is integrated into the existing
ArenaMW/EncoreMP alchemy balance, inventory, combat, projectile and save-game systems.

## Alchemy UI

- Potion / Poison mode buttons.
- Live preview uses the actual magnitude and duration that will be written to the crafted record.
- Four ingredient slots and the existing name/effect inventory filtering remain available.
- Batch brewing is limited to the amount currently craftable from the selected ingredient stacks.
- The last successful brew is displayed and the last recipe can be restored with one button.
- Apparatus tooltips describe their ArenaMW role.

## Permanent ingredient knowledge

Ingredient effects are no longer only a transient tooltip calculation. Known effects are stored
per save game in the `AMAL` record.

Default discovery rules (`[ArenaMW Alchemy]` in `settings-default.cfg`):

- Eating an ingredient permanently reveals its first valid effect.
- Successful brewing confirms effects used by the result; three confirmations reveal an effect.
- Alchemy skill permanently reveals slots at 15 / 30 / 45 / 60 by default.

The thresholds and discovery methods are configurable.

## Refined mixture rules

- Potion mode keeps beneficial shared effects.
- Poison mode keeps harmful shared effects.
- A third or fourth ingredient contributing to the same effect adds +10% per extra contributor.
- Supported complementary effect pairs receive a deterministic +10% synergy bonus.
- The existing ArenaMW/EncoreMP ingredient-value, mortar and potion-budget balancing remains the
  base formula.
- Fatigue can affect brewing success.
- Failure mode is configurable: forgiving, tainted-potion, or strict ingredient loss.

## Apparatus roles

- Mortar & Pestle: base extraction strength (existing ArenaMW quality curve).
- Alembic: reduces harmful components in Potion mode; concentrates them in Poison mode.
- Calcinator: boosts extracted effects, with a larger harmful-effect bonus.
- Retort: boosts beneficial effects; in Poison mode it also strengthens toxins.

## Weapon poison coating

A crafted potion is treated as a weapon poison when every effect on it is harmful. Using such a
potion normally coats the currently equipped right-hand weapon instead of drinking it. Forced or
scripted use keeps the original potion application path for compatibility.

Coating charges scale with Alchemy by default:

- 0-19: 1 hit
- 20-39: 2 hits
- 40-59: 3 hits
- 60-79: 4 hits
- 80+: 5 hits (configurable cap)

Charges are consumed only by confirmed hits. Misses do not consume a charge. The poison effects
are inflicted through the engine's normal magic-effect pipeline, so resistance, weakness,
reflection and other spell-effect mechanics remain active.

The coating belongs to the exact weapon instance, not just its base record. Coated and uncoated
weapons therefore do not stack. The poison ID and remaining charges are persisted in `ObjectState`
and survive save/load. In-flight projectile state also preserves the coating identity; bows,
crossbows and thrown weapons are handled by the native projectile hit path.

## Save compatibility

Old saves do not contain `AMAL`, `APOI` or `APCH` and load with empty alchemy knowledge / no coating.
New saves preserve knowledge, last recipe, weapon coating and in-flight projectile coating.

Rolling a save created with this patch back to an older ArenaMW executable is not guaranteed,
because the older executable does not know the new optional state fields/record.

## Main settings

```ini
[ArenaMW Alchemy]
learn from eating = true
learn from brewing = true
learn from skill = true
learning confirmations = 3
synergy = true
ingredient bonus = true
failure mode = 1
max poison charges = 5
skill reveal effect 1 = 15
skill reveal effect 2 = 30
skill reveal effect 3 = 45
skill reveal effect 4 = 60
fatigue affects success = true
```

## ArenaMW cumulative integration

This merged ArenaMW build keeps the anti-exploit rules from the native alchemy branch:

- Potion strength uses base Alchemy, base Intelligence and base Luck.
- Permanent effect discovery from skill uses base Alchemy only.
- Poison coating charge count uses base Alchemy only.
- Ingredient merchant value used for power/success/XP is capped by `[Alchemy] ingredient value power cap`.
- Repeated alchemical active effects use ArenaMW's duration-stacking rule instead of multiplying magnitude.
- Arrow Stick and weapon poison interoperate: a poisoned thrown weapon that sticks keeps its remaining coating charges.

The inventory single-click/list-view and right-handed bow presentation changes are independent from
these mechanics; bows remain logically equipped in ArenaMW's standard right-hand equipment slot,
so poison coating and projectile state continue to use the normal weapon path.
