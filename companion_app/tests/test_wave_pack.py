"""Tests for chunked dense-waveform packing."""

from nowplaying_server import pack_wave_u16


def test_pack_wave_u16_roundtrip_bits():
    wf = [[1, 2, 3, 15], [7, 0, 7, 31], [0, 4, 1, 0]]
    raw = pack_wave_u16(wf)
    assert len(raw) == 6
    for i, (r, g, b, h) in enumerate(wf):
        v = raw[i * 2] | (raw[i * 2 + 1] << 8)
        assert (v & 7) == r
        assert ((v >> 3) & 7) == g
        assert ((v >> 6) & 7) == b
        assert ((v >> 9) & 31) == h


def test_pack_wave_u16_size_for_cyd():
    wf = [[0, 0, 0, 0]] * 2048
    assert len(pack_wave_u16(wf)) == 4096
