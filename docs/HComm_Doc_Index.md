# HComm Documentation Index (Iteration Package)

> Scope: HComm Route A integration, offline export bundle, NoSendPerf validation path  
> Repo: `E:/source/tracy`  
> Branch baseline: `tracy-hcomm-0-13-1`

---

## 1) Iteration Overview

This index organizes the current HComm documentation set as an iteration-level package aligned to:
- **SRS** (requirements and goals)
- **SSD** (architecture/design decisions)
- **STC** (test/validation and acceptance evidence)

Use this file as the single entry point when archiving, handoff, or onboarding external integrators.

---

## 2) SRS (需求) Documents

### 2.1 Primary Requirements / Scope
1. **[`docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md`](./HComm_SRSSD_Refactor_Archive_2026-05-17.md)**  
   - Role: Iteration archive, requirement framing, scope boundaries, risks, roadmap.  
   - Includes: Background/goals, constraints, module views, sequence flows, acceptance criteria, backlog.

### 2.2 Integration Requirement Notes
2. **`docs/TracyNoSendPerf_Cpp14PlusCpp17_So_Integration.md`**  
   - Role: Practical requirement profile for mixed-standard integration (`C++14 body + single TU C++17`).  
   - Includes: Preconditions, minimal CMake contract, compatibility caveats, integration sequence.

---

## 3) SSD (设计) Documents

### 3.1 HComm Bundle Design and Host Wiring
3. **[`tracy_hcom/README.md`](../tracy_hcom/README.md)**  
   - Role: Route A design contract for external consumers.  
   - Includes: `tracyhcom_embed()` usage, required include paths/defines, direct-integration example targets, phase-two backlog.

### 3.2 Export Packaging Design (Offline Reuse)
4. **`scripts/tracy_hcom_export_bundle.py`**  
   - Role: Packaging design implementation for offline bundle export.  
   - Includes: transitive dependency extraction, standardized manifest metadata, integration snippet generation, optional verify flow.

---

## 4) STC (测试/验证) Documents

### 4.1 Investigation and Problem Evidence
5. **`docs/TracyNoSendPerfInvestigation.md`**  
   - Role: Investigation log and technical evidence for NoSendPerf-related behavior.

### 4.2 Example-level Export/Test Guidance
6. **`examples/TracyLite_ExportConfig_README.md`**  
   - Role: Example configuration and runtime/export behavior reference.

---

## 5) Traceability Matrix (SRS → SSD → STC)

| Requirement Theme | SRS Source | SSD Source | STC / Evidence |
|---|---|---|---|
| Route A direct source integration | `docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md` | `tracy_hcom/README.md` | `examples/NoSendPerf` targets build results + archive validation notes |
| Perfetto optional enablement | `docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md` | `tracy_hcom/README.md`, `scripts/tracy_hcom_export_bundle.py` | `TracyHcomRunner`, export/chunk checks in NoSendPerf flow |
| Offline external reuse | `docs/HComm_SRSSD_Refactor_Archive_2026-05-17.md` | `scripts/tracy_hcom_export_bundle.py` | Export manifest (`manifest.txt`) + verify mode outputs |
| Mixed C++ standard compatibility | `docs/TracyNoSendPerf_Cpp14PlusCpp17_So_Integration.md` | CMake snippets in same doc + `examples/NoSendPerf/CMakeLists.txt` | Investigation and build validation in docs/archive |

---

## 6) Suggested Iteration Deliverable Layout

For future iterations, keep this structure stable:

- `docs/HComm_Doc_Index.md` (this index)
- `docs/HComm_SRSSD_Refactor_<date>.md` (iteration archive)
- `docs/HComm_STC_<date>.md` (optional dedicated test completion report)
- `tracy_hcom/README.md` (current integration contract)
- `scripts/tracy_hcom_export_bundle.py` (packaging implementation contract)

---

## 7) Owner / Update Rules

- Update this index whenever:
  1. A new iteration archive is added;
  2. The integration contract (`tracy_hcom/README.md`) changes;
  3. Export/packaging behavior in `tracy_hcom_export_bundle.py` changes.
- Keep links and role descriptions synchronized with actual implementation to avoid documentation drift.

---

## 8) External Repo Integration Link Slot

- SampleRepoURL: `<to be filled>`
- SampleBranchOrTag: `<to be filled>`
- SampleCommit: `<to be filled>`
- IntegrationMode: `RouteA / tracyhcom_embed()`
- VerifiedMode: `core | perfetto`
- VerifiedOn: `Windows(x64 MSVC) | Linux | WSL2`
- Notes: `<integration deltas / 3rd-party dependency notes>`
