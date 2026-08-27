#!/usr/bin/env python3
"""Unit-ish checks for the M19 channel simulator rework (run with the
project venv: .venv/bin/python3 tools/channel/test_sim_channel.py)."""

import queue
import sys
import time

sys.path.insert(0, "tools/channel")
import sim_channel as sc


def test_parse_ue_quality():
    q = sc.parse_ue_quality("10001=good, 10003=poor")
    assert set(q) == {10001, 10003}
    assert q[10001]["snr_db"] == 28.0
    assert q[10003]["loss"] == 0.08
    try:
        sc.parse_ue_quality("10001=amazing")
        assert False, "bad profile name must raise"
    except ValueError:
        pass
    assert sc.parse_ue_quality("") == {}


def test_sigma_monotonic():
    # Higher SNR -> less noise; sigma is absolute (power-control visible).
    assert sc.sigma_for_snr(28.0) < sc.sigma_for_snr(10.0)
    assert sc.sigma_for_snr(10.0) > 0


def test_worker_sheds_oldest_and_relays():
    sent = []
    worker = sc.EndpointWorker("t", 19999, _FakeSock(sent), 0.0,
                               lambda d: d + b"!")
    worker.q = queue.Queue(maxsize=3)
    for i in range(5):  # overflow by 2: the two oldest must be shed
        worker.offer(bytes([i]))
    worker.start()
    deadline = time.time() + 3
    while time.time() < deadline and len(sent) < 3:
        time.sleep(0.01)
    worker.running = False
    assert len(sent) == 3, sent
    payloads = sorted(d[0] for _, d in sent)
    assert payloads == [2, 3, 4], payloads  # 0 and 1 shed
    assert all(d.endswith(b"!") for _, d in sent)


class _FakeSock:
    def __init__(self, out):
        self.out = out

    def sendto(self, data, addr):
        self.out.append((addr[1], data))


def test_parse_cell_quality():
    q = sc.parse_cell_quality("10001=1:good,10001=2:bad,10003=2:mid")
    assert q[10001][1]["snr_db"] == 28.0
    assert q[10001][2]["snr_db"] == 15.0  # "bad" (HO trigger)
    assert q[10001][2]["loss"] == 0.15
    assert q[10003][2]["loss"] == 0.03
    try:
        sc.parse_cell_quality("10001=3:good")  # cell ids are 1|2... parser accepts; move gates
    except ValueError:
        assert False, "valid profile name must parse"
    assert sc.parse_cell_quality("") == {}


if __name__ == "__main__":
    test_parse_ue_quality()
    test_parse_cell_quality()
    test_sigma_monotonic()
    test_worker_sheds_oldest_and_relays()
    print("sim_channel unit checks: OK")
