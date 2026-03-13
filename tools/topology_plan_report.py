#!/usr/bin/env python3
"""
Topology dry-run report for chain-format tp.json.

Outputs:
- detected chain (separator -> relay block -> separator ...)
- relay groups + expanded seeds
- per-device secure add list (peer, rid, group, LMK)
- index map per device (negative/positive routing window)

This mirrors current library chain parsing behavior used by topology.deploy.file.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


PROFILE_CODES = {
    "S": 3,   # kProfileSens
    "SM": 4,  # kProfileSemu
    "R": 2,   # kProfileRelay
    "RM": 5,  # kProfileRemu
}

CAP_SLOTS_BY_TYPE = {
    "S": 13,
    "SM": 13,
    "R": 13,
    "RM": 13,
}

MAX_PHYSICAL_DEVICES = 14

ROLE_LABEL_BY_CODE = {
    1: "PMS",
    2: "R",
    3: "S",
    4: "SM",
    5: "RM",
}


@dataclass
class Node:
    idx: int
    typ: str
    mac: bytes
    vi: int


@dataclass
class Group:
    gid: int
    left_sep_idx: int
    right_sep_idx: int
    relay_node_indices: List[int]
    seed32: bytes
    seed_u32: int


@dataclass
class PendingSlot:
    peer_mac: bytes
    peer_role: int
    group_id: int
    local_vi: int
    peer_vi: int
    delta: int
    peer_chain_idx: int


def parse_mac(text: str) -> bytes:
    parts = text.strip().split(":")
    if len(parts) != 6:
        raise ValueError(f"invalid mac: {text}")
    out = bytearray()
    for p in parts:
        if len(p) != 2:
            raise ValueError(f"invalid mac byte: {text}")
        out.append(int(p, 16))
    return bytes(out)


def fmt_mac(mac: bytes) -> str:
    return ":".join(f"{b:02X}" for b in mac)


def enc_vi(vi: int) -> int:
    return 0xFF if vi < 0 else int(vi) & 0xFF


def is_separator(t: str) -> bool:
    return t in ("S", "SM")


def is_relay(t: str) -> bool:
    return t in ("R", "RM")


def expand_seed_u32(seed_u32: int, gid: int) -> bytes:
    # Same algorithm as src/cli/cli_dispatch.cpp::expandSeedU32ToSeedBytes
    state = (seed_u32 ^ 0x9E3779B9 ^ ((gid & 0xFF) * 0x45D9F3B)) & 0xFFFFFFFF
    if state == 0:
        state = 0xA5B35705
    out = bytearray(32)
    for i in range(32):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= (state >> 17) & 0xFFFFFFFF
        state ^= (state << 5) & 0xFFFFFFFF
        state &= 0xFFFFFFFF
        out[i] = state & 0xFF
    if all(b == 0 for b in out):
        out[0] = 0xA5
    return bytes(out)


def derive_lmk(group_seed32: bytes, icm_mac: bytes, local_mac: bytes, peer_mac: bytes, gid: int) -> bytes:
    a = min(local_mac, peer_mac)
    b = max(local_mac, peer_mac)
    msg = b"L2L-v1" + icm_mac + a + b + bytes([gid & 0xFF])
    return hmac.new(group_seed32, msg, hashlib.sha256).digest()[:16]


def parse_nodes(doc: dict) -> List[Node]:
    schema_ver = int(doc.get("v", 0))
    if schema_ver != 2:
        raise ValueError("schema version invalid (expected v=2)")

    raw_chain = doc.get("chain")
    if not isinstance(raw_chain, list) or not raw_chain:
        raise ValueError("chain missing or empty")

    nodes: List[Node] = []
    sensor_macs: set[bytes] = set()
    relay_macs: set[bytes] = set()
    semu_parent_macs: set[bytes] = set()
    remu_parent_macs: set[bytes] = set()
    semu_vi_seen: Dict[bytes, set[int]] = {}
    remu_vi_seen: Dict[bytes, set[int]] = {}

    for i, raw in enumerate(raw_chain):
        if not isinstance(raw, dict):
            raise ValueError(f"chain[{i}] must be object")
        typ = str(raw.get("t", "")).upper()
        if typ not in PROFILE_CODES:
            raise ValueError(f"chain[{i}] invalid type: {typ}")
        mac = parse_mac(str(raw.get("m", "")))
        vi = int(raw.get("vi", -1))
        if typ in ("S", "R") and vi != -1:
            raise ValueError(f"chain[{i}] physical {typ} must use vi=-1")
        if typ == "SM" and not (0 <= vi <= 7):
            raise ValueError(f"chain[{i}] SM vi out of range 0..7")
        if typ == "RM" and not (0 <= vi <= 15):
            raise ValueError(f"chain[{i}] RM vi out of range 0..15")

        if typ == "S":
            if mac in sensor_macs:
                raise ValueError(f"chain[{i}] duplicated sensor mac")
            if mac in relay_macs or mac in semu_parent_macs or mac in remu_parent_macs:
                raise ValueError(f"chain[{i}] sensor mac overlaps relay/semu/remu mac")
            sensor_macs.add(mac)
        elif typ == "R":
            if mac in relay_macs:
                raise ValueError(f"chain[{i}] duplicated relay mac")
            if mac in sensor_macs or mac in semu_parent_macs or mac in remu_parent_macs:
                raise ValueError(f"chain[{i}] relay mac overlaps sensor/semu/remu mac")
            relay_macs.add(mac)
        elif typ == "SM":
            if mac in sensor_macs or mac in relay_macs or mac in remu_parent_macs:
                raise ValueError(f"chain[{i}] semu parent mac overlaps sensor/relay/remu mac")
            semu_parent_macs.add(mac)
            seen = semu_vi_seen.setdefault(mac, set())
            if vi in seen:
                raise ValueError(f"chain[{i}] duplicated SM vi for same parent mac")
            seen.add(vi)
        elif typ == "RM":
            if mac in sensor_macs or mac in relay_macs or mac in semu_parent_macs:
                raise ValueError(f"chain[{i}] remu parent mac overlaps sensor/relay/semu mac")
            remu_parent_macs.add(mac)
            seen = remu_vi_seen.setdefault(mac, set())
            if vi in seen:
                raise ValueError(f"chain[{i}] duplicated RM vi for same parent mac")
            seen.add(vi)

        nodes.append(Node(idx=i, typ=typ, mac=mac, vi=vi))

    physical_device_count = (
        len(sensor_macs) + len(relay_macs) + len(semu_parent_macs) + len(remu_parent_macs)
    )
    if physical_device_count > MAX_PHYSICAL_DEVICES:
        raise ValueError(f"physical device count exceeds {MAX_PHYSICAL_DEVICES}")

    if not is_separator(nodes[0].typ) or not is_separator(nodes[-1].typ):
        raise ValueError("chain must start and end with S/SM")
    for i in range(1, len(nodes)):
        if is_separator(nodes[i - 1].typ) and is_separator(nodes[i].typ):
            raise ValueError(f"adjacent separators not allowed at {i-1},{i}")
    return nodes


def build_groups(nodes: List[Node], seeds_u32: List[int]) -> List[Group]:
    groups: List[Group] = []
    i = 0
    while i < len(nodes):
        if is_separator(nodes[i].typ):
            i += 1
            continue
        if i == 0 or i + 1 >= len(nodes):
            raise ValueError("relay block boundary invalid")
        if not is_separator(nodes[i - 1].typ):
            raise ValueError("relay block missing left separator")
        left_sep = i - 1
        start = i
        while i < len(nodes) and is_relay(nodes[i].typ):
            i += 1
        if i >= len(nodes) or not is_separator(nodes[i].typ):
            raise ValueError("relay block missing right separator")
        right_sep = i
        gid = len(groups) + 1
        relay_nodes = list(range(start, i))
        groups.append(
            Group(
                gid=gid,
                left_sep_idx=left_sep,
                right_sep_idx=right_sep,
                relay_node_indices=relay_nodes,
                seed32=b"",
                seed_u32=0,
            )
        )

    if not groups:
        raise ValueError("no relay blocks found")
    if len(groups) != len(seeds_u32):
        raise ValueError(
            f"seed count mismatch: groups={len(groups)} seeds={len(seeds_u32)}"
        )

    for g, seed_u32 in zip(groups, seeds_u32):
        g.seed_u32 = int(seed_u32) & 0xFFFFFFFF
        g.seed32 = expand_seed_u32(g.seed_u32, g.gid)
    return groups


def build_targets(nodes: List[Node], groups: List[Group]) -> Dict[bytes, List[PendingSlot]]:
    # Build links: left-separator <-> relay, right-separator <-> relay for each block relay.
    links: List[Tuple[int, int, int]] = []
    for g in groups:
        for r in g.relay_node_indices:
            links.append((g.left_sep_idx, r, g.gid))
            links.append((g.right_sep_idx, r, g.gid))

    targets: Dict[bytes, List[PendingSlot]] = {}

    def upsert(local: Node, peer: Node, gid: int) -> None:
        if local.mac == peer.mac and local.vi == peer.vi and local.typ == peer.typ:
            return
        arr = targets.setdefault(local.mac, [])
        candidate = PendingSlot(
            peer_mac=peer.mac,
            peer_role=PROFILE_CODES[peer.typ],
            group_id=gid,
            local_vi=enc_vi(local.vi),
            peer_vi=enc_vi(peer.vi),
            delta=peer.idx - local.idx,
            peer_chain_idx=peer.idx,
        )
        if candidate.delta == 0:
            return

        same = None
        for i, s in enumerate(arr):
            if (
                s.peer_mac == candidate.peer_mac
                and s.peer_role == candidate.peer_role
                and s.peer_vi == candidate.peer_vi
            ):
                same = i
                break
        if same is None:
            arr.append(candidate)
            return

        existing = arr[same]
        a = abs(existing.delta)
        b = abs(candidate.delta)
        if b < a:
            arr[same] = candidate
            return
        if b == a and existing.local_vi == 0xFF and candidate.local_vi != 0xFF:
            existing.local_vi = candidate.local_vi
            existing.group_id = candidate.group_id
            arr[same] = existing

    for a, b, gid in links:
        upsert(nodes[a], nodes[b], gid)
        upsert(nodes[b], nodes[a], gid)
    return targets


def node_labels_for_mac(nodes: List[Node], mac: bytes) -> str:
    labels: List[str] = []
    for n in nodes:
        if n.mac != mac:
            continue
        if n.vi < 0:
            labels.append(n.typ)
        else:
            labels.append(f"{n.typ}({n.vi})")
    if not labels:
        return "?"
    return ",".join(labels)


def slot_cap_for_mac(nodes: List[Node], mac: bytes) -> int:
    kinds = sorted({n.typ for n in nodes if n.mac == mac})
    if not kinds:
        return 12
    # One physical MAC must map to one physical role family in topology.
    # For child-based roles (SM/RM), repeated entries with different vi are expected.
    return int(CAP_SLOTS_BY_TYPE.get(kinds[0], 12))


def role_label(role_code: int) -> str:
    return ROLE_LABEL_BY_CODE.get(int(role_code), str(role_code))


def local_sensor_label(local_vi: int) -> str:
    return "S" if int(local_vi) == 0xFF else f"SM({int(local_vi)})"


def render_chain(nodes: List[Node], groups: List[Group]) -> List[str]:
    lines: List[str] = []
    if not groups:
        return lines
    # Compact block chain line: S/SM -> [R/RM,...] -> S/SM ...
    parts: List[str] = []
    parts.append(f"{nodes[groups[0].left_sep_idx].typ}")
    for g in groups:
        block = ",".join(nodes[i].typ for i in g.relay_node_indices)
        parts.append(f"[{block}]")
        parts.append(f"{nodes[g.right_sep_idx].typ}")
    lines.append(" -> ".join(parts))

    # Detailed by group.
    for g in groups:
        left = nodes[g.left_sep_idx]
        right = nodes[g.right_sep_idx]
        relays = ",".join(
            f"{nodes[i].typ}({fmt_mac(nodes[i].mac)}"
            + (f",vi={nodes[i].vi}" if nodes[i].vi >= 0 else "")
            + ")"
            for i in g.relay_node_indices
        )
        lines.append(
            f"gid={g.gid}: {left.typ}({fmt_mac(left.mac)}"
            + (f",vi={left.vi}" if left.vi >= 0 else "")
            + f") -> {relays} -> {right.typ}({fmt_mac(right.mac)}"
            + (f",vi={right.vi}" if right.vi >= 0 else "")
            + ")"
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Topology dry-run planner for tp.json")
    parser.add_argument(
        "--topology",
        default="examples/master/data/o/s/tp.json",
        help="Path to chain topology json (default: examples/master/data/o/s/tp.json)",
    )
    parser.add_argument(
        "--icm-mac",
        default="8C:BF:EA:83:5E:60",
        help="ICM MAC used for LMK derivation (default: 8C:BF:EA:83:5E:60)",
    )
    args = parser.parse_args()

    topo_path = Path(args.topology)
    if not topo_path.exists():
        print(f"[ERROR] topology file not found: {topo_path}")
        return 2

    try:
        doc = json.loads(topo_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"[ERROR] invalid json: {exc}")
        return 2

    try:
        icm_mac = parse_mac(args.icm_mac)
    except Exception as exc:
        print(f"[ERROR] invalid --icm-mac: {exc}")
        return 2

    seeds = doc.get("seed")
    if not isinstance(seeds, list) or not seeds:
        print("[ERROR] seed array missing or empty")
        return 2
    try:
        seeds_u32 = [int(x) & 0xFFFFFFFF for x in seeds]
    except Exception:
        print("[ERROR] seed array must contain integers")
        return 2

    try:
        nodes = parse_nodes(doc)
        groups = build_groups(nodes, seeds_u32)
        targets = build_targets(nodes, groups)
    except Exception as exc:
        print(f"[ERROR] {exc}")
        return 2

    group_by_id = {g.gid: g for g in groups}

    print(f"[TOPO][PLAN] file={topo_path}")
    print(f"[TOPO][PLAN] icm_mac={fmt_mac(icm_mac)}")
    print(f"[TOPO][PLAN] nodes={len(nodes)} groups={len(groups)} targets={len(targets)}")
    print()

    print("[TOPO][CHAIN]")
    for line in render_chain(nodes, groups):
        print(f"  {line}")
    print()

    print("[TOPO][SEEDS]")
    for g in groups:
        print(
            f"  gid={g.gid} seed_u32={g.seed_u32} seed32={g.seed32.hex().upper()}"
        )
    print()

    for mac in sorted(targets.keys()):
        slots = list(targets[mac])
        neg = [s for s in slots if s.delta < 0]
        pos = [s for s in slots if s.delta > 0]

        def sort_key(s: PendingSlot):
            return (
                abs(int(s.delta)),
                int(s.peer_chain_idx),
                s.peer_mac,
                int(s.peer_role),
                int(s.peer_vi),
            )

        neg.sort(key=sort_key)
        pos.sort(key=sort_key)
        ordered = neg + pos

        # Assign runtime rid exactly as C++ parser does.
        rid_slots: List[Tuple[int, PendingSlot]] = []
        for i, s in enumerate(ordered):
            if i < len(neg):
                rid = -(i + 1)
            else:
                rid = (i - len(neg) + 1)
            rid_slots.append((rid, s))

        # Human-facing symmetric map: far-left .. -1, then +1 .. far-right
        idx_map = list(range(-len(neg), 0)) + list(range(1, len(pos) + 1))
        groups_used = sorted({s.group_id for _, s in rid_slots})
        peers_used = sorted({s.peer_mac for _, s in rid_slots})
        slot_cap = slot_cap_for_mac(nodes, mac)
        status = "OK" if len(rid_slots) <= slot_cap else f"OVER_CAP({len(rid_slots)}/{slot_cap})"

        print(
            f"[TOPO][DEVICE] mac={fmt_mac(mac)} kind={node_labels_for_mac(nodes, mac)} "
            f"slots={len(rid_slots)} neg={len(neg)} pos={len(pos)} groups={groups_used} status={status}"
        )
        rid_slots_print = sorted(rid_slots, key=lambda x: x[0])
        peer_key_gid: Dict[bytes, int] = {}
        peer_slot_gids: Dict[bytes, set[int]] = {}
        for _, s in rid_slots_print:
            peer_slot_gids.setdefault(s.peer_mac, set()).add(int(s.group_id))
            prev_gid = peer_key_gid.get(s.peer_mac)
            if prev_gid is None or int(s.group_id) < prev_gid:
                peer_key_gid[s.peer_mac] = int(s.group_id)
        has_semu_child = any(n.mac == mac and n.typ == "SM" for n in nodes)
        if not has_semu_child:
            print(f"  index_map={idx_map}")
            print("  key_plan:")
            for peer in sorted(peer_key_gid.keys()):
                slot_groups = sorted(peer_slot_gids.get(peer, set()))
                print(
                    f"    peer={fmt_mac(peer)} key_gid={peer_key_gid[peer]} slot_gids={slot_groups}"
                )
            print("  secure_add:")
            for rid, s in rid_slots_print:
                key_gid = peer_key_gid.get(s.peer_mac, int(s.group_id))
                group = group_by_id.get(key_gid)
                if group is None:
                    print(
                        f"    rid={rid:+d} peer={fmt_mac(s.peer_mac)} ERROR=missing_group({key_gid})"
                    )
                    continue
                lmk = derive_lmk(group.seed32, icm_mac, mac, s.peer_mac, key_gid)
                print(
                    f"    rid={rid:+d} peer={fmt_mac(s.peer_mac)} "
                    f"peer_role={role_label(s.peer_role)} slot_gid={s.group_id} key_gid={key_gid} "
                    f"local_vi={s.local_vi} peer_vi={s.peer_vi} lmk={lmk.hex().upper()}"
                )
            if peers_used:
                print("  peers=" + ", ".join(fmt_mac(p) for p in peers_used))
            print()
            continue

        # SEMU host: split by local sensor identity so each child has its own before/after map.
        sensor_local_vis: List[int] = []
        if any(n.mac == mac and n.typ == "S" for n in nodes):
            sensor_local_vis.append(0xFF)
        sensor_local_vis.extend(sorted({n.vi for n in nodes if n.mac == mac and n.typ == "SM"}))

        for lvi in sensor_local_vis:
            local_slots = [s for s in slots if int(s.local_vi) == int(lvi)]
            if not local_slots:
                continue
            local_neg = [s for s in local_slots if s.delta < 0]
            local_pos = [s for s in local_slots if s.delta > 0]
            local_neg.sort(key=sort_key)
            local_pos.sort(key=sort_key)
            local_ordered = local_neg + local_pos

            local_rid_slots: List[Tuple[int, PendingSlot]] = []
            for i, s in enumerate(local_ordered):
                if i < len(local_neg):
                    rid = -(i + 1)
                else:
                    rid = (i - len(local_neg) + 1)
                local_rid_slots.append((rid, s))

            local_idx_map = list(range(-len(local_neg), 0)) + list(range(1, len(local_pos) + 1))
            local_groups = sorted({s.group_id for _, s in local_rid_slots})
            local_rid_slots_print = sorted(local_rid_slots, key=lambda x: x[0])
            local_peer_slot_gids: Dict[bytes, set[int]] = {}
            for _, s in local_rid_slots_print:
                local_peer_slot_gids.setdefault(s.peer_mac, set()).add(int(s.group_id))

            before_items: List[str] = []
            after_items: List[str] = []
            for rid, s in local_rid_slots_print:
                item = (
                    f"{rid:+d}:{fmt_mac(s.peer_mac)}"
                    f"({role_label(s.peer_role)}"
                    + (f",vi={s.peer_vi}" if s.peer_vi != 0xFF else "")
                    + ")"
                )
                if rid < 0:
                    before_items.append(item)
                else:
                    after_items.append(item)

            print(
                f"  [TOPO][SENSOR_VIEW] sensor={local_sensor_label(lvi)} "
                f"slots={len(local_rid_slots)} neg={len(local_neg)} pos={len(local_pos)} groups={local_groups}"
            )
            print(f"    index_map={local_idx_map}")
            print(f"    before=[{', '.join(before_items)}]" if before_items else "    before=[]")
            print(f"    after=[{', '.join(after_items)}]" if after_items else "    after=[]")
            print("    key_plan:")
            for peer in sorted(local_peer_slot_gids.keys()):
                slot_groups = sorted(local_peer_slot_gids.get(peer, set()))
                print(
                    f"      peer={fmt_mac(peer)} key_gid={peer_key_gid.get(peer, int(slot_groups[0]))} slot_gids={slot_groups}"
                )
            print("    secure_add:")
            for rid, s in local_rid_slots_print:
                key_gid = peer_key_gid.get(s.peer_mac, int(s.group_id))
                group = group_by_id.get(key_gid)
                if group is None:
                    print(
                        f"      rid={rid:+d} peer={fmt_mac(s.peer_mac)} ERROR=missing_group({key_gid})"
                    )
                    continue
                lmk = derive_lmk(group.seed32, icm_mac, mac, s.peer_mac, key_gid)
                print(
                    f"      rid={rid:+d} peer={fmt_mac(s.peer_mac)} "
                    f"peer_role={role_label(s.peer_role)} slot_gid={s.group_id} key_gid={key_gid} "
                    f"local_vi={s.local_vi} peer_vi={s.peer_vi} lmk={lmk.hex().upper()}"
                )

        if peers_used:
            print("  peers=" + ", ".join(fmt_mac(p) for p in peers_used))
        print()

    print("[TOPO][DONE]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
