"""Render the existing STL geometry; does not regenerate or modify CAD files.

These are mesh previews, not SolidWorks screenshots or hardware validation.
Printable STL files have their own Z=0; restore the source model's assembly Z.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import numpy as np

ENCLOSURE = Path(__file__).resolve().parents[1]
ROOT = ENCLOSURE.parents[1]
COLORS = {"base": "#7d9ab4", "lid": "#d6e2ec", "electronics_tray": "#48aaa2"}
LABELS = {"base": "Base", "lid": "Lid", "electronics_tray": "Electronics tray"}
ASSEMBLY_Z = {"base": 0.0, "lid": 36.0, "electronics_tray": 2.4}
EXPLODED_EXTRA_Z = {"base": 0, "lid": 104, "electronics_tray": 66}


def load_stl(path):
    data = path.read_bytes()
    if len(data) < 84:
        raise ValueError(f"Truncated STL: {path.name}")
    count = struct.unpack_from("<I", data, 80)[0]
    if len(data) != 84 + count * 50:
        raise ValueError(f"Expected binary STL: {path.name}")
    dtype = np.dtype([("normal", "<f4", (3,)), ("vertices", "<f4", (3, 3)), ("attribute", "<u2")])
    triangles = np.frombuffer(data, dtype=dtype, count=count, offset=84)["vertices"].astype(float)
    if not np.isfinite(triangles).all():
        raise ValueError(f"Nonfinite coordinates: {path.name}")
    return triangles


def draw_parts(ax, parts, elev=35, azim=-58):
    """Orthographic CPU z-buffer; handles concave meshes and holes correctly."""
    elevation, azimuth = np.radians([elev, azim])
    direction = np.array([np.cos(elevation)*np.cos(azimuth),
                          np.cos(elevation)*np.sin(azimuth), np.sin(elevation)])
    right = np.array([-np.sin(azimuth), np.cos(azimuth), 0])
    up = np.cross(direction, right)
    transform = np.stack([right, up, direction], axis=1)
    projected = [(name, triangles @ transform, triangles) for name, triangles in parts]
    points = np.concatenate([triangles.reshape(-1, 3) for _, triangles, _ in projected])
    low, high = points[:, :2].min(axis=0), points[:, :2].max(axis=0)
    width, height = 1500, 1150
    scale = min((width-80)/(high[0]-low[0]), (height-80)/(high[1]-low[1]))
    center = (low+high)/2
    pixels = np.empty((height, width, 3), dtype=np.uint8)
    pixels[:] = [244,247,250]
    depth = np.full((height, width), -np.inf)
    light = np.array([-0.4,-0.5,0.85]); light /= np.linalg.norm(light)
    for name, camera, world in projected:
        normals = np.cross(world[:,1]-world[:,0], world[:,2]-world[:,0])
        normals /= np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-12)
        intensity = 0.50+0.50*np.abs(normals @ light)
        colors = (255*intensity[:,None]*np.array(matplotlib.colors.to_rgb(COLORS[name]))).astype(np.uint8)
        screen = camera.copy()
        screen[:,:,0] = (camera[:,:,0]-center[0])*scale+width/2
        screen[:,:,1] = height/2-(camera[:,:,1]-center[1])*scale
        for triangle, color in zip(screen,colors):
            x0,y0,z0 = triangle[0]; x1,y1,z1 = triangle[1]; x2,y2,z2 = triangle[2]
            denominator = (y1-y2)*(x0-x2)+(x2-x1)*(y0-y2)
            if abs(denominator)<1e-9: continue
            xmin = max(0,int(np.floor(triangle[:,0].min())))
            xmax = min(width-1,int(np.ceil(triangle[:,0].max())))
            ymin = max(0,int(np.floor(triangle[:,1].min())))
            ymax = min(height-1,int(np.ceil(triangle[:,1].max())))
            if xmin>xmax or ymin>ymax: continue
            y,x = np.mgrid[ymin:ymax+1,xmin:xmax+1]
            x=x+0.5; y=y+0.5
            a=((y1-y2)*(x-x2)+(x2-x1)*(y-y2))/denominator
            b=((y2-y0)*(x-x2)+(x0-x2)*(y-y2))/denominator
            c=1-a-b
            z=a*z0+b*z1+c*z2
            region=depth[ymin:ymax+1,xmin:xmax+1]
            visible=(a>=-1e-7)&(b>=-1e-7)&(c>=-1e-7)&(z>region)
            region[visible]=z[visible]
            pixels[ymin:ymax+1,xmin:xmax+1][visible]=color
    ax.imshow(pixels)
    ax.set_axis_off()


def footer(fig):
    fig.text(0.5, 0.025, "Existing CAD mesh preview | Dimensions in mm | Untested physical prototype",
             ha="center", color="#526477", fontsize=11)


def assembly(meshes, output, exploded=False):
    fig = plt.figure(figsize=(12, 9), facecolor="#f4f7fa")
    ax = fig.add_axes([0.02, 0.09, 0.96, 0.80], )
    placed = []
    for name, triangles in meshes.items():
        z = ASSEMBLY_Z[name]
        if exploded:
            z += EXPLODED_EXTRA_Z[name]
        triangles = triangles + np.array([0, 0, z])
        placed.append((name, triangles))
    draw_parts(ax, placed, elev=35)
    fig.suptitle("Enclosure — exploded geometry" if exploded else "Enclosure — assembled geometry",
                 fontsize=22, weight="bold", color="#233449", y=0.95)
    fig.text(0.5, 0.895, "Parts separated for visibility; offsets are illustrative" if exploded
             else "90 × 130 × 38.4 mm body | No electronics, battery, or fasteners modeled",
             ha="center", fontsize=12, color="#526477")
    fig.legend(handles=[Patch(color=COLORS[n], label=LABELS[n]) for n in meshes],
               loc="lower center", bbox_to_anchor=(0.5, 0.05), ncol=3, frameon=False)
    footer(fig)
    fig.savefig(output, dpi=160, facecolor=fig.get_facecolor())
    plt.close(fig)


def components(meshes, output):
    fig = plt.figure(figsize=(16, 6.5), facecolor="#f4f7fa")
    for index, (name, triangles) in enumerate(meshes.items(), 1):
        ax = fig.add_subplot(1, 3, index, )
        draw_parts(ax, [(name, triangles)], elev=55 if name == "base" else 36)
        ax.set_title(LABELS[name], fontsize=16, color="#233449", pad=14)
    fig.suptitle("Enclosure parts — existing printable STL geometry", fontsize=22,
                 color="#233449", weight="bold", y=0.97)
    footer(fig)
    fig.subplots_adjust(left=0.01, right=0.99, top=0.83, bottom=0.12, wspace=0.03)
    fig.savefig(output, dpi=160, facecolor=fig.get_facecolor())
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/images")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    meshes = {name: load_stl(ENCLOSURE / "stl" / f"{name}.stl") for name in COLORS}
    assembly(meshes, args.output / "enclosure-assembled.png")
    assembly(meshes, args.output / "enclosure-exploded.png", exploded=True)
    components(meshes, args.output / "enclosure-parts.png")
    report = {"method": "Orthographic CPU z-buffer of existing binary STL meshes, composed with Matplotlib; not a SolidWorks screenshot",
              "assembly_z_mm": ASSEMBLY_Z,
              "exploded_extra_z_mm": EXPLODED_EXTRA_Z,
              "inputs": {name: {"file": f"hardware/enclosure/stl/{name}.stl",
                                "sha256": hashlib.sha256((ENCLOSURE / "stl" / f"{name}.stl").read_bytes()).hexdigest(),
                                "triangles": len(triangles)} for name, triangles in meshes.items()}}
    (args.output / "enclosure-preview-provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Rendered three enclosure previews without modifying the CAD inputs.")


if __name__ == "__main__":
    main()
