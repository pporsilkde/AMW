# X032 — Graphics Auto / Shadows fix (ArenaMW)

- Auto graphics использует hardware recommendation при включённом hardware-auto.
- Если hardware-auto выключен, Auto использует Balanced fallback.
- Unknown physical GPU больше не падает автоматически в Minimum; software renderer по-прежнему получает Minimum.
- Shadow UI учитывает master enable shadows и не показывает stale actor/object flags.
- Shadow controls корректно включаются/выключаются по наличию caster shadows.
- Связь shadow distance с viewing distance сохраняется, максимум 16384.
- Графические пресеты остаются Auto / Minimum / Low / Balanced / Medium / High / Ultra.
