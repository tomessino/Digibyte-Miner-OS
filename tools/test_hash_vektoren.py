#!/usr/bin/env python3
"""
Testvektoren für den Mining-Kern (Phase 1).

Prüft am Bitcoin-Genesis-Block:
  1. Byte-Reihenfolge des SHA-256d-Ergebnisses (Fehler K1)
  2. Richtige Share-Diff (Ergebnis muss ~2536.43 sein, Pool-Diff-1-Basis)
  3. Netzwerk-Diff aus nbits (Fehler K2, Genesis nbits=0x1d00ffff -> 1.0)
  4. Was die fehlerhafte v6.5-Logik daraus macht (zum Vergleich)

Aufruf:  python3 test_hash_vektoren.py
Die gleichen Zahlen soll später die Firmware im seriellen Log ausgeben
(Selbsttest beim Booten, siehe docs/PLAN.md Phase 1).
"""
import hashlib

DIFF1 = 0xFFFF * 2 ** 208  # diff1-Target (Bitcoin/DGB, "Pool-Diff 1")

GENESIS_HEADER = bytes.fromhex(
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "29ab5f49"
    "ffff001d"
    "1dac2b7c"
)
GENESIS_HASH_DISPLAY = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"


def sha256d(b: bytes) -> bytes:
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def diff_from_raw_hash(raw: bytes) -> float:
    """Richtig: rohes SHA-Ergebnis ist little-endian -> MSB in raw[31]."""
    return DIFF1 / int.from_bytes(raw, "little")


def diff_v65_falsch(raw: bytes) -> float:
    """So rechnet v6.5: raw[0] als MSB (big-endian)."""
    return DIFF1 / int.from_bytes(raw, "big")


def net_diff_from_nbits(nbits: int) -> float:
    exp = nbits >> 24
    mant = nbits & 0x007FFFFF
    target = mant * 256 ** (exp - 3)
    return DIFF1 / target


def main() -> int:
    ok = True
    raw = sha256d(GENESIS_HEADER)

    print("raw[0..3]   =", raw[:4].hex(), "(erwartet 6fe28c0a)")
    print("raw[28..31] =", raw[28:].hex(), "(erwartet 00000000)")
    ok &= raw[:4].hex() == "6fe28c0a" and raw[28:].hex() == "00000000"

    disp = raw[::-1].hex()
    print("Anzeige-Hash =", disp)
    ok &= disp == GENESIS_HASH_DISPLAY

    d_ok = diff_from_raw_hash(raw)
    d_bad = diff_v65_falsch(raw)
    print(f"Share-Diff richtig  = {d_ok:.6f} (erwartet ~2536.43)")
    print(f"Share-Diff v6.5     = {d_bad:.3e} (falsch, Vorfilter hash[0]==0 greift nicht einmal)")
    ok &= 2536 < d_ok < 2537

    nd = net_diff_from_nbits(0x1D00FFFF)
    print(f"Net-Diff nbits 1d00ffff = {nd:.6f} (erwartet 1.0)")
    ok &= abs(nd - 1.0) < 1e-12

    print("Block? (Diff >= NetDiff):", d_ok >= nd, "(erwartet True)")
    ok &= d_ok >= nd

    print("\nERGEBNIS:", "ALLE TESTS OK" if ok else "FEHLER")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
