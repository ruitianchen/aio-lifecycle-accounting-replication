"""Frozen estimands and arithmetic, independent of experiment execution."""
import math
import numpy as np


def interval_metrics(intervals):
    points = []
    for a, b in intervals:
        assert b >= a
        if b > a:
            points.extend([(a, 1), (b, -1)])
    active = peak = union = 0
    previous = None
    for t, delta in sorted(points):
        if previous is not None and active:
            union += t - previous
        active += delta
        assert active >= 0
        peak = max(peak, active)
        previous = t
    assert active == 0
    return {'count': len(intervals), 'sum': sum(b-a for a, b in intervals), 'union': union, 'peak': peak}


def exact_sign_p(differences):
    d = [x for x in differences if x != 0]
    n = len(d)
    if not n:
        return 1.0
    k = min(sum(x > 0 for x in d), sum(x < 0 for x in d))
    return min(1.0, 2 * sum(math.comb(n, i) for i in range(k+1)) / 2**n)


def holm_adjust(ps):
    order = sorted(range(len(ps)), key=lambda i: ps[i])
    out = [0.0]*len(ps)
    current = 0.0
    for rank, i in enumerate(order):
        current = max(current, min(1.0, (len(ps)-rank)*ps[i]))
        out[i] = current
    return out


def describe_pair(reference, target, bootstrap_samples=10000, seed=20261024):
    r, s = np.asarray(reference, dtype=float), np.asarray(target, dtype=float)
    assert len(r) == len(s) and len(r) and np.all(r > 0) and np.all(s > 0)
    log = np.log(s/r)
    pct = 100*(1-s/r)
    indices = np.random.default_rng(seed).integers(0, len(r), size=(bootstrap_samples, len(r)))
    # Entire paired process blocks are sampled, preserving both arms in each block.
    boot_log = log[indices].mean(axis=1)
    lo, hi = np.quantile(boot_log, [.025, .975], method='linear')
    logmean = float(log.mean())
    return {'n': len(r), 'reference_median_s': float(np.median(r)), 'target_median_s': float(np.median(s)),
        'median_ratio_reduction_pct': float(100*(1-np.median(s)/np.median(r))),
        'paired_pct': pct.tolist(), 'paired_median_pct': float(np.median(pct)),
        'paired_iqr_pct': np.quantile(pct, [.25, .75], method='linear').tolist(),
        'mean_log_target_reference': logmean, 'mean_log_ci95': [float(lo), float(hi)],
        'geometric_mean_reduction_pct': float(100*(1-math.exp(logmean))),
        'geometric_reduction_ci95_pct': [float(100*(1-math.exp(hi))), float(100*(1-math.exp(lo)))],
        'mean_paired_seconds_saved': float(np.mean(r-s)), 'positive_pairs': int(sum(pct > 0)),
        'ties': int(sum(pct == 0)), 'sign_p_two_sided': exact_sign_p(pct),
        'bootstrap_samples': bootstrap_samples, 'bootstrap_seed': seed,
        'confidence_interval_estimand': 'paired mean log target/reference ratio, monotonically transformed to percent reduction; not ratio of arm medians'}
