"""Unit tests for rkbx_link OSC deck matching and playhead payloads."""

import time

from rkbx_link_osc import (
    DeckTransport,
    RkbxLinkState,
    build_playhead_decks,
    match_deck_index,
    normalize_title,
)


def test_normalize_title_collapses_case_and_space():
    assert normalize_title("  Hello   World ") == "hello world"


def test_match_deck_index_prefers_title():
    state = RkbxLinkState()
    state.deck(1).title = "Track A"
    state.deck(1).position_s = 10.0
    state.deck(1).updated_at = time.monotonic()
    state.deck(2).title = "Track B"
    state.deck(2).position_s = 20.0
    state.deck(2).updated_at = time.monotonic()

    # Slot 0 title matches deck 2 — should not use fallback index 0 → deck 1
    assert match_deck_index("Track B", state, fallback_index=0) == 2


def test_match_deck_index_falls_back_to_slot_order():
    state = RkbxLinkState()
    state.deck(1).position_s = 1.5
    state.deck(1).updated_at = time.monotonic()
    state.deck(2).position_s = 2.5
    state.deck(2).updated_at = time.monotonic()

    assert match_deck_index("", state, fallback_index=0) == 1
    assert match_deck_index("unknown", state, fallback_index=1) == 2


def test_match_deck_index_ignores_stale():
    state = RkbxLinkState()
    state.deck(1).title = "Stale"
    state.deck(1).position_s = 9.0
    state.deck(1).updated_at = time.monotonic() - 10.0

    assert match_deck_index("Stale", state, fallback_index=0) is None


def test_build_playhead_decks_emits_slots():
    state = RkbxLinkState()
    now = time.monotonic()
    state.deck(1).title = "Alpha"
    state.deck(1).position_s = 12.345
    state.deck(1).bpm = 128.0
    state.deck(1).updated_at = now
    state.deck(2).title = "Beta"
    state.deck(2).position_s = 0.5
    state.deck(2).updated_at = now

    decks = build_playhead_decks(["Alpha", "Beta"], state)
    assert len(decks) == 2
    assert decks[0]["slot"] == "A"
    assert decks[0]["position_ms"] == 12345
    assert decks[0]["bpm"] == 128.0
    assert decks[1]["slot"] == "B"
    assert decks[1]["position_ms"] == 500


def test_deck_transport_position_ms_clamps_negative():
    t = DeckTransport(position_s=-1.0)
    assert t.position_ms == 0
