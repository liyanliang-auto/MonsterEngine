"""Preregistered independent analytic Gaussian fixtures for real GPU validation.

These are validation assets, not an alternative GPU renderer. Coordinates use
integer pixel centers, top-left images and a right-handed -Z view direction.
"""

import hashlib
import json
import math
import os
from pathlib import Path

import numpy as np
from PIL import Image


def _write(path, data):
    with Path(path).open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def sh_basis(direction, degree):
    """Real SH basis in standard 3DGS ordering, through degree three."""
    d = np.asarray(direction, dtype=np.float64)
    if d.shape != (3,) or not np.isfinite(d).all() or np.linalg.norm(d) == 0:
        raise ValueError("invalid SH direction")
    x, y, z = d / np.linalg.norm(d)
    values = [0.28209479177387814]
    if degree >= 1:
        values += [-0.4886025119029199*y, 0.4886025119029199*z, -0.4886025119029199*x]
    if degree >= 2:
        values += [1.0925484305920792*x*y, -1.0925484305920792*y*z,
                   0.31539156525252005*(2*z*z-x*x-y*y), -1.0925484305920792*x*z,
                   0.5462742152960396*(x*x-y*y)]
    if degree >= 3:
        values += [-0.5900435899266435*y*(3*x*x-y*y), 2.890611442640554*x*y*z,
                   -0.4570457994644658*y*(4*z*z-x*x-y*y),
                   0.3731763325901154*z*(2*z*z-3*x*x-3*y*y),
                   -0.4570457994644658*x*(4*z*z-x*x-y*y),
                   1.445305721320277*z*(x*x-y*y), -0.5900435899266435*x*(x*x-3*y*y)]
    if type(degree) is not int or degree not in range(4):
        raise ValueError("unsupported SH degree")
    return np.array(values)


def _rotation_y(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)


def evaluate_analytic_fixture(fixture):
    """Evaluate activated parameters, projected covariance and fixed samples.

    The independent reference uses pinhole Jacobians and a 0.3 pixel covariance
    filter, no antialias opacity compensation, single-Gaussian over operation.
    No values are fitted to a captured image.
    """
    camera = fixture["camera"]
    view = np.array(camera["viewMatrix"], dtype=float).reshape(4, 4, order="F")
    point = np.array(fixture["position"], dtype=float)
    scales = np.exp(np.array(fixture["logScales"], dtype=float))
    w, x, y, z = np.array(fixture["quaternionWXYZ"], dtype=float)
    qnorm = math.sqrt(w*w+x*x+y*y+z*z)
    w, x, y, z = np.array([w, x, y, z])/qnorm
    rot = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                    [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                    [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
    covariance = rot @ np.diag(scales*scales) @ rot.T
    p = (view @ np.append(point, 1))[:3]
    depth = -p[2]
    if depth <= 0:
        raise ValueError("fixture behind camera")
    fx, fy = camera["fx"], camera["fy"]
    center = np.array([fx*p[0]/depth+camera["cx"], -fy*p[1]/depth+camera["cy"]])
    jac = np.array([[fx/depth, 0, fx*p[0]/(depth*depth)],
                    [0, -fy/depth, -fy*p[1]/(depth*depth)]])
    screen_cov = jac @ view[:3, :3] @ covariance @ view[:3, :3].T @ jac.T + np.eye(2)*0.3
    eye = -view[:3, :3].T @ view[:3, 3]
    basis = sh_basis(point-eye, fixture["degree"])
    color = np.maximum(0, 0.5 + basis @ np.array(fixture["coefficientsRGB"]))
    opacity = 1/(1+math.exp(-fixture["opacityLogit"]))
    inv_cov = np.linalg.inv(screen_cov)
    samples = []
    for region in fixture["sampleRegions"]:
        for py in range(region["y"], region["y"]+region["height"]):
            for px in range(region["x"], region["x"]+region["width"]):
                delta = np.array([px, py])-center
                alpha = min(0.99, opacity*math.exp(-0.5*float(delta@inv_cov@delta)))
                if alpha < 1/255:
                    alpha = 0
                rgb = color*alpha + np.array(fixture["backgroundRGB"])*(1-alpha)
                display = np.where(rgb <= 0.0031308, 12.92*rgb, 1.055*np.power(rgb, 1/2.4)-0.055)
                samples.append({"x": px, "y": py, "region": region["id"],
                                "rgb": np.clip(rgb, 0, 1).tolist(),
                                "displayRGB": np.clip(display, 0, 1).tolist()})
    result = {"center": center.tolist(), "scales": scales.tolist(), "opacity": opacity,
              "worldCovariance": covariance.tolist(), "screenCovariance": screen_cov.tolist(),
              "shBasis": basis.tolist(), "colorRGB": color.tolist(), "samples": samples}
    if not all(math.isfinite(v) for v in _numbers(result)):
        raise ValueError("nonfinite analytic fixture")
    return result


def _numbers(value):
    if isinstance(value, dict):
        for item in value.values():
            yield from _numbers(item)
    elif isinstance(value, list):
        for item in value:
            yield from _numbers(item)
    elif isinstance(value, (int, float)):
        yield value


def create_fixtures(output_directory):
    """Create immutable eight-view SH0..3 assets and mathematical sample regions."""
    root = Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixtures = []
    # A nontrivial source-to-export similarity is exercised independently of LEGO.
    similarity = np.eye(4)
    similarity[:3, :3] = 1.7*_rotation_y(0.31)
    similarity[:3, 3] = [0.35, -0.22, 0.41]
    source_point = np.array([0.17, -0.13, 0.21, 1.0])
    position = (similarity@source_point)[:3]
    for degree in range(4):
        for side, angle in enumerate([-0.37, 0.29]):
            ident = "sh%d-direction%d" % (degree, side)
            rotation = _rotation_y(angle)
            eye = position + rotation.T@np.array([0.16, -0.11, 3.2])
            view = np.eye(4)
            view[:3, :3], view[:3, 3] = rotation, -rotation@eye
            coeff = [[0.11, -0.07, 0.19]] + [[0.027*math.sin(i+1), 0.021*math.cos(2*i+1),
                                                            -0.019*math.sin(3*i+2)] for i in range((degree+1)**2-1)]
            camera = {"width": 160, "height": 120, "fx": 127.0, "fy": 103.0,
                      "cx": 71.3, "cy": 64.1, "nearPlane": 0.01, "farPlane": 100.0,
                      "viewMatrix": view.flatten(order="F").tolist(), "shDegree": degree}
            cx = camera["fx"]*(-0.16)/3.2+camera["cx"]
            cy = -camera["fy"]*0.11/3.2+camera["cy"]
            fixture = {"id": ident, "degree": degree, "camera": camera,
                       "sourcePoint": source_point[:3].tolist(), "sourceToExport": similarity.flatten(order="F").tolist(),
                       "exportToSource": np.linalg.inv(similarity).flatten(order="F").tolist(),
                       "position": position.tolist(), "logScales": np.log([0.24, 0.105, 0.16]).tolist(),
                       "quaternionWXYZ": [math.cos(0.39), 0, 0, math.sin(0.39)],
                       "opacityLogit": 0.73, "coefficientsRGB": coeff, "backgroundRGB": [0.0, 0.0, 0.0],
                       "sampleRegions": [{"id": "interior", "x": int(round(cx))-1, "y": int(round(cy))-1,
                                          "width": 3, "height": 3},
                                         {"id": "background", "x": 1, "y": 1, "width": 3, "height": 3}],
                       "analyticContract": "pinhole-jacobian-covariance-plus-0.3;alpha=min(.99,sigmoid(logit)*exp(-q/2));cutoff=1/255;SH(point-eye);clamp-min0;over-black",
                       "mutations": ["wrong-view-y-axis", "omit-principal-point", "negate-SH-nonDC", "swap-SH-red-blue"]}
            fixture["analytic"] = evaluate_analytic_fixture(fixture)
            full_fixture = dict(fixture)
            full_fixture["sampleRegions"] = [{"id": "full-image", "x": 0, "y": 0,
                                               "width": camera["width"], "height": camera["height"]}]
            full = evaluate_analytic_fixture(full_fixture)["samples"]
            for field, suffix in (("rgb", "analytic-linear"), ("displayRGB", "analytic-display")):
                pixels = np.array([sample[field] for sample in full]).reshape(camera["height"], camera["width"], 3)
                path = root/(ident+"-"+suffix+".png")
                with path.open("xb") as stream:
                    Image.fromarray(np.rint(pixels*255).astype(np.uint8), "RGB").save(stream, format="PNG")
                    stream.flush()
                    os.fsync(stream.fileno())
                fixture[suffix+"Reference"] = _identity(path)
            props = ["x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2"]
            rest = [coeff[j][ch] for ch in range(3) for j in range(1, len(coeff))]
            props += ["f_rest_%d" % i for i in range(len(rest))]
            props += ["opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"]
            values = position.tolist()+coeff[0]+rest+[fixture["opacityLogit"]]+fixture["logScales"]+fixture["quaternionWXYZ"]
            ply = "ply\nformat ascii 1.0\nelement vertex 1\n"+"".join("property float "+p+"\n" for p in props)+"end_header\n"
            ply += " ".join(format(v, ".17g") for v in values)+"\n"
            _write(root/(ident+".ply"), ply.encode("ascii"))
            fixture["sourcePLY"] = _identity(root/(ident+".ply"))
            _write(root/(ident+".json"), (json.dumps(fixture, indent=2, allow_nan=False)+"\n").encode())
            fixture["fixtureIdentity"] = _identity(root/(ident+".json"))
            fixtures.append(fixture)
    result = {"schemaVersion": 1, "fixtures": fixtures,
              "gates": {"centerErrorPixelsMax": 0.25, "cpuAbsTolerance": 1e-5,
                        "cpuRelTolerance": 1e-5, "syntheticRgbAbsMax": 2/255}}
    _write(root/"fixtures.json", (json.dumps(result, indent=2, allow_nan=False)+"\n").encode())
    return result
