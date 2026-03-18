"""
NutriCalc Solver — SVD-based nutrient formula solver + LMCv2 EC estimator.
Mirrors HydroBuddy's core algorithm, implemented in Python/NumPy.
"""

import numpy as np
from dataclasses import dataclass, field
from typing import Optional

ELEMENTS = ["N_NO3", "N_NH4", "P", "K", "Ca", "Mg", "S",
            "Fe", "Mn", "Zn", "Cu", "B", "Mo", "Ni", "Na", "Si", "Cl"]

# ── Limiting molar conductivities (S·cm²/mol) at 25°C ──────────────────────
# Source: CRC Handbook; used in LMCv2 EC model.
LMC = {
    "NO3":  71.4,  "NH4": 73.5, "H2PO4": 36.0, "HPO4": 57.0,
    "K":    73.5,  "Ca":  59.5, "Mg":    53.1,  "SO4":  80.0,
    "Fe2":  54.0,  "Fe3": 68.0, "Mn":    53.5,  "Zn":   52.8,
    "Cu":   53.6,  "Na":  50.1, "Cl":    76.3,  "B":     0.0,
    "Mo":   49.0,  "Si":   0.0
}

# Molecular weights for element→ion conversion (g/mol)
MW = {
    "N": 14.007, "P": 30.974, "K": 39.098, "Ca": 40.078,
    "Mg": 24.305, "S": 32.06,  "Fe": 55.845, "Mn": 54.938,
    "Zn": 65.38,  "Cu": 63.546, "B": 10.811, "Mo": 95.96,
    "Na": 22.990, "Si": 28.086, "Cl": 35.453, "Ni": 58.693
}


@dataclass
class SolveResult:
    success: bool
    weights_g_per_L: dict          # compound_id → grams per litre
    weights_g_per_batch: dict      # compound_id → grams per batch volume
    achieved_ppm: dict             # element → ppm achieved
    target_ppm: dict               # element → ppm requested
    deviation_pct: dict            # element → % deviation from target
    residual_rms: float            # root-mean-square error across all elements
    ec_estimated: float            # mS/cm
    cost_per_batch: float          # currency units
    cost_complete: bool            # False if any used compound has no price set
    volume_L: float
    ab_split: Optional[dict] = None  # {"A": [...ids], "B": [...ids]}
    warnings: list = field(default_factory=list)
    error: Optional[str] = None


def solve_formula(targets: dict, compounds: list, volume_L: float = 100.0) -> SolveResult:
    """
    Solve for compound weights that best achieve the target nutrient profile.

    Args:
        targets:   {element: ppm_mg_per_L}  — zeros are treated as unconstrained
        compounds: list of compound dicts from chemicals.json (selected by user)
        volume_L:  final solution volume in litres

    Returns:
        SolveResult with weights, deviations, cost, EC estimate
    """
    if not compounds:
        return SolveResult(success=False, weights_g_per_L={}, weights_g_per_batch={},
                           achieved_ppm={}, target_ppm=targets, deviation_pct={},
                           residual_rms=0, ec_estimated=0, cost_per_batch=0, cost_complete=False,
                           volume_L=volume_L, error="No compounds selected.")

    # Only solve for elements that have non-zero targets
    active_elements = [e for e in ELEMENTS if targets.get(e, 0) > 0]
    if not active_elements:
        return SolveResult(success=False, weights_g_per_L={}, weights_g_per_batch={},
                           achieved_ppm={}, target_ppm=targets, deviation_pct={},
                           residual_rms=0, ec_estimated=0, cost_per_batch=0, cost_complete=False,
                           volume_L=volume_L, error="All targets are zero — set at least one nutrient target.")

    n_elements = len(active_elements)
    n_compounds = len(compounds)

    # Build composition matrix A [n_elements × n_compounds]
    # A[i][j] = mg of element i per gram of compound j
    # (elemental % / 100 × purity% / 100 × 1000 mg/g)
    A = np.zeros((n_elements, n_compounds))
    for j, cmpd in enumerate(compounds):
        purity = cmpd.get("purity", 99.0) / 100.0
        for i, elem in enumerate(active_elements):
            elem_pct = cmpd.get("elements", {}).get(elem, 0.0)
            A[i, j] = (elem_pct / 100.0) * purity * 1000.0  # mg/g

    # Target vector b [n_elements] — ppm = mg/L, so b is directly in mg/L
    b = np.array([targets.get(e, 0.0) for e in active_elements])

    # Solve using least-squares (SVD internally via numpy)
    # x = grams per litre of solution
    x, residuals, rank, sv = np.linalg.lstsq(A, b, rcond=None)

    # Clamp negatives to zero (can't add negative amounts)
    warnings = []
    if np.any(x < -0.001):
        warnings.append(
            f"Solver requested negative amounts for: "
            f"{[compounds[j]['name'] for j in range(n_compounds) if x[j] < 0]}. "
            f"Clamped to zero — consider removing those compounds or adjusting targets."
        )
    x = np.clip(x, 0, None)

    # Calculate achieved concentrations
    achieved_vec = A @ x  # mg/L per element
    achieved_ppm = {e: float(achieved_vec[i]) for i, e in enumerate(active_elements)}
    # Fill zeros for elements not in active set
    for e in ELEMENTS:
        if e not in achieved_ppm:
            achieved_ppm[e] = 0.0

    # Deviation per element
    deviation_pct = {}
    for i, e in enumerate(active_elements):
        t = targets.get(e, 0.0)
        a = achieved_ppm[e]
        if t > 0:
            deviation_pct[e] = ((a - t) / t) * 100.0
        else:
            deviation_pct[e] = 0.0

    # RMS residual
    residual_rms = float(np.sqrt(np.mean((achieved_vec - b) ** 2)))

    # Weights per compound
    weights_g_per_L = {cmpd["id"]: float(x[j]) for j, cmpd in enumerate(compounds)}
    weights_g_per_batch = {cid: w * volume_L for cid, w in weights_g_per_L.items()}

    # Cost — only include compounds that are actually used AND have a price
    used_compounds = [c for c in compounds if weights_g_per_batch.get(c["id"], 0) >= 0.001]
    cost_per_batch = sum(
        weights_g_per_batch[c["id"]] / 1000.0 * c.get("cost_per_kg", 0.0)
        for c in used_compounds
    )
    cost_complete = all(c.get("cost_per_kg", 0) > 0 for c in used_compounds)

    # EC estimate
    ec = estimate_ec(achieved_ppm)

    # A/B split analysis
    ab = _split_ab(compounds, weights_g_per_batch)

    return SolveResult(
        success=True,
        weights_g_per_L=weights_g_per_L,
        weights_g_per_batch=weights_g_per_batch,
        achieved_ppm=achieved_ppm,
        target_ppm=targets,
        deviation_pct=deviation_pct,
        residual_rms=residual_rms,
        ec_estimated=ec,
        cost_per_batch=cost_per_batch,
        cost_complete=cost_complete,
        volume_L=volume_L,
        ab_split=ab,
        warnings=warnings
    )


def estimate_ec(ppm: dict) -> float:
    """
    LMCv2 EC estimator — ion-specific conductivity corrections based on ionic strength.
    Returns estimated EC in mS/cm at 25°C.

    Each ion's contribution is reduced based on:
    - Its charge (higher charge → more reduction at elevated concentration)
    - Overall ionic strength (I = 0.5 × Σ(c × z²))
    """
    # Convert ppm (mg/L) → mmol/L for each ion
    ions = {}  # ion_key → {mmol, charge, lmc}

    def add_ion(key, mmol_L, charge, lmc_val):
        if mmol_L > 0:
            ions[key] = {"mmol": mmol_L, "charge": abs(charge), "lmc": lmc_val}

    # Nitrate
    no3_mmol = ppm.get("N_NO3", 0) / MW["N"]
    add_ion("NO3", no3_mmol, -1, LMC["NO3"])

    # Ammonium
    nh4_mmol = ppm.get("N_NH4", 0) / MW["N"]
    add_ion("NH4", nh4_mmol, +1, LMC["NH4"])

    # Phosphate — simplify to H2PO4 at typical hydro pH 5.5-6.5
    p_mmol = ppm.get("P", 0) / MW["P"]
    add_ion("H2PO4", p_mmol, -1, LMC["H2PO4"])

    # Potassium
    k_mmol = ppm.get("K", 0) / MW["K"]
    add_ion("K", k_mmol, +1, LMC["K"])

    # Calcium (divalent)
    ca_mmol = ppm.get("Ca", 0) / MW["Ca"]
    add_ion("Ca", ca_mmol, +2, LMC["Ca"])

    # Magnesium (divalent)
    mg_mmol = ppm.get("Mg", 0) / MW["Mg"]
    add_ion("Mg", mg_mmol, +2, LMC["Mg"])

    # Sulfate (divalent)
    s_mmol = ppm.get("S", 0) / MW["S"]
    add_ion("SO4", s_mmol, -2, LMC["SO4"])

    # Micronutrients (small contribution but included for accuracy)
    add_ion("Fe2",  ppm.get("Fe", 0) / MW["Fe"], +2, LMC["Fe2"])
    add_ion("Mn",   ppm.get("Mn", 0) / MW["Mn"], +2, LMC["Mn"])
    add_ion("Zn",   ppm.get("Zn", 0) / MW["Zn"], +2, LMC["Zn"])
    add_ion("Cu",   ppm.get("Cu", 0) / MW["Cu"], +2, LMC["Cu"])
    add_ion("Ni",   ppm.get("Ni", 0) / MW.get("Ni", 58.693), +2, 54.0)  # Ni²⁺, LMC ≈ 54
    add_ion("Na",   ppm.get("Na", 0) / MW["Na"], +1, LMC["Na"])
    add_ion("Cl",   ppm.get("Cl", 0) / MW["Cl"], -1, LMC["Cl"])

    if not ions:
        return 0.0

    # Ionic strength I = 0.5 × Σ(c_i × z_i²), units mol/L
    ionic_strength = 0.5 * sum(
        (v["mmol"] / 1000.0) * (v["charge"] ** 2)
        for v in ions.values()
    )

    # LMCv2: per-ion correction factor based on charge and ionic strength
    # Correction follows: f(z, I) = 1 / (1 + k_z × sqrt(I))
    # k_z = 1.0 for monovalent, 2.0 for divalent (empirical from HydroBuddy LMCv2)
    def correction(charge, I):
        k = 1.0 if charge == 1 else 2.0
        return 1.0 / (1.0 + k * np.sqrt(I))

    # EC = Σ(lmc_i × c_i × correction_i) × unit_conversion
    # lmc in S·cm²/mol, c in mmol/L
    # Derivation: S·cm²/mol × mmol/L × (10⁻³ mol/mmol) / (10³ cm³/L) = 10⁻⁶ S/cm = 10⁻³ mS/cm
    # → multiply by 0.001
    ec_mS_cm = sum(
        v["lmc"] * v["mmol"] * correction(v["charge"], ionic_strength) * 0.001
        for v in ions.values()
    )

    return round(ec_mS_cm, 3)


def _split_ab(compounds: list, weights_g_per_batch: dict) -> dict:
    """
    Assign compounds to A or B stock solution based on precipitation incompatibilities.

    Rules (same as HydroBuddy):
      - Ca²⁺ sources → Part A
      - SO₄²⁻ sources → Part B  (Ca + SO4 → CaSO4 precipitate)
      - PO₄³⁻ sources → Part B  (Ca + PO4 → Ca3PO4 precipitate)
      - Fe chelates    → Part A  (Fe + PO4 → FePO4 if both in B)
      - Everything else → Part B by default
    """
    ca_ids = {c["id"] for c in compounds if c.get("elements", {}).get("Ca", 0) > 0}
    so4_ids = {c["id"] for c in compounds if c.get("elements", {}).get("S", 0) > 0}
    p_ids = {c["id"] for c in compounds if c.get("elements", {}).get("P", 0) > 0}
    fe_ids = {c["id"] for c in compounds
              if c.get("elements", {}).get("Fe", 0) > 0 and "EDTA" in c.get("formula", "") or
              "DTPA" in c.get("formula", "") or "EDDHA" in c.get("formula", "")}

    part_a = []
    part_b = []

    for c in compounds:
        cid = c["id"]
        w = weights_g_per_batch.get(cid, 0)
        if w < 0.001:
            continue  # skip zero-weight compounds

        entry = {"id": cid, "name": c["name"], "grams": round(w, 2)}

        if cid in ca_ids or cid in fe_ids:
            part_a.append(entry)
        else:
            part_b.append(entry)

    return {"A": part_a, "B": part_b}


def reverse_solve(weights_g_per_L: dict, compounds: list) -> dict:
    """
    Given compound weights, calculate resulting nutrient concentrations.
    (HydroBuddy Mode 2 — direct weight entry)
    """
    result = {e: 0.0 for e in ELEMENTS}
    for cmpd in compounds:
        cid = cmpd["id"]
        w = weights_g_per_L.get(cid, 0.0)
        purity = cmpd.get("purity", 99.0) / 100.0
        for elem, pct in cmpd.get("elements", {}).items():
            result[elem] = result.get(elem, 0.0) + w * (pct / 100.0) * purity * 1000.0
    return result
