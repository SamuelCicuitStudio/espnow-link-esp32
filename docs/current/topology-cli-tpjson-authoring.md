# Topology Chain CLI (Fixed `tp.json` Path)

## Fixed File Path

For this project, all chain commands must operate on one fixed file only:

- `D:\Freelancer\cornetb5\EasyDriveway-production\data\icm\o\s\tp.json`

No path argument is accepted by chain commands.

## Locked File Format

`tp.json` must always match the same structure as `lib/tp.json`:

- root keys: `v`, `seed`, `chain`
- chain node keys: `t`, `m`, `vi`
- node tokens: `S`, `R`, `SM`, `RM`

Example:

```json
{
  "v": 2,
  "seed": [1111709919, 67016344, 1086724318, 1796807863, 1956310764, 607379315],
  "chain": [
    { "t": "S", "m": "02:00:00:00:00:07", "vi": -1 },
    { "t": "R", "m": "02:00:00:00:00:12", "vi": -1 },
    { "t": "RM", "m": "02:00:00:00:00:17", "vi": 11 },
    { "t": "SM", "m": "02:00:00:00:00:18", "vi": 6 }
  ]
}
```

## Seeds (Why They Exist)

Seeds are required so topology groups can derive deterministic per-link LMK material during topology stage/commit.

- Each topology slot references a `group_id`.
- Each active `group_id` must have a 32-byte seed in the snapshot groups table.
- If a group seed is missing, runtime can fail with errors like `topology_group_seed_missing` / `topology_group_missing_seed`.

## Seeds (How Many You Need)

Seed count is not "per node". It is "per relay block".

- A relay block is a consecutive run of `R`/`RM` nodes between two separator nodes (`S`/`SM`).
- Required count rule: `seed.length == relay_block_count`.
- Max relay blocks: `12`.

For your current file `data/icm/o/s/tp.json`:

- relay blocks: `6`
- required seeds: `6`
- current seeds: `6` (already valid)

## Seeds (How They Are Generated In WiFi Manager)

WiFi manager uses a seed-id based flow (not raw `seed[]` integers):

- On `topology.save` / `topology.apply`, it reads `topo_seed_id`.
- If empty, it auto-generates one with:
  - `randomSeedIdRoute() = HEX(esp_random()) + HEX(esp_random()) + HEX(millis())`
- During apply, one 32-byte seed is derived per relay block via:
  - `deriveEdgeSeedRoute(seedId, leftSeparator, rightSeparator, groupId)`
- Derivation mixes:
  - `seedId`
  - boundary MACs
  - `groupId`
  - virtual indices
  - role codes
- If no relay block exists, apply fails with `topology_chain_no_relay_group`.

## Important: Two Seed Models In This Project

1. `tp.json` CLI chain file (`v=2` in espnow-link parser):
- Requires `seed` array in the file.
- `seed[]` is user-provided (uint32 list), then expanded to 32-byte group seeds internally.

2. WiFi manager control route flow (`topology.save` / `topology.apply`):
- Uses `topo_seed_id` string and derives 32-byte seeds automatically.
- Does not require a `seed[]` array in request payload.

Do not mix these two models when documenting or debugging chain behavior.

## Commands To Add

These commands always target the fixed file path above:

1. `topology.chain.show`
2. `topology.chain.graph`
3. `topology.chain.clear`
4. `topology.chain.add <S|R|SM|RM> <paired_index|MAC> [vi]`
5. `topology.chain.edit <index> <S|R|SM|RM> <paired_index|MAC> [vi]`
6. `topology.chain.del <index>`
7. `topology.chain.move <from_index> <to_index>`
8. `topology.chain.validate`
9. `topology.chain.fix`
10. `topology.chain.apply`
11. `topology.chain.backup`
12. `topology.chain.restore`
13. `topology.chain.set <chain_spec>`
14. `topology.chain.set.help`

## Bulk Chain Entry (One Command)

Use this command to enter the full chain at once:

- `topology.chain.set <chain_spec>`

Recommended easy format:

- node syntax: `<TYPE>@<PEER>[#<CH>]`
- node separator: `>`
- allowed types: `S`, `R`, `SM`, `RM`
- `<PEER>`: paired index (recommended) or MAC
- `#<CH>` only for `SM`/`RM`

Range rules:

- `S`, `R`: no `#<CH>`
- `SM`: `#1..8`
- `RM`: `#1..16`

Examples:

```text
topology.chain.set S@0>R@3>RM@7#11>R@4>S@1
topology.chain.set S@0>R@5>R@6>SM@8#6
topology.chain.set S@02:00:00:00:00:07>R@02:00:00:00:00:12>RM@02:00:00:00:00:17#11>S@02:00:00:00:00:05
```

Paired-index full-chain example (when `paired` is):

```text
0) 8C:BF:EA:84:E4:98
1) 8C:BF:EA:84:E7:20
2) 8C:BF:EA:85:20:98
```

and role mapping is:

- `0` -> sensor/semu peer
- `1` -> relay peer
- `2` -> remu peer

bulk command:

```text
topology.chain.set S@0>R@1>RM@2#11>R@1>S@0>R@1>R@1>SM@0#6>RM@2#4>RM@2#15>S@0>R@1>RM@2#2>R@1>SM@0#1>R@1>R@1>S@0>RM@2#9>RM@2#14>RM@2#1>RM@2#7>SM@0#2
```

Behavior:

- Parses all nodes in order.
- Validates complete chain rules before writing.
- If valid, overwrites chain in fixed `tp.json` in one operation.
- If invalid, does not modify file and returns the first error with node position.

## Graph Output Requirement

`topology.chain.graph` must print the chain in vertical tree + block style (no seed display), with this visual contract:

```text
[MASTER][TOPO][GRAPH] view=vertical.blocks path=/o/s/tp.json nodes=23

├── +--------------------------------------------------------+
│   |          01 SENSOR mac=02:00:00:00:00:07               |
│   +--------------------------------------------------------+
│   +--------------------------------------+
│   |  02 RELAY mac=02:00:00:00:00:12      |
│   +--------------------------------------+
│   | 03 REMU ch=11 mac=02:00:00:00:00:17  |
│   +--------------------------------------+
│   |  04 RELAY mac=02:00:00:00:00:14      |
│   +--------------------------------------+
│
├── +--------------------------------------------------------+
│   |          05 SENSOR mac=02:00:00:00:00:05               |
│   +--------------------------------------------------------+
│   +--------------------------------------+
│   |  06 RELAY mac=02:00:00:00:00:09      |
│   +--------------------------------------+
│   |  07 RELAY mac=02:00:00:00:00:0B      |
│   +--------------------------------------+
│
├── +--------------------------------------------------------+
│   |           08 SEMU ch=6 mac=02:00:00:00:00:18           |
│   +--------------------------------------------------------+
│   +--------------------------------------+
│   | 09 REMU ch=4 mac=02:00:00:00:00:17   |
│   +--------------------------------------+
│   | 10 REMU ch=15 mac=02:00:00:00:00:17  |
│   +--------------------------------------+
│
├── +--------------------------------------------------------+
│   |          11 SENSOR mac=02:00:00:00:00:03               |
│   +--------------------------------------------------------+
│   +--------------------------------------+
│   |  12 RELAY mac=02:00:00:00:00:0A      |
│   +--------------------------------------+
│   | 13 REMU ch=2 mac=02:00:00:00:00:17   |
│   +--------------------------------------+
│   |  14 RELAY mac=02:00:00:00:00:0D      |
│   +--------------------------------------+
│
├── +--------------------------------------------------------+
│   |           15 SEMU ch=1 mac=02:00:00:00:00:18           |
│   +--------------------------------------------------------+
│   +--------------------------------------+
│   |  16 RELAY mac=02:00:00:00:00:0F      |
│   +--------------------------------------+
│   |  17 RELAY mac=02:00:00:00:00:10      |
│   +--------------------------------------+
│
├── +--------------------------------------------------------+
│   |          18 SENSOR mac=02:00:00:00:00:08               |
│   +--------------------------------------------------------+
│   +--------------------------------------+
│   | 19 REMU ch=9 mac=02:00:00:00:00:17   |
│   +--------------------------------------+
│   | 20 REMU ch=14 mac=02:00:00:00:00:17  |
│   +--------------------------------------+
│   | 21 REMU ch=1 mac=02:00:00:00:00:17   |
│   +--------------------------------------+
│   | 22 REMU ch=7 mac=02:00:00:00:00:17   |
│   +--------------------------------------+
│
└── +--------------------------------------------------------+
    |           23 SEMU ch=2 mac=02:00:00:00:00:18           |
    +--------------------------------------------------------+
```

## Behavior Notes

- `show`, `graph`, `validate`, `fix`, `apply`, `backup`, `restore` operate directly on the fixed file.
- `clear`, `add`, `edit`, `del`, `move` modify in-memory chain then persist to the fixed file.
- `backup` writes a sibling backup file (recommended: `tp.json.bak`).
- `restore` loads from that backup and overwrites `tp.json`.
