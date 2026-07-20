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
    # Near raw (may include a few ms of changed_age extrapolation)
    assert abs(decks[0]["position_ms"] - 12345) < 50
    assert decks[0]["bpm"] == 128.0
    assert decks[1]["slot"] == "B"
    assert abs(decks[1]["position_ms"] - 500) < 50


def test_extrapolate_bridges_flat_osc_repeats():
    """Same /N/time heartbeats must not reset the bridge clock."""
    from rkbx_link_osc import MAX_BRIDGE_S

    state = RkbxLinkState()
    t0 = time.monotonic()
    d = state.deck(1)
    d.title = "T"
    d.position_s = 10.0
    d.last_delta_s = 0.05  # was moving forward
    d.position_changed_at = t0 - 0.4  # inside bridge window
    d.updated_at = t0  # heartbeat just now with same time
    # Fresh repeats → treat as paused (no runaway)
    assert abs(d.extrapolated_position_s(t0) - 10.0) < 0.01
    # Silent gap inside bridge window → advance
    d.updated_at = t0 - 0.5
    assert abs(d.extrapolated_position_s(t0) - 10.4) < 0.05
    # Past bridge window → plateau at raw+MAX (no snap-back)
    d.position_changed_at = t0 - (MAX_BRIDGE_S + 0.5)
    assert abs(d.extrapolated_position_s(t0) - (10.0 + MAX_BRIDGE_S)) < 0.01


def test_extrapolate_stops_on_scratch_back():
    state = RkbxLinkState()
    t0 = time.monotonic()
    d = state.deck(1)
    d.position_s = 12.0
    d.last_delta_s = -0.5  # just scrubbed backward
    d.position_changed_at = t0 - 0.2
    d.updated_at = t0 - 0.2
    assert abs(d.extrapolated_position_s(t0) - 12.0) < 0.01


def test_match_deck_prefers_slot_when_duplicate_titles():
    state = RkbxLinkState()
    now = time.monotonic()
    state.deck(1).title = "Same Track"
    state.deck(1).position_s = 1.0
    state.deck(1).updated_at = now
    state.deck(2).title = "Same Track"
    state.deck(2).position_s = 9.0
    state.deck(2).updated_at = now

    assert match_deck_index("Same Track", state, fallback_index=0) == 1
    assert match_deck_index("Same Track", state, fallback_index=1) == 2


def test_deck_transport_position_ms_clamps_negative():
    t = DeckTransport(position_s=-1.0)
    assert t.position_ms == 0
