#!/usr/bin/env python3
"""Inventory the unchanged MGSM3 asset and evidence-backed Android account.

Usage: frame_optimization_budget.py --input <inventory.json> --output <new.json>
Paths in input are local evidence paths. No asset/build/config is modified.
Static logical arithmetic never establishes native admission or required detail.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import zipfile

import frame_optimization_quality as quality


MANIFEST_SHA256 = "007121454769ff9d816c37af1f0381f12bfea8179bb10c82b34f42cca381c816"
AGGREGATE_LIMIT = 768 * 1024 * 1024
SOURCE_RECORD_BYTES = 244
COMPONENTS = ("CPUCache", "DecodedReady", "SourceGPU", "StagingGPU", "TemporaryGPU",
              "Swapchain", "Metadata", "Builder", "Java", "Native")
COSTS = ("source", "sort", "staging", "output", "transition", "worker", "wsi", "driver")
LIMITATIONS = [
    "Full-leaf SH3 logical bytes are a lower bound for that residency, not a requirement that every leaf be simultaneously resident.",
    "Visible quality-satisfying cut and transition overlap require original same-pose runtime evidence.",
    "Native Vulkan allocation requirements cannot be reconstructed by rounding logical sizes; QueryTemporaryBytes must run on the actual device.",
    "Account reservations, allocation backing, process PSS/RSS and opaque driver/WSI costs are different overlapping observations; never sum them blindly.",
    "Manifest/page inventory is not the engine Gaussian decoder or native package/render validation.",
]


def _bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")


def _uint(value, label):
    if type(value) is not int or not 0 <= value <= (1 << 64) - 1:
        raise ValueError(label + " must be a nonnegative uint64")
    return value


def _read(path, sources, limit=32 << 20):
    path = Path(path).resolve(strict=True)
    if path.stat().st_size > limit:
        raise ValueError("input exceeds bounded read size: " + str(path))
    data = path.read_bytes()
    sources.append({"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return data


def _load(path, sources):
    def invalid(value):
        raise ValueError("invalid nonfinite JSON: " + value)
    result = json.loads(_read(path, sources), parse_constant=invalid,
                        object_pairs_hook=quality._unique_object)
    if not isinstance(result, dict):
        raise ValueError("expected JSON object")
    return result


def _verify(entry, sources):
    data = _read(entry["path"], sources)
    if sources[-1]["sha256"] != entry["sha256"] or len(data) != _uint(entry["bytes"], "identity bytes"):
        raise ValueError("evidence identity changed: " + str(entry["path"]))
    return data


class _Reader:
    """Bounded little-endian MGSM reader matching SplatSceneFormat v3 fields."""
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or self.pos + size > len(self.data):
            raise ValueError("truncated MGSM manifest")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def number(self, fmt):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))[0]

    def text(self):
        size = self.number("I")
        if size > 65536:
            raise ValueError("MGSM string limit exceeded")
        return self.take(size).decode("utf-8")

    def ids(self, limit):
        size = self.number("I")
        if size > limit:
            raise ValueError("MGSM index count exceeded")
        return [self.number("I") for _ in range(size)]


def manifest_inventory(path, sources):
    """Check exact original manifest and every unchanged page's bytes and SHA256."""
    data = _read(path, sources)
    if sources[-1]["sha256"] != MANIFEST_SHA256:
        raise ValueError("original manifest SHA256 mismatch; no regenerated scene accepted")
    if len(data) < 64 or hashlib.sha256(data[:-64]).hexdigest().encode("ascii") != data[-64:]:
        raise ValueError("MGSM internal checksum mismatch")
    reader = _Reader(data[:-64])
    if reader.number("I") != 0x4D53474D or reader.number("I") != 3 or reader.number("I") != 3:
        raise ValueError("inventory supports original MGSM3 SH3 only")
    names = ("sourceSha256", "builderVersion", "builderHash", "buildConfigHash", "buildConfig",
             "endian", "coordinates", "quaternion", "shLayout")
    result = {key: reader.text() for key in names}
    result["supportSigma"] = reader.number("f")
    root = reader.number("I")
    for key in ("sourceRecords", "leafRecords", "proxyRecords", "totalPageBytes"):
        result[key] = reader.number("Q")
    node_count, page_count = reader.number("I"), reader.number("I")
    if not 0 < node_count <= 2000000 or not 0 < page_count <= 2000000 or root >= node_count:
        raise ValueError("invalid MGSM index bounds")
    nodes = []
    for _ in range(node_count):
        reader.take(24)
        children, pages = reader.ids(8), reader.ids(page_count)
        count = reader.number("I")
        reader.take(12 + 8 + 24)
        nodes.append({"children": children, "pages": pages, "records": count})
    page_rows = []
    directory = Path(path).resolve().parent
    for index in range(page_count):
        ident, relative = reader.number("I"), reader.text()
        offset, size, decoded = reader.number("Q"), reader.number("Q"), reader.number("Q")
        count, digest, pinned = reader.number("I"), reader.text(), reader.number("I")
        portable = PurePosixPath(relative.replace("\\", "/"))
        if ident != index or portable.is_absolute() or ":" in relative or any(p in (".", "..") for p in portable.parts) or not portable.parts or pinned not in (0, 1):
            raise ValueError("invalid page ID/path/pinned flag")
        target = directory.joinpath(*portable.parts).resolve(strict=True)
        if not target.is_relative_to(directory):
            raise ValueError("page path leaves scene directory")
        if size != 24 + count * 252 or not count or size > 256 << 20:
            raise ValueError("invalid page storage metadata")
        if decoded < count * 252:
            raise ValueError("decoded byte count smaller than serialized records")
        sha = hashlib.sha256()
        with target.open("rb") as stream:
            stream.seek(offset)
            remaining = size
            header = stream.read(24)
            if len(header) != 24 or struct.unpack("<6I", header) != (0x5053474D, 1, 3, count, 252, 0):
                raise ValueError("original page header mismatch")
            sha.update(header)
            remaining -= 24
            while remaining:
                block = stream.read(min(remaining, 1024 * 1024))
                if not block:
                    raise ValueError("truncated original page")
                remaining -= len(block)
                sha.update(block)
        if sha.hexdigest() != digest:
            raise ValueError("original page SHA256 mismatch")
        row = {"id": ident, "path": str(target), "offset": offset, "bytes": size,
               "decodedBytes": decoded, "records": count, "sha256": digest, "pinned": bool(pinned)}
        page_rows.append(row)
        sources.append({"path": str(target), "offset": offset, "bytes": size, "sha256": digest})
    if reader.pos != len(reader.data):
        raise ValueError("unexpected manifest trailing payload")
    leaves = sum(n["records"] for n in nodes if not n["children"])
    proxies = sum(n["records"] for n in nodes if n["children"])
    if leaves != result["leafRecords"] or leaves != result["sourceRecords"] or proxies != result["proxyRecords"] or sum(p["bytes"] for p in page_rows) != result["totalPageBytes"]:
        raise ValueError("manifest statistics mismatch")
    result.update(nodeCount=node_count, pageCount=page_count, pages=page_rows,
                  fullLeafSH3LogicalBytes=leaves * SOURCE_RECORD_BYTES,
                  proxySH3LogicalBytes=proxies * SOURCE_RECORD_BYTES,
                  sourceBytesPerGaussian=SOURCE_RECORD_BYTES,
                  rootRecords=nodes[root]["records"])
    result.pop("buildConfig")
    return result


def native_inventory(native, config_sha256, sources, reasons):
    """Validate an audited native observation, keeping charges and costs separate."""
    if native.get("schemaVersion") != 1 or native.get("manifestSha256") != MANIFEST_SHA256 or native.get("configSha256") != config_sha256:
        raise ValueError("native evidence schema/manifest/config identity mismatch")
    if native.get("aggregateLimitBytes") != AGGREGATE_LIMIT:
        raise ValueError("native evidence changed fixed 768 MiB aggregate limit")
    raw = native.get("rawEvidence", [])
    if not isinstance(raw, list) or not raw:
        reasons.append("native observation has no raw backing evidence")
    for entry in raw:
        _verify(entry, sources)
    charges = native.get("componentCharges", {})
    if set(charges) != set(COMPONENTS):
        reasons.append("complete disjoint native component charges unavailable")
    total = sum(_uint(value, "component charge") for value in charges.values())
    reserved = native.get("reservedBytes")
    if reserved is None or _uint(reserved, "reserved bytes") != total:
        raise ValueError("native reserved bytes differ from disjoint component sum")
    peak = _uint(native.get("peakBytes"), "native peak")
    if peak < total or peak > AGGREGATE_LIMIT or total > AGGREGATE_LIMIT:
        raise ValueError("native aggregate observed limit/peak violation")
    quotas = native.get("quotas", {})
    for key in ("source", "active", "sort", "temporary", "staging", "cpuCache"):
        row = quotas.get(key)
        if not isinstance(row, dict) or not {"requested", "effective", "unit"}.issubset(row):
            reasons.append("requested/effective quota unavailable: " + key)
            continue
        _uint(row["requested"], key + " requested")
        _uint(row["effective"], key + " effective")
        if row["unit"] not in ("bytes", "records", "entries"):
            raise ValueError("invalid quota unit")
    costs = native.get("costs", {})
    for key in COSTS:
        row = costs.get(key)
        if not isinstance(row, dict):
            reasons.append("cost evidence unknown: " + key)
            continue
        if row.get("kind") not in ("actual", "estimate", "unknown", "overlapping"):
            raise ValueError("cost kind must disclose actual/estimate/unknown/overlapping")
        for field in ("logicalBytes", "requiredBytes", "backingBytes", "alignmentBytes", "estimateBytes"):
            if field in row and row[field] is not None:
                _uint(row[field], key + " " + field)
        if not isinstance(row.get("accounting"), str) or not row["accounting"]:
            raise ValueError("cost accounting overlap/attribution disclosure missing: " + key)
        if key not in ("wsi", "driver") and row["kind"] not in ("actual", "overlapping"):
            reasons.append("actual allocation/cost evidence unavailable: " + key)
        if key in ("source", "sort", "staging", "output") and any(row.get(field) is None for field in ("logicalBytes", "requiredBytes", "backingBytes", "alignmentBytes")):
            reasons.append("logical/requirements/alignment/backing evidence incomplete: " + key)
        if key == "wsi" and row["kind"] == "unknown":
            reasons.append("opaque WSI conservative account estimate unavailable")
    detail = native.get("necessaryVisibleDetail", {})
    # Legacy evidence is still inventoried, but its asserted boolean is not an
    # independent reference registration or a successful detail-use observation.
    reasons.append("legacy quality assertion is not registered-reference and successful-detail-use proof")
    if not isinstance(detail.get("evidence"), list) or not detail["evidence"]:
        reasons.append("necessary visible detail/reference evidence unavailable")
    for entry in detail.get("evidence", []):
        _verify(entry, sources)
    for key in ("sourceRecords", "transitionOverlapRecords"):
        if key not in detail:
            reasons.append("necessary visible detail count unknown: " + key)
        else:
            _uint(detail[key], key)
    if native.get("vulkanRequirementsQueried") is not True or native.get("allocationsMeasured") is not True:
        reasons.append("actual Vulkan allocation requirements/backing query unavailable")
    for key in ("deviceIdentity", "buildIdentity", "shaderIdentity", "captureIdentity"):
        if not isinstance(native.get(key), dict) or not native[key]:
            reasons.append("matching native observation identity unavailable: " + key)
    detail_logical = None
    if all(key in detail for key in ("sourceRecords", "transitionOverlapRecords")):
        detail_logical = (detail["sourceRecords"] + detail["transitionOverlapRecords"]) * SOURCE_RECORD_BYTES
        source = costs.get("source", {})
        if source.get("backingBytes") is not None and detail_logical > source["backingBytes"]:
            raise ValueError("necessary visible and additional transition source lower bound exceeds observed source backing")
        quota = quotas.get("source", {})
        capacity = quota.get("effective")
        if capacity is not None and quota.get("unit") in ("bytes", "records"):
            logical_capacity = capacity if quota["unit"] == "bytes" else capacity * SOURCE_RECORD_BYTES
            if detail_logical > logical_capacity:
                raise ValueError("necessary visible and additional transition source lower bound exceeds effective quota")
    return {"aggregateLimitBytes": AGGREGATE_LIMIT, "componentCharges": charges,
            "reservedBytes": reserved, "peakBytes": peak, "quotas": quotas, "costs": costs,
            "necessaryVisibleDetail": detail,
            "necessaryVisibleAndAdditionalTransitionSH3LogicalBytes": detail_logical,
            "processMemory": native.get("processMemory", {"status": "unknown", "reason": "PSS/RSS unavailable"}),
            "vulkanManagedMemory": native.get("vulkanManagedMemory", {"status": "unknown"})}


def _object(value):
    return value if isinstance(value, dict) else {}


def _frame_config(value):
    """Normalize Config's flat export and JNI EncodeConfig's typed envelope.

    Only the latter encodes runtime/streaming/lod containers. Duplicate facts
    must agree, including JSON types; a conflicting envelope is not evidence.
    """
    value = dict(_object(value))
    nested = "runtime" in value
    if not nested:
        # Both serializers may spell an integral float as an integer token.
        for key in ("qualityPixels", "hysteresis", "fusionRelativeRGBAError"):
            if type(value.get(key)) is int:
                value[key] = float(value[key])
        return value, False
    if not isinstance(value["runtime"], dict):
        raise ValueError("native configuration runtime must be an object")
    if "schemaVersion" in value and (type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1):
        raise ValueError("unsupported native configuration envelope schema")
    runtime = dict(value.pop("runtime"))
    value.pop("schemaVersion", None)
    for container in ("streaming", "lod"):
        fields = runtime.pop(container, {})
        if not isinstance(fields, dict):
            raise ValueError("native configuration " + container + " must be an object")
        for key, field in fields.items():
            if key in runtime and _bytes(runtime[key]) != _bytes(field):
                raise ValueError("conflicting native configuration field: " + key)
            runtime[key] = field
    # These JNI-only flags are not serialized by frame Config.
    runtime.pop("benchmark", None)
    runtime.pop("validation", None)
    for key, field in runtime.items():
        # DecodeConfig stores these settings as float, while historical JNI
        # JSON may print the shorter decimal (e.g. 0.1). Match that native
        # representation, never add an arbitrary readiness tolerance.
        if key in ("qualityPixels", "hysteresis", "fusionRelativeRGBAError") and type(field) in (int, float) and math.isfinite(field):
            field = struct.unpack("<f", struct.pack("<f", field))[0]
        if key in value and _bytes(value[key]) != _bytes(field):
            raise ValueError("conflicting native configuration field: " + key)
        value[key] = field
    return value, True


def _diagnostic_rows(document):
    """Accept existing raw transport wrappers without reading profiling strings."""
    if "ok" in document and document["ok"] is not True:
        raise ValueError("raw native diagnostic transport unsuccessful")
    payload = _object(document.get("payload", document))
    diagnostic = _object(payload.get("diagnostics", payload))
    if diagnostic.get("schemaVersion") != 2 or not isinstance(diagnostic.get("frames"), list):
        raise ValueError("raw diagnostics require schema2 frame ring")
    if len(diagnostic["frames"]) > 4096:
        raise ValueError("raw diagnostic frame bound exceeded")
    return diagnostic["frames"]


def _resource_observation(row, raw, config):
    """Retain separately scoped physical/account facts and exact native gaps."""
    gaps = []
    p0 = _object(row.get("diagnosticsP0"))
    if type(p0.get("schemaVersion")) is not int or p0.get("schemaVersion") != 1:
        return {"identity": {key: row.get(key) for key in
                ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration")},
                "rawEvidence": raw, "status": "blocked",
                "gaps": ["native diagnosticsP0 schema1 resource extension unavailable"]}
    identity = _object(p0.get("identity"))
    for key in ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration", "pipelineGeneration"):
        expected = _object(row.get("measurement")).get(key) if key == "pipelineGeneration" else row.get(key)
        if type(expected) is not int or expected <= 0 or type(identity.get(key)) is not int or identity.get(key) != expected:
            gaps.append("native frame identity unavailable/mismatched: " + key)
    if row.get("contentHash") != MANIFEST_SHA256:
        gaps.append("native original manifest identity unavailable/mismatched")
    effective, effective_nested = _frame_config(row.get("effectiveConfig"))
    supplied, supplied_nested = _frame_config(config)
    comparison_effective, comparison_supplied = dict(effective), dict(supplied)
    unavailable_across_shapes = []
    if effective_nested != supplied_nested:
        # EncodeConfig omits these two fields; configHash still binds the
        # entire configuration. Compare them when both exports contain them.
        for key in ("fusionImplementation", "fusionRelativeRGBAError"):
            if key not in effective or key not in supplied:
                unavailable_across_shapes.append(key)
                comparison_effective.pop(key, None)
                comparison_supplied.pop(key, None)
    if _bytes(comparison_effective) != _bytes(comparison_supplied):
        gaps.append("native effective configuration differs from supplied runtime configuration")
    for key in ("configHash", "shaderIdentity"):
        digest = effective.get(key)
        if not isinstance(digest, str) or len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
            gaps.append("native " + key + " identity unavailable/invalid")
    if "generation" in effective and (type(effective["generation"]) is not int or effective["generation"] != row.get("configGeneration")):
        gaps.append("native effective configuration generation unavailable/mismatched")
    if not effective.get("shaderIdentity"):
        gaps.append("native shader identity unavailable")
    if row.get("renderWidth") != 1876 or row.get("renderHeight") != 1105:
        gaps.append("native unchanged actual render dimensions unavailable/mismatched")
    if (type(effective.get("displaySHDegree")) is not int or effective["displaySHDegree"] != 3 or
            type(effective.get("shStorageMode")) is not int or effective["shStorageMode"] != 0):
        gaps.append("native SH3/FP32 identity unavailable/mismatched")
    frame_gaps = list(gaps)
    resources = _object(p0.get("resources"))
    if resources.get("status") != "observed":
        gaps.append("native resources status: " + str(resources.get("status", "unavailable")))
    if resources.get("aggregateLimitBytes") is None:
        gaps.append("native unchanged aggregate cap unavailable")
    elif resources["aggregateLimitBytes"] != AGGREGATE_LIMIT:
        raise ValueError("native diagnostic aggregate limit differs from fixed 768 MiB")
    charges = _object(resources.get("componentCharges"))
    if set(charges) != set(COMPONENTS) or any(value is None for value in charges.values()):
        gaps.append("complete disjoint component charges unavailable")
    else:
        total = sum(_uint(value, "component charge") for value in charges.values())
        reserved, peak = resources.get("reservedBytes"), resources.get("peakBytes")
        if reserved is None or peak is None:
            gaps.append("native reserved/peak observation unavailable")
        elif _uint(reserved, "reserved") != total or not total <= _uint(peak, "peak") <= AGGREGATE_LIMIT:
            raise ValueError("native disjoint charge sum/reserved/peak/cap mismatch")
    account_observed = (resources.get("status") == "observed" and
                        resources.get("aggregateLimitBytes") == AGGREGATE_LIMIT and
                        set(charges) == set(COMPONENTS) and
                        all(value is not None for value in charges.values()) and
                        resources.get("reservedBytes") is not None and resources.get("peakBytes") is not None)
    quotas = _object(resources.get("quotas"))
    for name in ("source", "active", "sort", "temporary", "staging", "cpuCache"):
        quota = _object(quotas.get(name))
        for field in ("requested", "effective"):
            if quota.get(field) is None:
                gaps.append("quota " + name + " " + field + " unavailable")
            else:
                _uint(quota[field], "quota " + name + " " + field)
        if quota.get("unit") not in ("bytes", "records", "entries"):
            gaps.append("quota " + name + " unit unavailable/unsupported")
    costs = _object(resources.get("costs"))
    for name in COSTS + ("temporary",):
        cost = _object(costs.get(name))
        kind = cost.get("kind", "unknown")
        if kind not in ("actual", "estimate", "unknown", "overlapping"):
            raise ValueError("unsupported native cost kind: " + name)
        for field in ("logicalBytes", "requiredBytes", "backingBytes", "alignmentBytes", "estimateBytes"):
            if cost.get(field) is not None:
                _uint(cost[field], name + " " + field)
        if not isinstance(cost.get("accounting"), str) or not cost["accounting"]:
            gaps.append("cost accounting/overlap disclosure unavailable: " + name)
        if name in ("source", "sort", "staging", "output", "temporary"):
            if kind not in ("actual", "overlapping") or any(cost.get(field) is None for field in
                    ("logicalBytes", "requiredBytes", "backingBytes", "alignmentBytes")):
                gaps.append("actual Vulkan logical/requirements/backing/alignment unavailable: " + name)
        if name == "wsi" and (kind != "estimate" or cost.get("estimateBytes") is None):
            gaps.append("opaque WSI conservative account estimate unavailable")
        if name == "driver" and cost.get("backingBytes") is None:
            gaps.append("opaque driver physical backing unavailable")
        if name in ("transition", "worker") and kind == "unknown":
            gaps.append("native overlap/working-set cost unavailable: " + name)
    admission = _object(resources.get("admission"))
    if admission.get("status") not in ("admitted", "refused", "unknown"):
        raise ValueError("unsupported native admission status")
    for field in ("requestedBytes", "availableBytes"):
        if admission.get(field) is not None:
            _uint(admission[field], "admission " + field)
    if admission.get("status") != "admitted":
        gaps.append("last native reservation " + str(admission.get("status")) + ": " +
                    json.dumps(admission.get("reasonCodes", []), sort_keys=True))
    cut = _object(resources.get("cut"))
    physical_gaps = list(frame_gaps)
    if _object(row.get("status")).get("ok") is not True:
        physical_gaps.append("successful compute submission unavailable")
    if cut.get("rootReady") is not True or cut.get("loading") is not False or type(cut.get("requestPages")) is not int or cut["requestPages"] != 0:
        physical_gaps.append("selected detail loading incomplete/unknown")
    pages, used = cut.get("selectedPageCount"), cut.get("usedSelectedPages")
    if type(pages) is not int or pages <= 0 or type(used) is not int or used != pages:
        physical_gaps.append("all selected pages successful consumption unavailable")
    if type(row.get("submissionToken")) is not int or row["submissionToken"] <= 0 or type(cut.get("consumptionSubmissionToken")) is not int or cut.get("consumptionSubmissionToken") != row["submissionToken"]:
        physical_gaps.append("selected cut successful consuming token unavailable")
    if cut.get("transitionAdmitted") is not True:
        physical_gaps.append("necessary transition admission unavailable")
    threshold = effective.get("qualityPixels")
    error = cut.get("maxSelectedProxyErrorPixels")
    if (type(threshold) not in (int, float) or not math.isfinite(threshold) or threshold <= 0 or
            type(error) not in (int, float) or not math.isfinite(error) or not 0 <= error <= threshold):
        physical_gaps.append("requested detail error not satisfied/unknown")
    for name in ("selectionVersion", "residencyVersion", "activeCount"):
        if type(cut.get(name)) is not int or cut[name] <= 0:
            physical_gaps.append("selected cut identity/count unavailable: " + name)
    gaps.extend(gap for gap in physical_gaps if gap not in gaps)
    # Native qualitySatisfied is deliberately null. Neither the selected error,
    # reservation result nor a handwritten boolean certifies original pixels.
    quality_gap = "independent registered-reference/policy/metrics-to-native-frame linkage not supplied to diagnostic inventory; use qualityReceipt"
    gaps.append(quality_gap)
    if row.get("buildIdentity") is None:
        gaps.append("native build identity unavailable; genuine external APK/ELF/source/Shader capture binding required")
    return {"identity": identity, "contentHash": row.get("contentHash"),
            "configHash": effective.get("configHash"), "shaderIdentity": effective.get("shaderIdentity"),
            "configurationEvidence": {
                "effectiveShape": "nested_jni" if effective_nested else "flat_frame",
                "suppliedShape": "nested_jni" if supplied_nested else "flat_frame",
                "unavailableAcrossShapes": unavailable_across_shapes,
                "effectiveQualityPixels": effective.get("qualityPixels"),
                "effectiveRawQualityPixels": (_object(_object(_object(row.get("effectiveConfig")).get("runtime")).get("lod")).get("qualityPixels")
                    if effective_nested else _object(row.get("effectiveConfig")).get("qualityPixels")),
                "suppliedQualityPixels": supplied.get("qualityPixels"),
                "suppliedRawQualityPixels": (_object(_object(config.get("runtime")).get("lod")).get("qualityPixels")
                    if supplied_nested else config.get("qualityPixels")),
                "comparison": "exact typed JSON after explicit native float32 envelope conversion; configHash and shaderIdentity retained"},
            "nativeBuildIdentity": row.get("buildIdentity"), "rawEvidence": raw,
            "status": "blocked", "gaps": gaps, "accountStatus": "observed" if account_observed else "blocked",
            "reservationObservationStatus": admission.get("status"),
            "account": {key: resources.get(key) for key in
                        ("aggregateLimitBytes", "componentCharges", "reservedBytes", "peakBytes")},
            "quotas": quotas, "costs": costs, "admission": admission, "cut": cut,
            "physicalCutReadiness": {"status": "blocked" if physical_gaps else "observed",
                                     "gaps": physical_gaps},
            "qualitySatisfyingAdmission": {"status": "blocked", "gaps": [quality_gap],
                "requiredConsumerInput": "qualityReceipt",
                "requiredNextEvidence": "Supply qualityReceipt identities for the fixed freeze, separate candidate, complete comparison report and exact native screenshot observation. The consumer independently recomputes pixels and binds the actual consumed cut/account. Native qualitySatisfied remains null."},
            "vulkanManagedMemory": resources.get("vulkanManagedMemory"),
            "processMemory": {"status": "unknown", "pssBytes": None, "rssBytes": None,
                              "reason": "not supplied by native resource extension; measure separately"},
            "detail": p0.get("detail"),
            "accounting": "Charges are disjoint. Costs, pooled backing, overlap, WSI estimate and process PSS/RSS are independent, non-additive views."}


def diagnostic_inventory(entries, runtime_config, sources):
    """Consume identity-verified raw chunks; preserve overlapping provenance."""
    if not isinstance(entries, list) or not entries or len(entries) > 256:
        raise ValueError("diagnosticEvidence requires 1..256 path/bytes/sha256 identities")
    observations, by_identity, legacy_accounts = [], {}, []
    total_frames, missing_extension = 0, 0
    for entry in entries:
        data = _verify(entry, sources)
        raw = dict(sources[-1])
        document = json.loads(data, parse_constant=lambda value: (_ for _ in ()).throw(ValueError("nonfinite JSON: " + value)))
        if not isinstance(document, dict):
            raise ValueError("raw diagnostic evidence must be an object")
        for row in _diagnostic_rows(document):
            if not isinstance(row, dict):
                raise ValueError("raw diagnostic frame must be an object")
            total_frames += 1
            if "diagnosticsP0" not in row:
                missing_extension += 1
                # Historical rows do expose an account and managed-memory
                # observations. Preserve them without manufacturing quotas,
                # queried allocation requirements or admission facts.
                legacy_accounts.append({"rawEvidence": raw,
                    "identity": {name: row.get(name) for name in
                                 ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration")},
                    "contentHash": row.get("contentHash"), "account": row.get("budget"),
                    "vulkanManagedMemory": {name: row.get(name) for name in
                        ("vulkanManagedLiveBytes", "vulkanManagedBackingBytes", "vulkanPooledUnusedBytes")},
                    "status": "legacy_partial_no_native_resource_extension"})
                if len(legacy_accounts) > 16384:
                    raise ValueError("legacy account observation bound exceeded")
                continue
            # Overlapping rings may resolve delayed GPU fields. Deduplicate the
            # resource extension only; keep every raw source proving that view.
            key = json.dumps([row.get("contentHash"), row.get("effectiveConfig"),
                              [row.get(name) for name in ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration")],
                              _object(row.get("measurement")).get("pipelineGeneration"),
                              row.get("diagnosticsP0")], sort_keys=True, allow_nan=False)
            if key in by_identity:
                previous = observations[by_identity[key]]["rawEvidence"]
                if raw not in previous:
                    previous.append(raw)
                continue
            if len(observations) >= 16384:
                raise ValueError("distinct native resource observation bound exceeded")
            observation = _resource_observation(row, [raw], runtime_config)
            by_identity[key] = len(observations)
            observations.append(observation)
    gaps = sorted({gap for item in observations for gap in item["gaps"]})
    if missing_extension:
        gaps.append("native diagnosticsP0 unavailable in " + str(missing_extension) + " retained raw frame observations (overlaps included)")
    if not observations:
        gaps.append("no native schema1 resource extension observations; acquire bounded matching-build diagnostics")
    return {"status": "blocked", "analysisStatus": "complete", "qualityAdmissionStatus": "blocked",
            "unsupportedStages": [], "requiredConsumerInput": "qualityReceipt",
            "observations": observations,
            "legacyAccountObservations": legacy_accounts, "gaps": gaps,
            "coverage": {"rawFrameObservations": total_frames, "missingExtensionObservations": missing_extension,
                         "distinctResourceObservations": len(observations)},
            "runtimeAdmissionScope": "Last aggregate reservation operation only; physical cut readiness and independent quality are separate claims.",
            "impossibilityEstablished": False}


def quality_cut_receipt(request, sources):
    """Bind recomputed registered pixels to their exact successful native cut.

    Admission here certifies the observed cut under the recorded aggregate
    account only. Unknown physical driver/WSI/process allocation stays unknown.
    """
    if not isinstance(request, dict) or set(request) != {"candidate", "freeze", "comparison", "nativeObservation"}:
        raise ValueError("qualityReceipt requires exactly candidate/freeze/comparison/nativeObservation file identities")
    supplied = quality._document(request["comparison"], sources)
    recomputed = quality.comparison_report(request["candidate"], request["freeze"])
    # Compare the entire report, not only metric/pass fields. This also checks
    # source paths, ordering, receipt provenance and the comparison-tool digest.
    if _bytes(supplied) != _bytes(recomputed):
        raise ValueError("comparison report differs from independent full raw-pixel recomputation")
    sources.extend(recomputed["sourceFiles"])
    candidate = quality._document(request["candidate"], sources)
    frozen = quality._document(request["freeze"], sources)
    pose = frozen["policy"]["poses"][0]
    entry = candidate["poses"][0]
    metadata = quality._document(entry["metadata"], sources)
    evidence = metadata["frameObservation"]
    if request["nativeObservation"] != evidence["screenshotMetadata"]:
        raise ValueError("requested native observation differs from candidate screenshot evidence")
    screenshot = quality._document(request["nativeObservation"], sources)
    diagnostic = quality._document(evidence["diagnostics"], sources)
    rows = _diagnostic_rows(diagnostic)
    row = quality.select_screenshot_frame(screenshot, rows)
    p0 = _object(row.get("diagnosticsP0"))
    resources = _object(p0.get("resources"))
    cut = _object(resources.get("cut"))
    gaps = []
    # A successful compare may measure a loading image. Keep those true pixel
    # results while refusing admission for an incomplete native consumed cut.
    readiness = quality.reference_readiness(row, frozen["policy"]["qualitySettings"]["lodErrorThreshold"])
    gaps.extend(readiness["reasons"])
    for key in ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration", "pipelineGeneration"):
        actual = _object(row.get("measurement")).get(key) if key == "pipelineGeneration" else row.get(key)
        if type(actual) is not int or actual <= 0 or _object(p0.get("identity")).get(key) != actual:
            gaps.append("native exact frame identity unavailable/mismatched: " + key)
    if "qualitySatisfied" not in cut or cut["qualitySatisfied"] is not None:
        raise ValueError("native qualitySatisfied must remain explicitly null")
    if cut.get("consumedCutHashAlgorithm") != "sha256_le64_selected_node_page_slot_v1":
        gaps.append("actual consumed cut hash algorithm unavailable/unsupported")
    if not quality._hex(cut.get("consumedCutHash")):
        gaps.append("actual selected node/page/slot consumed cut fingerprint unavailable")
    for key, native_key in (("frameId", "consumedCutFrameId"), ("submissionToken", "consumedCutSubmissionToken")):
        if type(cut.get(native_key)) is not int or cut[native_key] != row.get(key):
            gaps.append("actual consumed cut screenshot identity unavailable/mismatched: " + native_key)
    if row.get("contentHash") != MANIFEST_SHA256:
        raise ValueError("native cut original manifest differs")
    if resources.get("aggregateLimitBytes") != AGGREGATE_LIMIT:
        raise ValueError("native cut aggregate cap differs from fixed 768 MiB")
    charges = _object(resources.get("componentCharges"))
    account_observed = False
    if set(charges) != set(COMPONENTS) or any(value is None for value in charges.values()):
        gaps.append("complete native account component charges unavailable")
    else:
        total = sum(_uint(v, "native account component") for v in charges.values())
        reserved, peak = resources.get("reservedBytes"), resources.get("peakBytes")
        if reserved is None or peak is None:
            gaps.append("native account reserved/peak unavailable")
        elif total != _uint(reserved, "native account reserved") or not total <= _uint(peak, "native account peak") <= AGGREGATE_LIMIT:
            raise ValueError("native cut account charges/reserved/peak/cap mismatch")
        else:
            account_observed = resources.get("status") == "observed"
    admission = _object(resources.get("admission"))
    if type(admission.get("sequence")) is not int or admission["sequence"] <= 0:
        gaps.append("exact-row aggregate admission account sequence unavailable")
    if not account_observed:
        gaps.append("same-frame native account not fully observed")
    account = {key: resources.get(key) for key in
               ("status", "aggregateLimitBytes", "componentCharges", "reservedBytes", "peakBytes", "admission")}
    account_hash = hashlib.sha256(_bytes(account)).hexdigest()
    binding = {"identity": _object(p0.get("identity")), "contentHash": row.get("contentHash"),
               "configHash": _object(row.get("effectiveConfig")).get("configHash"),
               "shaderIdentity": _object(row.get("effectiveConfig")).get("shaderIdentity"),
               "consumedCutHash": cut.get("consumedCutHash"), "cut": cut,
               "accountSha256": account_hash, "account": account,
               "nativeObservation": request["nativeObservation"],
               "candidateImage": entry["image"], "candidateMetadata": entry["metadata"],
               "comparison": request["comparison"], "freeze": request["freeze"]}
    pixel_status = recomputed["status"]
    admission_status = "blocked" if gaps else pixel_status
    reasons = list(gaps)
    if pixel_status == "fail":
        reasons.append("observed full-image or fixed-ROI pixel thresholds failed")
    return {"status": admission_status, "qualityStatus": pixel_status, "reasons": reasons,
            "comparisonVerification": {"status": "verified", "method": "complete deterministic report and raw pixel recomputation",
                                       "result": recomputed},
            "qualitySatisfyingAdmission": {"status": admission_status, "gaps": gaps,
                                           "binding": binding,
                                           "scope": "single registered stationary-end successful consumed cut and exact-row charged account"},
            "physicalCutReadiness": readiness,
            "chargedAccount": {"status": "observed" if account_observed else "blocked",
                               "withinAggregateLimit": True if account_observed else None,
                               "value": account, "sha256": account_hash},
            "resourceFeasibility": {"status": "unknown", "impossibilityEstablished": False,
                "reason": "Observed charged account is not full physical driver/WSI/process allocation or all-view cut feasibility.",
                "logicalSourceLowerBoundBytes": cut.get("activeCount", 0) * SOURCE_RECORD_BYTES
                    if type(cut.get("activeCount")) is int and cut["activeCount"] > 0 else None,
                "logicalSourceLowerBoundScope": "active consumed Gaussian records only; excludes transition overlap and other resources",
                "vulkanManagedMemory": resources.get("vulkanManagedMemory"),
                "costs": resources.get("costs"), "quotas": resources.get("quotas")},
            "impossibilityEstablished": False}


def inventory(config, report):
    """Retain verified asset inventory even when native/detail evidence is missing."""
    sources, reasons = report["sourceFiles"], report["reasons"]
    if config.get("schemaVersion") != 1:
        raise ValueError("unknown inventory input schema")
    if "qualityReceipt" in config:
        report.update(quality_cut_receipt(config["qualityReceipt"], sources))
        return
    report["asset"] = manifest_inventory(config["manifestPath"], sources)
    if not config.get("configPath"):
        reasons.append("actual runtime configuration unavailable")
        return
    runtime_config = _load(config["configPath"], sources)
    config_sha = sources[-1]["sha256"]
    report["runtimeConfig"] = {"sha256": config_sha, "value": runtime_config}
    if "diagnosticEvidence" in config:
        report["nativeDiagnostics"] = diagnostic_inventory(config["diagnosticEvidence"], runtime_config, sources)
        reasons.extend(report["nativeDiagnostics"]["gaps"])
        report["qualityStatus"] = "blocked"
        if config.get("nativeEvidencePath"):
            native = _load(config["nativeEvidencePath"], sources)
            report["native"] = native_inventory(native, config_sha, sources, reasons)
        return
    if not config.get("nativeEvidencePath"):
        reasons.append("actual native quota/allocation/admission evidence unavailable")
        return
    native = _load(config["nativeEvidencePath"], sources)
    report["native"] = native_inventory(native, config_sha, sources, reasons)
    if not reasons:
        report["status"] = "pass"
    report["runtimeAdmissionScope"] = "Only the supplied observed quality-satisfying cut/transition scope; no global all-view proof."


def main(argv=None):
    """Emit exclusive-create, identity-bearing pass/fail/blocked inventory JSON."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    report = {"schemaVersion": 1, "status": "blocked", "reasons": [], "sourceFiles": [],
              "aggregateLimitBytes": AGGREGATE_LIMIT, "limitations": LIMITATIONS}
    try:
        inventory(_load(args.input, report["sourceFiles"]), report)
    except (FileNotFoundError, quality.EvidenceUnavailable) as error:
        report["reasons"].append(str(error))
    except (OSError, ValueError, KeyError, TypeError, AttributeError, struct.error, OverflowError, zipfile.BadZipFile) as error:
        report.update(status="fail")
        report["reasons"].append(str(error))
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(_bytes(report))
            stream.flush()
            os.fsync(stream.fileno())
    except (OSError, ValueError) as error:
        print("cannot create output (overwrite forbidden): " + str(error), file=sys.stderr)
        return 2
    print(json.dumps({"status": report["status"], "output": str(args.output), "reasons": report["reasons"]}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
